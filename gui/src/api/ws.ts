import { eventsUrl } from '../config';

export type WsStatus = 'connecting' | 'open' | 'closed';

export interface EventsSocketHandlers {
  onStatus?: (status: WsStatus) => void;
  onEvent?: (event: Record<string, unknown>) => void;
}

/**
 * Minimal /v1/events client: subscribe job fan-out and scene watchers,
 * auto-reconnect with fixed delay, resubscribe on open.
 */
export class EventsSocket {
  private ws: WebSocket | null = null;
  private closedByUser = false;
  private reconnectTimer: ReturnType<typeof setTimeout> | null = null;
  private jobIds = new Set<string>();
  private sceneIds = new Set<string>();

  constructor(private readonly handlers: EventsSocketHandlers = {}) {}

  connect(url: string = eventsUrl): void {
    this.closedByUser = false;
    this.open(url);
  }

  private open(url: string): void {
    this.setStatus('connecting');
    const ws = new WebSocket(url);
    this.ws = ws;

    ws.onopen = () => {
      this.setStatus('open');
      for (const jobId of this.jobIds) {
        this.sendRaw({ op: 'subscribe', jobId });
      }
      for (const sceneId of this.sceneIds) {
        this.sendRaw({ op: 'subscribeScene', sceneId });
      }
    };
    ws.onmessage = (msg) => {
      try {
        const data = JSON.parse(String(msg.data));
        if (data && typeof data === 'object') {
          this.handlers.onEvent?.(data as Record<string, unknown>);
        }
      } catch {
        // Malformed frames are ignored — REST stays authoritative.
      }
    };
    ws.onclose = () => {
      this.setStatus('closed');
      this.scheduleReconnect(url);
    };
    ws.onerror = () => {
      // onclose follows and drives reconnect.
    };
  }

  private scheduleReconnect(url: string): void {
    if (this.closedByUser || this.reconnectTimer !== null) {
      return;
    }
    this.reconnectTimer = setTimeout(() => {
      this.reconnectTimer = null;
      if (!this.closedByUser) {
        this.open(url);
      }
    }, 2000);
  }

  private setStatus(status: WsStatus): void {
    this.handlers.onStatus?.(status);
  }

  private sendRaw(payload: Record<string, unknown>): void {
    if (this.ws && this.ws.readyState === WebSocket.OPEN) {
      this.ws.send(JSON.stringify(payload));
    }
  }

  /** jobId may be '*'. */
  subscribeJobs(jobId = '*'): void {
    this.jobIds.add(jobId);
    this.sendRaw({ op: 'subscribe', jobId });
  }

  subscribeScene(sceneId: string): void {
    if (!sceneId) {
      return;
    }
    this.sceneIds.add(sceneId);
    this.sendRaw({ op: 'subscribeScene', sceneId });
  }

  /** Interactive move/power patch; ack is scene.patched / scene.error. */
  scenePatch(
    sceneId: string,
    ops: Array<Record<string, unknown>>,
  ): void {
    if (!sceneId || ops.length === 0) {
      return;
    }
    this.sendRaw({ op: 'scene.patch', sceneId, ops });
  }

  close(): void {
    this.closedByUser = true;
    if (this.reconnectTimer !== null) {
      clearTimeout(this.reconnectTimer);
      this.reconnectTimer = null;
    }
    this.ws?.close();
    this.ws = null;
    this.setStatus('closed');
  }
}
