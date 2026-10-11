// Wire shapes of av-api /v1 (ARCHITECTURE.md §3). Loose on purpose where the
// server may add fields — GUI1 only reads what the panels display.

export interface HealthResponse {
  status: string;
  version: string;
  engines: string[];
}

export interface EngineCaps {
  supportsRect: boolean;
  supportsMesh: boolean;
  supportsRayPaths: boolean;
  supportsBands: boolean;
  maxOrder: number;
}

export interface EngineDescriptor {
  id: string;
  name: string;
  version: string;
  caps: EngineCaps;
  paramsSchema: Record<string, unknown>;
  artifacts: string[];
}

export interface SourceCatalogSpectrum {
  bandsHz: number[];
  responseDb: number[];
}

export interface SourceCatalogDirectivity {
  type: string;
  description?: string;
}

export interface SourceCatalogEntry {
  id: string;
  name: string;
  manufacturer?: string;
  category?: string;
  powerDb: number;
  spectrum: SourceCatalogSpectrum;
  directivity: SourceCatalogDirectivity;
  notes?: string;
  sourceUrl?: string;
  disclaimer: string;
}

export interface SourceCatalog {
  disclaimer: string;
  sources: SourceCatalogEntry[];
}

export interface ScenePatchOp {
  op: string;
  [key: string]: unknown;
}

export interface ScenarioMaterial {
  id: string;
  name: string;
  bandsHz: number[];
  absorption: number[];
  scattering?: number;
}

export interface ScenarioRoom {
  type: string;
  size?: { w: number; h: number; d: number };
  faces?: Record<string, string>;
  mesh?: Record<string, unknown>;
}

export interface ScenarioSource {
  id: string;
  position: [number, number, number];
  powerDb: number;
}

export interface ScenarioReceiver {
  id: string;
  position: [number, number, number];
}

export interface ScenarioParams {
  engine: string;
  maxReflectionOrder?: number;
  sampleRate?: number;
  durationSec?: number;
  bandsHz?: number[];
  speedOfSound?: number;
  capturePaths?: boolean;
  airAbsorption?: boolean;
  signedIr?: boolean;
  fieldGrid?: { enabled: boolean; nx: number; ny: number; nz: number };
  methodParams?: Record<string, unknown>;
}

export interface Scenario {
  schemaVersion: number;
  name: string;
  room: ScenarioRoom;
  materials: ScenarioMaterial[];
  sources: ScenarioSource[];
  receivers: ScenarioReceiver[];
  params: ScenarioParams;
}

export interface CreateSceneResponse {
  sceneId: string;
  sceneVersion: number;
}

export interface GetSceneResponse extends CreateSceneResponse {
  scenario: Scenario;
}

export interface SubmitSimulationResponse {
  jobId: string;
}

export interface JobProgress {
  fraction: number;
  stage: string;
}

export interface JobStatus {
  id: string;
  state: 'queued' | 'running' | 'succeeded' | 'failed' | 'cancelled' | string;
  progress: JobProgress;
  error: string;
}

export interface RunSummary {
  id: string;
  jobId: string;
  engine: string;
  scenarioName: string;
  sceneVersion?: number;
  state: string;
  submittedAt: string;
  finishedAt: string;
  paramsDigest: Record<string, unknown>;
}

export interface RunDetail extends RunSummary {
  links: { result: string; ir: string; field: string };
}

export interface ProblemBody {
  type: string;
  title: string;
  status: number;
  detail: string;
}
