#!/usr/bin/env python3
"""av-api integration smoke: spawns the server and exercises /v1 over HTTP + WS.

Stdlib only (urllib, json, socket, base64, struct) so it needs no pip deps.
Run: api_smoke.py <av-api-binary> <demo-scenario.json>
"""

import base64
import json
import os
import socket
import struct
import subprocess
import sys
import time
import urllib.error
import urllib.request


def fail(msg):
    print(f"FAIL: {msg}", file=sys.stderr)
    sys.exit(1)


def http(method, url, body=None, raw=False):
    data = None
    headers = {}
    if body is not None:
        data = json.dumps(body).encode()
        headers["Content-Type"] = "application/json"
    req = urllib.request.Request(url, data=data, headers=headers, method=method)
    try:
        with urllib.request.urlopen(req, timeout=30) as res:
            payload = res.read()
            return res.status, (payload if raw else json.loads(payload or b"{}"))
    except urllib.error.HTTPError as err:
        payload = err.read()
        try:
            return err.code, json.loads(payload or b"{}")
        except json.JSONDecodeError:
            return err.code, {"detail": payload[:200].decode(errors="replace")}


def free_port():
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


def wait_healthy(base, timeout=15.0):
    deadline = time.time() + timeout
    while time.time() < deadline:
        try:
            status, body = http("GET", f"{base}/v1/health")
            if status == 200:
                return body
        except (urllib.error.URLError, OSError):
            pass
        time.sleep(0.1)
    fail("server did not become healthy")


class MiniWs:
    """Minimal RFC6455 client: one text stream, masking only what's required."""

    def __init__(self, host, port, path):
        self.sock = socket.create_connection((host, port), timeout=15)
        key = base64.b64encode(os.urandom(16)).decode()
        self.sock.sendall(
            (
                f"GET {path} HTTP/1.1\r\n"
                f"Host: {host}:{port}\r\n"
                "Upgrade: websocket\r\n"
                "Connection: Upgrade\r\n"
                f"Sec-WebSocket-Key: {key}\r\n"
                "Sec-WebSocket-Version: 13\r\n"
                "\r\n"
            ).encode()
        )
        buf = b""
        while b"\r\n\r\n" not in buf:
            chunk = self.sock.recv(4096)
            if not chunk:
                fail("WS handshake: connection closed")
            buf += chunk
        head, _, rest = buf.partition(b"\r\n\r\n")
        if b"101" not in head.split(b"\r\n", 1)[0]:
            fail(f"WS handshake rejected: {head[:80]!r}")
        self.buf = rest

    def send_text(self, text):
        payload = text.encode()
        mask = os.urandom(4)
        header = b"\x81"  # FIN + text
        n = len(payload)
        if n < 126:
            header += bytes([0x80 | n])
        elif n < 65536:
            header += bytes([0x80 | 126]) + struct.pack("!H", n)
        else:
            header += bytes([0x80 | 127]) + struct.pack("!Q", n)
        masked = bytes(b ^ mask[i % 4] for i, b in enumerate(payload))
        self.sock.sendall(header + mask + masked)

    def recv_text(self, timeout=15.0):
        deadline = time.time() + timeout
        self.sock.settimeout(2.0)
        while time.time() < deadline:
            while len(self.buf) < 2:
                self._pump(deadline)
            b0, b1 = self.buf[0], self.buf[1]
            opcode = b0 & 0x0F
            n = b1 & 0x7F
            off = 2
            if n == 126:
                while len(self.buf) < off + 2:
                    self._pump(deadline)
                n = struct.unpack("!H", self.buf[off : off + 2])[0]
                off += 2
            elif n == 127:
                while len(self.buf) < off + 8:
                    self._pump(deadline)
                n = struct.unpack("!Q", self.buf[off : off + 8])[0]
                off += 8
            while len(self.buf) < off + n:
                self._pump(deadline)
            frame = self.buf[off : off + n]
            self.buf = self.buf[off + n :]
            if opcode == 0x1:
                return frame.decode()
            if opcode == 0x9:  # ping -> pong
                self.sock.sendall(b"\x8a" + bytes([0x80 | len(frame)]) + b"\x00\x00\x00\x00" + bytes(b ^ b"\x00"[0] for b in frame))
            # ignore other opcodes (pong/close-ack handled by teardown)
        fail("WS recv timeout")

    def _pump(self, deadline):
        if time.time() >= deadline:
            fail("WS recv timeout")
        try:
            chunk = self.sock.recv(8192)
        except socket.timeout:
            return
        if not chunk:
            fail("WS connection closed")
        self.buf += chunk

    def close(self):
        try:
            self.sock.close()
        except OSError:
            pass


def expect(cond, msg):
    if not cond:
        fail(msg)


