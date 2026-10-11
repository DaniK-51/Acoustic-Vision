import { apiBaseUrl } from '../config';
import type {
  CreateSceneResponse,
  EngineDescriptor,
  HealthResponse,
  JobResult,
  JobStatus,
  ProblemBody,
  RunDetail,
  RunSummary,
  Scenario,
  SourceCatalog,
  SubmitSimulationResponse,
} from './types';

export class ApiError extends Error {
  readonly status: number;
  readonly title: string;
  readonly detail: string;

  constructor(status: number, title: string, detail: string) {
    super(detail || title);
    this.name = 'ApiError';
    this.status = status;
    this.title = title;
    this.detail = detail;
  }
}

async function parseBody(res: Response): Promise<unknown> {
  const text = await res.text();
  if (!text) {
    return null;
  }
  try {
    return JSON.parse(text);
  } catch {
    return text;
  }
}

function toApiError(status: number, body: unknown): ApiError {
  const problem = body as Partial<ProblemBody>;
  if (problem && typeof problem === 'object' && 'title' in problem) {
    return new ApiError(
      status,
      String(problem.title ?? 'error'),
      String(problem.detail ?? ''),
    );
  }
  return new ApiError(status, `HTTP ${status}`, typeof body === 'string' ? body : '');
}

/**
 * text/plain avoids a CORS preflight (av-api has no OPTIONS handler and sends
 * no CORS headers; Electron main injects the response headers). Body is still
 * JSON — Crow parses req.body regardless of Content-Type.
 */
const JSON_BODY_AS_TEXT = 'text/plain; charset=utf-8';

async function request<T>(
  method: string,
  path: string,
  body?: unknown,
): Promise<T> {
  const res = await fetch(apiBaseUrl.replace(/\/+$/, '') + path, {
    method,
    headers: body === undefined ? {} : { 'Content-Type': JSON_BODY_AS_TEXT },
    body: body === undefined ? undefined : JSON.stringify(body),
  });
  const parsed = await parseBody(res);
  if (!res.ok) {
    throw toApiError(res.status, parsed);
  }
  return parsed as T;
}

export const api = {
  health: () => request<HealthResponse>('GET', '/health'),

  engines: () => request<EngineDescriptor[]>('GET', '/engines'),

  sourceCatalog: () => request<SourceCatalog>('GET', '/source-catalog'),

  listScenarios: () => request<unknown[]>('GET', '/scenarios'),

  createScenario: (scenario: Scenario | Record<string, unknown>) =>
    request<CreateSceneResponse>('POST', '/scenarios', scenario),

  getScenario: (sceneId: string) =>
    request<import('./types').GetSceneResponse>(
      'GET',
      `/scenarios/${encodeURIComponent(sceneId)}`,
    ),

  submitSimulation: (body: { sceneId: string } | Scenario | Record<string, unknown>) =>
    request<SubmitSimulationResponse>('POST', '/simulations', body),

  /**
   * Submit a run for a stored scene via the SimParams overlay:
   * `{"sceneId", engine?, methodParams?, params?}` — the server merges the
   * overlay onto the stored snapshot, so the run keeps sceneVersion in its
   * digest. Full-scenario submit stays available via submitSimulation().
   */
  submitSimulationForScene: (
    sceneId: string,
    overrides: {
      engine?: string;
      methodParams?: Record<string, unknown>;
      params?: Record<string, unknown>;
    } = {},
  ) => {
    const body: Record<string, unknown> = { sceneId };
    if (overrides.engine !== undefined) {
      body.engine = overrides.engine;
    }
    if (overrides.methodParams !== undefined) {
      body.methodParams = overrides.methodParams;
    }
    if (overrides.params !== undefined) {
      body.params = overrides.params;
    }
    return request<SubmitSimulationResponse>('POST', '/simulations', body);
  },

  jobStatus: (jobId: string) =>
    request<JobStatus>('GET', `/jobs/${encodeURIComponent(jobId)}`),

  cancelJob: (jobId: string) =>
    request<JobStatus>('POST', `/jobs/${encodeURIComponent(jobId)}/cancel`),

  jobResult: (jobId: string) =>
    request<JobResult>('GET', `/jobs/${encodeURIComponent(jobId)}/result`),

  /** Full SpatialField grid (values linear energy, index x + nx*(y + ny*z)). */
  jobField: (jobId: string) =>
    request<import('./types').JobFieldSummary>(
      'GET',
      `/jobs/${encodeURIComponent(jobId)}/field`,
    ),

  irWavUrl: (jobId: string) =>
    `${apiBaseUrl.replace(/\/+$/, '')}/jobs/${encodeURIComponent(jobId)}/ir.wav`,

  /** Fetch ir.wav bytes for the client-side float32 WAV parser (see wav.ts). */
  fetchIrWavBuffer: async (jobId: string): Promise<ArrayBuffer> => {
    const res = await fetch(api.irWavUrl(jobId));
    if (!res.ok) {
      const parsed = await parseBody(res);
      throw toApiError(res.status, parsed);
    }
    return res.arrayBuffer();
  },

  /** Fetch ir.wav and hand it to the browser download path (sandbox-safe). */
  downloadIrWav: async (jobId: string, filename: string): Promise<void> => {
    const res = await fetch(api.irWavUrl(jobId));
    if (!res.ok) {
      const parsed = await parseBody(res);
      throw toApiError(res.status, parsed);
    }
    const blob = await res.blob();
    const url = URL.createObjectURL(blob);
    try {
      const a = document.createElement('a');
      a.href = url;
      a.download = filename;
      a.click();
    } finally {
      URL.revokeObjectURL(url);
    }
  },

  listRuns: (engine?: string) =>
    request<RunSummary[]>(
      'GET',
      engine ? `/runs?engine=${encodeURIComponent(engine)}` : '/runs',
    ),

  getRun: (id: string) =>
    request<RunDetail>('GET', `/runs/${encodeURIComponent(id)}`),
};
