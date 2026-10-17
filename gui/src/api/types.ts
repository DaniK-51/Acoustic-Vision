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

/** gainDb[i] belongs to freqsHz[i]; each row is sampled at anglesDeg. */
export interface PolarCurve {
  freqsHz: number[];
  anglesDeg: number[];
  gainDb: number[][];
}

export interface Directivity {
  type: string;
  description?: string;
  horizontal?: PolarCurve;
  vertical?: PolarCurve;
}

export type SourceCatalogDirectivity = Directivity;

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

/** yawDeg: 0 faces +Z, positive turns toward +X. pitchDeg: positive looks up. */
export interface SourceOrientation {
  yawDeg?: number;
  pitchDeg?: number;
}

export interface ScenarioSource {
  id: string;
  position: [number, number, number];
  powerDb: number;
  orientation?: SourceOrientation;
  directivity?: Directivity;
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

export interface Rt60Entry {
  schroederT30: number;
  sabine: number;
  eyring: number;
}

export interface EarlyReflection {
  t: number;
  amp: number;
  order: number;
}

export interface JobMetrics {
  rt60: Record<string, Rt60Entry>;
  edt: Record<string, number>;
  earlyReflections: EarlyReflection[];
}

export interface JobIrSummary {
  sampleRate: number;
  durationSec: number;
  channels: number;
  channelNames?: string[];
  bandsHz?: number[];
}

export interface JobFieldSummary {
  nx: number;
  ny: number;
  nz: number;
  origin?: number[];
  cellSize?: number;
  bandHz?: number;
  unit?: string;
  values?: number[];
}

/** One captured ray path (result.rays.paths[]): source → wall hits. */
export interface RayPolyline {
  points: number[][];
  amplitude: number;
  order: number;
}

/** resultToJson "rays" object (present only for engines with the rays artifact). */
export interface JobRays {
  paths: RayPolyline[];
}

/** GET /v1/jobs/{jobId}/result (resultToJson). */
export interface JobResult {
  metrics: JobMetrics;
  ir?: JobIrSummary;
  field?: JobFieldSummary;
  rays?: JobRays;
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

// --- Room-editor Scene (V0.2, schemas/scene.schema.json) ---
// Parallel document to Scenario; separate id space under /v1/scenes.

export type Vec3 = [number, number, number];
export type Vec2 = [number, number];

export interface EditorMaterial {
  id: string;
  name?: string;
  bandsHz?: number[];
  absorption: number[];
  scattering?: number;
}

export type WallOpeningType = 'door' | 'window';

export interface WallOpening {
  type: WallOpeningType;
  /** Meters along the wall baseline from `start`. */
  offset: number;
  width: number;
  height: number;
  /** Height of the opening bottom above the wall base; default 0. */
  sill?: number;
}

export interface WallFaces {
  inner: string;
  outer: string;
  side: string;
}

export interface WallEntity {
  type: 'wall';
  id: string;
  name?: string;
  start: Vec3;
  end: Vec3;
  height: number;
  thickness: number;
  faces: WallFaces;
  openings?: WallOpening[];
}

export interface SolidBoxEntity {
  type: 'solid_box';
  id: string;
  name?: string;
  origin: Vec3;
  size: Vec3;
  materialId: string;
}

export interface SolidExtrudeEntity {
  type: 'solid_extrude';
  id: string;
  name?: string;
  /** 2D polygon in XY, extruded along +Z by `height`. */
  polygon: Vec2[];
  height: number;
  materialId: string;
}

export interface VoidEntity {
  type: 'void';
  id: string;
  name?: string;
  origin: Vec3;
  size: Vec3;
}

export type EditorGeometryEntity =
  | WallEntity
  | SolidBoxEntity
  | SolidExtrudeEntity
  | VoidEntity;

export interface EditorSourceRef {
  type?: 'source';
  id: string;
  name?: string;
  position: Vec3;
  powerDb?: number;
}

export interface EditorReceiverRef {
  type?: 'receiver';
  id: string;
  name?: string;
  position: Vec3;
}

export type EditorEntity =
  | EditorGeometryEntity
  | EditorSourceRef
  | EditorReceiverRef;

export type SurfaceAlgorithm = 'auto' | 'earclip' | 'advancing-front';
export type MeshQuality = 'draft' | 'standard' | 'fine';
export type VolumeAlgorithm = 'tet' | 'hex-box';

export interface MeshSettingsSurface {
  algorithm?: SurfaceAlgorithm;
  targetEdge?: number;
  maxEdge?: number;
  quality?: MeshQuality;
}

export interface MeshSettingsVolume {
  algorithm?: VolumeAlgorithm;
  targetSize?: number;
  enabled?: boolean;
}

export interface EditorMeshSettings {
  surface?: MeshSettingsSurface;
  volume?: MeshSettingsVolume;
}

/** Scene document (schemas/scene.schema.json). Units are meters only. */
export interface EditorScene {
  schemaVersion: number;
  id?: string;
  units: 'm';
  materials: EditorMaterial[];
  entities: EditorEntity[];
  sources?: EditorSourceRef[];
  receivers?: EditorReceiverRef[];
  meshSettings?: EditorMeshSettings;
}

export interface CreateEditorSceneResponse {
  sceneId: string;
  sceneVersion: number;
}

export interface GetEditorSceneResponse extends CreateEditorSceneResponse {
  scene: EditorScene;
}

export interface MeshReportStats {
  vertices: number;
  triangles: number;
  zones: string[];
}

/** mesh-report.json / MeshReport wire shape (contracts.md §5). */
export interface MeshReport {
  kind: string;
  stats: MeshReportStats;
  bbox: number[][];
  warnings: string[];
  settingsUsed: Record<string, unknown>;
  entityTriangleCount: Record<string, number>;
}

export interface EditorMeshStatus {
  sceneId: string;
  sceneVersion: number;
  jobId: string;
  state: 'queued' | 'running' | 'succeeded' | 'failed' | 'cancelled' | string;
  error?: string;
  report?: MeshReport;
}