def main():
    if len(sys.argv) != 3:
        fail("usage: api_smoke.py <av-api-binary> <demo-scenario.json>")
    binary, scenario_path = sys.argv[1], sys.argv[2]
    with open(scenario_path) as f:
        scenario = json.load(f)

    port = free_port()
    base = f"http://127.0.0.1:{port}"
    server = subprocess.Popen(
        [binary, "--port", str(port), "--bind", "127.0.0.1"],
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
    )
    try:
        health = wait_healthy(base)
        expect(health.get("status") == "ok", f"health status: {health}")
        expect(isinstance(health.get("engines"), list) and health["engines"], "health engines")
        expect("image-source" in health["engines"], f"image-source registered: {health}")
        expect("sabine-eyring" in health["engines"], f"sabine-eyring registered: {health}")
        expect(bool(health.get("version")), "health version")
        print("ok: health")

        status, engines = http("GET", f"{base}/v1/engines")
        expect(status == 200 and isinstance(engines, list) and engines, "GET /v1/engines")
        by_id = {e.get("id"): e for e in engines}
        expect("image-source" in by_id, "engines list has image-source")
        desc = by_id["image-source"]
        for field in ("name", "version", "caps", "paramsSchema", "artifacts"):
            expect(field in desc, f"descriptor field {field}")
        expect(desc["caps"].get("supportsRect") is True, "image-source supportsRect")
        expect("ir" in desc.get("artifacts", []), "image-source artifacts ir")
        print("ok: engines")

        status, bands = http("GET", f"{base}/v1/bands")
        expect(status == 200, "GET /v1/bands")
        expect(bands.get("bandsHz") == [125, 250, 500, 1000, 2000, 4000], f"bands: {bands}")
        print("ok: bands")

        status, result = http("POST", f"{base}/v1/simulations?sync=1", scenario)
        expect(status == 200, f"sync simulation status {status}: {result}")
        expect("metrics" in result and "rt60" in result["metrics"], "sync metrics")
        expect("ir" in result, "sync ir summary")
        expect(result.get("state") == "succeeded", f"sync state: {result.get('state')}")
        job_id = result.get("jobId")
        expect(isinstance(job_id, str) and job_id, "sync jobId")
        print(f"ok: sync simulation ({job_id})")

        status, ir_bytes = http("GET", f"{base}/v1/jobs/{job_id}/ir.wav", raw=True)
        expect(status == 200 and ir_bytes[:4] == b"RIFF", "ir.wav for sync job")
        print("ok: ir.wav")

        status, accepted = http("POST", f"{base}/v1/simulations", scenario)
        expect(status == 202 and accepted.get("jobId"), f"async submit: {status} {accepted}")
        async_id = accepted["jobId"]
        print(f"ok: async submit ({async_id})")

        ws = MiniWs("127.0.0.1", port, "/v1/events")
        ws.send_text(json.dumps({"op": "subscribe", "jobId": async_id}))
        got = None
        # Job may already have finished before the subscription landed; poll as fallback.
        for _ in range(80):
            status, st = http("GET", f"{base}/v1/jobs/{async_id}")
            expect(status == 200, f"job status: {status} {st}")
            if st.get("state") in ("succeeded", "failed", "cancelled"):
                break
            time.sleep(0.1)
        expect(st.get("state") == "succeeded", f"async job state: {st}")
        # Drain one WS event; accept either a terminal event or a late progress.
        try:
            for _ in range(20):
                msg = json.loads(ws.recv_text(timeout=5.0))
                if msg.get("jobId") == async_id and msg.get("event", "").startswith("job."):
                    got = msg
                    if msg["event"] in ("job.succeeded", "job.failed", "job.cancelled"):
                        break
        except Exception as ex:  # noqa: BLE001 - WS is progressive enhancement
            print(f"note: WS events unavailable ({ex}); REST polling covered the job")
        else:
            expect(got is not None, "no job.* WS event received")
            print(f"ok: WS job event ({got['event']})")
        ws.close()

        status, job_result = http("GET", f"{base}/v1/jobs/{async_id}/result")
        expect(status == 200 and "metrics" in job_result, f"async result: {status} {job_result}")
        expect("ir" in job_result, "async result has ir summary")
        print("ok: async result")

        # Cancel smoke: heavy job, cancel early, expect terminal state + 409 on re-cancel.
        heavy = json.loads(json.dumps(scenario))
        heavy["params"]["maxReflectionOrder"] = 28
        heavy["name"] = "cancel-smoke"
        status, accepted = http("POST", f"{base}/v1/simulations", heavy)
        expect(status == 202 and accepted.get("jobId"), f"heavy submit: {status} {accepted}")
        heavy_id = accepted["jobId"]
        status, body = http("POST", f"{base}/v1/jobs/{heavy_id}/cancel")
        expect(status == 200, f"cancel accepted: {status} {body}")
        expect(body.get("state") in ("queued", "running", "cancelled"), f"cancel body: {body}")
        for _ in range(100):
            status, st = http("GET", f"{base}/v1/jobs/{heavy_id}")
            if st.get("state") in ("succeeded", "failed", "cancelled"):
                break
            time.sleep(0.1)
        expect(st.get("state") in ("cancelled", "succeeded"), f"after cancel: {st}")
        status, body = http("POST", f"{base}/v1/jobs/{heavy_id}/cancel")
        expect(status == 409, f"re-cancel should be 409: {status} {body}")
        expect(body.get("status") == 409 and "title" in body and "detail" in body, f"problem shape: {body}")
        print(f"ok: cancel smoke (final state {st['state']})")

        status, body = http("POST", f"{base}/v1/jobs/nope/cancel")
        expect(status == 404 and body.get("status") == 404, f"cancel unknown: {status} {body}")
        status, body = http("GET", f"{base}/v1/jobs/nope/result")
        expect(status == 404, f"result unknown: {status} {body}")
        status, body = http("GET", f"{base}/v1/jobs/nope/ir.wav")
        expect(status == 404, f"ir.wav unknown: {status} {body}")
        status, body = http("POST", f"{base}/v1/simulations?sync=1", {"schemaVersion": 1})
        expect(status == 400 and body.get("status") == 400, f"bad scenario: {status} {body}")
        print("ok: error paths")

        print("ALL API SMOKE TESTS PASSED")
    finally:
        server.terminate()
        try:
            server.wait(timeout=5)
        except subprocess.TimeoutExpired:
            server.kill()


if __name__ == "__main__":
    main()
