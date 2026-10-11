#!/usr/bin/env python3
"""av-api integration smoke: spawns the server and exercises /v1 over HTTP + WS.

Stdlib only (urllib, json, socket, base64, struct) so it needs no pip deps.
Run: api_smoke.py <av-api-binary> <demo-scenario.json> [source_catalog.json]
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
    if len(sys.argv) not in (3, 4):
        fail("usage: api_smoke.py <av-api-binary> <demo-scenario.json> [source_catalog.json]")
    binary, scenario_path = sys.argv[1], sys.argv[2]
    catalog_path = sys.argv[3] if len(sys.argv) == 4 else None
    with open(scenario_path) as f:
        scenario = json.load(f)
    fixtures_dir = os.path.dirname(os.path.abspath(scenario_path))
    mesh_scenario = None
    mesh_scenario_path = os.path.join(fixtures_dir, "box-scenario.json")
    box_obj = os.path.join(fixtures_dir, "box.obj")
    if os.path.isfile(mesh_scenario_path) and os.path.isfile(box_obj):
        with open(mesh_scenario_path) as f:
            mesh_scenario = json.load(f)
        # API resolves relative mesh uris against the server CWD, so pin the
        # fixture to an absolute path for a deterministic smoke run.
        mesh_scenario["room"]["mesh"]["uri"] = box_obj

    port = free_port()
    base = f"http://127.0.0.1:{port}"
    env = os.environ.copy()
    if catalog_path:
        env["AV_SOURCE_CATALOG"] = os.path.abspath(catalog_path)
    server = subprocess.Popen(
        [binary, "--port", str(port), "--bind", "127.0.0.1"],
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        env=env,
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

        # Source catalog: always 200 with a disclaimer + sources array.
        status, catalog = http("GET", f"{base}/v1/source-catalog")
        expect(status == 200, f"GET /v1/source-catalog: {status} {catalog}")
        expect(isinstance(catalog.get("disclaimer"), str) and catalog["disclaimer"],
               f"catalog disclaimer: {catalog}")
        sources = catalog.get("sources")
        expect(isinstance(sources, list), f"catalog sources array: {catalog}")
        if catalog_path:
            expect(sources, f"catalog non-empty with AV_SOURCE_CATALOG: {catalog}")
            for entry in sources:
                for field in ("id", "name", "powerDb", "spectrum", "directivity", "disclaimer"):
                    expect(field in entry, f"catalog entry field {field}: {entry}")
                spec = entry["spectrum"]
                expect(len(spec.get("bandsHz", [])) == len(spec.get("responseDb", [])),
                       f"catalog spectrum lengths: {entry}")
                expect(entry["directivity"].get("type") in
                       ("omni", "dipole", "cardioid", "custom"),
                       f"catalog directivity type: {entry}")
        print(f"ok: source catalog ({len(sources)} entries)")

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

        # P2e: runs history — every terminal job lands in GET /v1/runs.
        status, runs = http("GET", f"{base}/v1/runs")
        expect(status == 200 and isinstance(runs, list), f"GET /v1/runs: {status} {runs}")
        run_ids = [r.get("id") for r in runs]
        run_job_ids = [r.get("jobId") for r in runs]
        expect(job_id in run_job_ids, f"sync run recorded: {run_job_ids}")
        expect(async_id in run_job_ids, f"async run recorded: {run_job_ids}")
        expect(len(set(run_ids)) == len(run_ids), f"run ids unique: {run_ids}")
        for r in runs:
            # Handle split: `id` is the run key (run-…), `jobId` the artifact handle.
            expect(r.get("id") != r.get("jobId"), f"run id != jobId: {r}")
            expect(str(r.get("id", "")).startswith("run-"), f"run id prefix: {r}")
            expect(str(r.get("jobId", "")).startswith("job-"), f"run jobId prefix: {r}")
            expect(r.get("state") in ("succeeded", "failed", "cancelled"), f"run state: {r}")
            expect(r.get("engine") == scenario["params"]["engine"], f"run engine: {r}")
            expect(r.get("scenarioName") == scenario.get("name"), f"run scenarioName: {r}")
            expect("submittedAt" in r and "finishedAt" in r, f"run timestamps: {r}")
            expect(isinstance(r.get("paramsDigest"), dict) and "engine" in r["paramsDigest"],
                   f"run paramsDigest: {r}")
        expect(runs[0]["finishedAt"] >= runs[-1]["finishedAt"],
               f"runs newest first: {[r['finishedAt'] for r in runs]}")
        print(f"ok: runs list ({len(runs)} recorded)")

        engine_name = scenario["params"]["engine"]
        status, filtered = http("GET", f"{base}/v1/runs?engine={engine_name}")
        expect(status == 200 and filtered, f"runs engine filter: {status} {filtered}")
        expect(all(r["engine"] == engine_name for r in filtered), f"filter contents: {filtered}")
        status, none_runs = http("GET", f"{base}/v1/runs?engine=no-such-engine")
        expect(status == 200 and none_runs == [], f"filter miss: {status} {none_runs}")
        print("ok: runs engine filter")

        # GET /v1/runs/{id} accepts either handle: jobId (backwards) and runId.
        status, run = http("GET", f"{base}/v1/runs/{async_id}")
        expect(status == 200, f"GET /v1/runs/{{jobId}}: {status} {run}")
        run_id = run.get("id")
        expect(run.get("jobId") == async_id, f"run detail jobId: {run}")
        expect(isinstance(run_id, str) and run_id.startswith("run-"),
               f"run detail id: {run}")
        expect(run.get("state") == "succeeded", f"run detail state: {run}")
        links = run.get("links", {})
        expect(links.get("result") == f"/v1/jobs/{async_id}/result", f"run links result: {links}")
        expect(links.get("ir") == f"/v1/jobs/{async_id}/ir.wav", f"run links ir: {links}")
        expect(links.get("field") == f"/v1/jobs/{async_id}/field", f"run links field: {links}")
        status, by_run = http("GET", f"{base}/v1/runs/{run_id}")
        expect(status == 200 and by_run.get("id") == run_id
               and by_run.get("jobId") == async_id, f"GET /v1/runs/{{runId}}: {status} {by_run}")
        status, body = http("GET", f"{base}/v1/runs/nope")
        expect(status == 404 and body.get("status") == 404, f"run 404: {status} {body}")
        print("ok: run detail (runId+jobId) + 404")

        # Scene Store + WS scene.patch (interactive mode, docs/idea.md).
        status, created = http("POST", f"{base}/v1/scenarios", scenario)
        expect(status == 201, f"POST /v1/scenarios: {status} {created}")
        scene_id = created.get("sceneId")
        expect(isinstance(scene_id, str) and scene_id, f"sceneId: {created}")
        expect(created.get("sceneVersion") == 1, f"initial sceneVersion: {created}")
        print(f"ok: scene create ({scene_id})")

        status, loaded = http("GET", f"{base}/v1/scenarios/{scene_id}")
        expect(status == 200, f"GET /v1/scenarios: {status} {loaded}")
        expect(loaded.get("sceneId") == scene_id, f"scene get id: {loaded}")
        expect(loaded.get("sceneVersion") == 1, f"scene get version: {loaded}")
        body_scenario = loaded.get("scenario")
        expect(isinstance(body_scenario, dict) and body_scenario.get("name") == scenario.get("name"),
               f"scenario payload: {body_scenario}")
        expect(body_scenario.get("sources") == scenario.get("sources"),
               f"scenario sources round-trip: {body_scenario.get('sources')}")
        status, body = http("GET", f"{base}/v1/scenarios/nope")
        expect(status == 404 and body.get("status") == 404, f"scene 404: {status} {body}")
        print("ok: scene get + 404")

        # P3c: two connections — requester + subscribeScene watcher. The
        # requester gets the ack; the watcher gets the fan-out.
        ws = MiniWs("127.0.0.1", port, "/v1/events")
        watcher = MiniWs("127.0.0.1", port, "/v1/events")
        for client in (ws, watcher):
            client.send_text(json.dumps({"op": "subscribeScene", "sceneId": scene_id}))
            sub = json.loads(client.recv_text(timeout=5.0))
            expect(sub.get("event") == "scene.subscribed", f"scene.subscribed: {sub}")
            expect(sub.get("sceneId") == scene_id, f"sub sceneId: {sub}")
        ws.send_text(json.dumps({
            "op": "scene.patch",
            "sceneId": scene_id,
            "ops": [
                {"op": "move", "entityType": "source", "id": "src-1",
                 "position": [2.0, 1.2, 2.0]},
                {"op": "setPower", "entityType": "source", "id": "src-1",
                 "powerDb": 55.0},
            ],
        }))
        ack = json.loads(ws.recv_text(timeout=5.0))
        expect(ack.get("event") == "scene.patched", f"scene.patched ack: {ack}")
        expect(ack.get("sceneId") == scene_id, f"ack sceneId: {ack}")
        expect(ack.get("sceneVersion") == 2, f"ack version: {ack}")
        expect(ack.get("applied") == 2, f"ack applied: {ack}")
        bcast = json.loads(watcher.recv_text(timeout=5.0))
        expect(bcast.get("event") == "scene.patched", f"fan-out event: {bcast}")
        expect(bcast.get("sceneId") == scene_id, f"fan-out sceneId: {bcast}")
        expect(bcast.get("sceneVersion") == 2 and bcast.get("applied") == 2,
               f"fan-out payload: {bcast}")
        print("ok: WS scene.patch round-trip + fan-out to second client")

        # Unknown op -> scene.error (and the scene stays untouched).
        ws.send_text(json.dumps({
            "op": "scene.patch",
            "sceneId": scene_id,
            "ops": [{"op": "rotate", "entityType": "source", "id": "src-1",
                     "yawDeg": 30.0}],
        }))
        err = json.loads(ws.recv_text(timeout=5.0))
        expect(err.get("event") == "scene.error", f"scene.error event: {err}")
        expect("rotate" in err.get("detail", ""), f"scene.error detail: {err}")
        berr = json.loads(watcher.recv_text(timeout=5.0))
        expect(berr.get("event") == "scene.error" and berr.get("sceneId") == scene_id,
               f"scene.error fan-out: {berr}")
        ws.send_text(json.dumps({
            "op": "scene.patch",
            "sceneId": "nope",
            "ops": [{"op": "move", "entityType": "source", "id": "src-1",
                     "position": [0, 0, 0]}],
        }))
        err = json.loads(ws.recv_text(timeout=5.0))
        expect(err.get("event") == "scene.error", f"scene.error unknown scene: {err}")
        ws.close()
        watcher.close()
        print("ok: WS scene.error paths")

        status, loaded = http("GET", f"{base}/v1/scenarios/{scene_id}")
        expect(status == 200 and loaded.get("sceneVersion") == 2,
               f"scene version after patch: {status} {loaded}")
        moved = loaded["scenario"]["sources"][0]
        expect(moved.get("id") == "src-1", f"source id: {moved}")
        expect(moved.get("position") == [2.0, 1.2, 2.0], f"moved position: {moved}")
        expect(moved.get("powerDb") == 55.0, f"patched powerDb: {moved}")
        print("ok: scene state after patch")

        # Scene-based submit: {"sceneId"} snapshot + sceneVersion in the run.
        status, accepted = http("POST", f"{base}/v1/simulations", {"sceneId": scene_id})
        expect(status == 202 and accepted.get("jobId"), f"scene submit: {status} {accepted}")
        scene_job = accepted["jobId"]
        for _ in range(80):
            status, st = http("GET", f"{base}/v1/jobs/{scene_job}")
            expect(status == 200, f"scene job status: {status} {st}")
            if st.get("state") in ("succeeded", "failed", "cancelled"):
                break
            time.sleep(0.1)
        expect(st.get("state") == "succeeded", f"scene job state: {st}")
        status, run = http("GET", f"{base}/v1/runs/{scene_job}")
        expect(status == 200, f"scene run: {status} {run}")
        expect(isinstance(run.get("sceneVersion"), int)
               and not isinstance(run.get("sceneVersion"), bool)
               and run.get("sceneVersion") == 2, f"run sceneVersion uint64: {run}")
        expect(run.get("paramsDigest", {}).get("sceneVersion") == 2,
               f"paramsDigest sceneVersion: {run}")
        print("ok: scene-based simulation + run sceneVersion")

        # Scene submit + SimParams overlay: engine/methodParams ride on the
        # snapshot body and still record sceneVersion in the run.
        status, accepted = http("POST", f"{base}/v1/simulations", {
            "sceneId": scene_id,
            "engine": "sabine-eyring",
            "methodParams": {"probe": 7},
            "params": {"maxReflectionOrder": 3},
        })
        expect(status == 202 and accepted.get("jobId"),
               f"overlay submit: {status} {accepted}")
        overlay_job = accepted["jobId"]
        for _ in range(80):
            status, st = http("GET", f"{base}/v1/jobs/{overlay_job}")
            expect(status == 200, f"overlay job status: {status} {st}")
            if st.get("state") in ("succeeded", "failed", "cancelled"):
                break
            time.sleep(0.1)
        expect(st.get("state") == "succeeded", f"overlay job state: {st}")
        status, run = http("GET", f"{base}/v1/runs/{overlay_job}")
        expect(status == 200, f"overlay run: {status} {run}")
        digest = run.get("paramsDigest", {})
        expect(run.get("engine") == "sabine-eyring", f"overlay engine: {run}")
        expect(digest.get("engine") == "sabine-eyring", f"overlay digest engine: {run}")
        expect(digest.get("methodParams") == {"probe": 7},
               f"overlay digest methodParams: {run}")
        expect(digest.get("maxReflectionOrder") == 3,
               f"overlay digest maxReflectionOrder: {run}")
        expect(digest.get("sceneVersion") == 2, f"overlay digest sceneVersion: {run}")
        print("ok: scene submit with engine/methodParams/params overlay")

        status, body = http("POST", f"{base}/v1/simulations",
                            {"sceneId": scene_id, "engine": "no-such-engine"})
        expect(status == 400 and "engine" in (body.get("title", "") + body.get("detail", "")).lower(),
               f"overlay unknown engine 400: {status} {body}")
        status, body = http("POST", f"{base}/v1/simulations",
                            {"sceneId": scene_id, "params": {"nope": 1}})
        expect(status == 400, f"overlay unknown params key 400: {status} {body}")
        status, body = http("POST", f"{base}/v1/simulations",
                            {"sceneId": scene_id, "room": {"type": "rect"}})
        expect(status == 400, f"overlay rejects non-overlay keys: {status} {body}")
        print("ok: scene overlay error paths")

        status, body = http("POST", f"{base}/v1/simulations", {"sceneId": "nope"})
        expect(status == 404 and body.get("status") == 404, f"scene submit 404: {status} {body}")
        status, body = http("POST", f"{base}/v1/scenarios", {"schemaVersion": 1})
        expect(status == 400 and body.get("status") == 400, f"bad scene create: {status} {body}")
        print("ok: scene error paths")

        # P2c: mesh room with loadedMesh filled at the API boundary completes.
        if mesh_scenario is not None:
            status, accepted = http("POST", f"{base}/v1/simulations", mesh_scenario)
            expect(status == 202 and accepted.get("jobId"), f"mesh submit: {status} {accepted}")
            mesh_id = accepted["jobId"]
            for _ in range(100):
                status, st = http("GET", f"{base}/v1/jobs/{mesh_id}")
                expect(status == 200, f"mesh job status: {status} {st}")
                if st.get("state") in ("succeeded", "failed", "cancelled"):
                    break
                time.sleep(0.1)
            expect(st.get("state") == "succeeded", f"mesh job state: {st}")
            status, mesh_result = http("GET", f"{base}/v1/jobs/{mesh_id}/result")
            expect(status == 200 and "metrics" in mesh_result, f"mesh result: {status} {mesh_result}")
            print(f"ok: mesh scenario ({mesh_id})")

            # Unresolvable mesh uri fails fast at the boundary with a clear error.
            broken = json.loads(json.dumps(mesh_scenario))
            broken["room"]["mesh"]["uri"] = os.path.join(fixtures_dir, "no-such-mesh.obj")
            status, body = http("POST", f"{base}/v1/simulations", broken)
            expect(status == 400 and "mesh" in body.get("title", "").lower(),
                   f"broken mesh uri: {status} {body}")
            print("ok: broken mesh uri rejected")

            # P2d: spatial field artifact + GET /v1/jobs/{id}/field.
            field_scenario = json.loads(json.dumps(mesh_scenario))
            field_scenario["params"]["fieldGrid"] = {
                "enabled": True, "nx": 6, "ny": 4, "nz": 5,
            }
            field_scenario["params"]["methodParams"]["rayCount"] = 400
            field_scenario["params"]["methodParams"]["maxBounces"] = 16
            status, accepted = http("POST", f"{base}/v1/simulations?sync=1", field_scenario)
            expect(status == 200, f"field sync simulation: {status} {accepted}")
            field_id = accepted.get("jobId")
            expect(isinstance(field_id, str) and field_id, "field jobId")
            expect("field" in accepted, f"sync result includes field: {accepted.keys()}")
            expect(accepted["field"].get("nx") == 6, f"sync field dims: {accepted['field']}")

            status, field = http("GET", f"{base}/v1/jobs/{field_id}/field")
            expect(status == 200, f"field endpoint: {status} {field}")
            for key in ("nx", "ny", "nz", "origin", "cellSize", "bandHz", "unit", "values"):
                expect(key in field, f"field key {key}")
            expect(field["nx"] == 6 and field["ny"] == 4 and field["nz"] == 5,
                   f"field dims: {field['nx'], field['ny'], field['nz']}")
            expect(len(field["values"]) == 6 * 4 * 5, f"field values: {len(field['values'])}")
            expect(field["unit"] == "energy", f"field unit: {field['unit']}")
            expect(any(v > 0 for v in field["values"]), "field has nonzero values")
            expect(all(v == v and abs(v) != float("inf") for v in field["values"]),
                   "field values finite")
            print("ok: field artifact + endpoint")

            # Jobs without a fieldGrid request expose no field artifact.
            status, body = http("GET", f"{base}/v1/jobs/{mesh_id}/field")
            expect(status == 404 and body.get("status") == 404,
                   f"field 404 when absent: {status} {body}")
            print("ok: field 404 when absent")
        else:
            print("note: box-scenario.json/box.obj fixtures missing; mesh smoke skipped")

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

        # Cancelled (or late-succeeded) jobs are terminal — history keeps them.
        status, runs = http("GET", f"{base}/v1/runs")
        expect(status == 200, f"runs after cancel: {status} {runs}")
        heavy_runs = [r for r in runs if r.get("jobId") == heavy_id]
        expect(len(heavy_runs) == 1, f"heavy run recorded: {runs}")
        expect(heavy_runs[0]["state"] in ("cancelled", "succeeded"),
               f"heavy run state: {heavy_runs[0]}")
        print("ok: terminal non-success run in history")

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
