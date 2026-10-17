<script setup lang="ts">
import { computed, onBeforeUnmount, onMounted, ref, watch } from 'vue';
import { api, ApiError } from './api/client';
import type {
  EditorEntity,
  EditorMeshSettings,
  EditorMeshStatus,
  EditorScene,
  EngineDescriptor,
  HealthResponse,
  JobFieldSummary,
  JobRays,
  JobResult,
  MeshQuality,
  RunDetail,
  RunSummary,
  Scenario,
  SourceCatalog,
  SourceCatalogEntry,
} from './api/types';
import { parseWavFloat32, type IrWave } from './api/wav';
import { EventsSocket, type WsStatus } from './api/ws';
import { apiBaseUrl } from './config';
import {
  addEntity,
  createDefaultEditorScene,
  createEmptyEditorScene,
  findEntity,
  makeSolidBox,
  makeVoid,
  makeWall,
  makeSolidExtrude,
  removeEntity,
  replaceEntity,
} from './editor-scene';
import { formValuesFrom } from './params-schema';
import type { ActiveJob, WorkflowStage } from './ui-types';
import CatalogPanel from './components/CatalogPanel.vue';
import EnginePanel from './components/EnginePanel.vue';
import EngineParamsForm from './components/EngineParamsForm.vue';
import EntityInspector from './components/EntityInspector.vue';
import EntityTree from './components/EntityTree.vue';
import LoadScenarioPanel from './components/LoadScenarioPanel.vue';
import MaterialsPanel from './components/MaterialsPanel.vue';
import MeshPanel from './components/MeshPanel.vue';
import RunControls from './components/RunControls.vue';
import RunDetailPanel from './components/RunDetail.vue';
import RunsPanel from './components/RunsPanel.vue';
import SceneOutlinePanel from './components/SceneOutlinePanel.vue';
import SourceInspector from './components/SourceInspector.vue';
import StageBar from './components/StageBar.vue';
import StatusBar from './components/StatusBar.vue';
import Viewport, { type EntityRef } from './components/Viewport.vue';

// --- shared state (GUI1 slice: read-only views + scenario load) ---
const engines = ref<EngineDescriptor[]>([]);
const enginesError = ref('');
const enginesLoading = ref(false);
const selectedEngineId = ref('');

const catalog = ref<SourceCatalog | null>(null);
const catalogError = ref('');
const catalogLoading = ref(false);

const scenario = ref<Scenario | null>(null);
const sceneId = ref('');
const sceneVersion = ref(0);
const loadError = ref('');
const loadLoading = ref(false);

const runs = ref<RunSummary[]>([]);
const runsError = ref('');
const runsLoading = ref(false);

const health = ref<HealthResponse | null>(null);
const healthError = ref('');
const wsStatus = ref<WsStatus>('connecting');

const activeJob = ref<ActiveJob | null>(null);

// --- D2: workflow stage (Scene → Objects/Sources → Engines → Run → Results) ---
const stage = ref<WorkflowStage>('scene');

// --- GUI2: compute + run detail ---
const methodParams = ref<Record<string, unknown>>({});
const submitting = ref(false);
const submitError = ref('');
const currentJobId = ref('');

const runDetailOpen = ref(false);
const runDetail = ref<RunDetail | null>(null);
const runResult = ref<JobResult | null>(null);
const runDetailError = ref('');
const runResultError = ref('');
const runDetailLoading = ref(false);

// --- GUI3: field heat-map + IR waveform ---
const fieldData = ref<JobFieldSummary | null>(null);
const irWave = ref<IrWave | null>(null);
const irWaveError = ref('');
const irWaveLoading = ref(false);

// --- GUI4: ray path polylines (from the same result JSON as metrics) ---
const raysData = ref<JobRays | null>(null);

// --- V0.2 room editor (Scene document, /v1/scenes) ---
const editorScene = ref<EditorScene | null>(null);
const editorSceneId = ref('');
const editorSceneVersion = ref(0);
const editorSelection = ref<string | null>(null);
const editorBusy = ref(false);
const editorError = ref('');
const meshStatus = ref<EditorMeshStatus | null>(null);
const meshGenerating = ref(false);
const meshError = ref('');
const toScenarioSubmitting = ref(false);
const toScenarioError = ref('');
let meshPollTimer: ReturnType<typeof setTimeout> | null = null;

const selectedEngine = computed<EngineDescriptor | undefined>(() =>
  engines.value.find((e) => e.id === selectedEngineId.value),
);

function errText(err: unknown): string {
  if (err instanceof ApiError) {
    return err.detail ? `${err.title}: ${err.detail}` : err.title;
  }
  return err instanceof Error ? err.message : String(err);
}

const emptyMethodParams: Record<string, unknown> = {};

function storedMethodParams(): Record<string, unknown> {
  // Stable reference when absent — a fresh {} here would retrigger the form's
  // schema/initial watcher on every render and loop methodParams updates.
  return scenario.value?.params?.methodParams ?? emptyMethodParams;
}

function resetMethodParams(): void {
  methodParams.value = formValuesFrom(
    selectedEngine.value?.paramsSchema,
    storedMethodParams(),
  );
}

// Re-init the form when the selected engine or the loaded scene changes.
watch(
  () => [selectedEngineId.value, scenario.value] as const,
  () => {
    resetMethodParams();
  },
  { deep: true },
);

// --- REST loaders ---
async function loadEngines(): Promise<void> {
  enginesLoading.value = true;
  enginesError.value = '';
  try {
    engines.value = await api.engines();
    if (
      !selectedEngineId.value ||
      !engines.value.some((e) => e.id === selectedEngineId.value)
    ) {
      selectedEngineId.value = engines.value[0]?.id ?? '';
    }
    resetMethodParams();
  } catch (err) {
    enginesError.value = errText(err);
  } finally {
    enginesLoading.value = false;
  }
}

async function loadCatalog(): Promise<void> {
  catalogLoading.value = true;
  catalogError.value = '';
  try {
    catalog.value = await api.sourceCatalog();
  } catch (err) {
    catalogError.value = errText(err);
  } finally {
    catalogLoading.value = false;
  }
}

async function refreshRuns(): Promise<void> {
  runsLoading.value = true;
  runsError.value = '';
  try {
    runs.value = await api.listRuns();
  } catch (err) {
    runsError.value = errText(err);
  } finally {
    runsLoading.value = false;
  }
}

async function refreshHealth(): Promise<void> {
  try {
    health.value = await api.health();
    healthError.value = '';
  } catch (err) {
    health.value = null;
    healthError.value = errText(err);
  }
}

async function onScenarioSubmit(
  payload: Scenario | Record<string, unknown>,
  opts: { keepSelection?: boolean } = {},
): Promise<void> {
  loadLoading.value = true;
  loadError.value = '';
  try {
    const res = await api.createScenario(payload);
    sceneId.value = res.sceneId;
    sceneVersion.value = res.sceneVersion;
    scenario.value = payload as Scenario;
    // Orientation edits re-store the snapshot and must not clear the selection.
    if (!opts.keepSelection) {
      selection.value = null;
      // New scene → old heat-map / rays no longer match the geometry.
      // Orientation-only re-stores keep them: the geometry is unchanged.
      fieldData.value = null;
      raysData.value = null;
    }
    patchError.value = '';
    pendingOps = [];
    if (patchTimer !== null) {
      clearTimeout(patchTimer);
      patchTimer = null;
    }
    socket?.subscribeScene(res.sceneId);
    resetMethodParams();
  } catch (err) {
    sceneId.value = '';
    sceneVersion.value = 0;
    loadError.value = errText(err);
  } finally {
    loadLoading.value = false;
  }
}

// --- compute ---
const jobRunning = computed(() => {
  const state = activeJob.value?.state;
  return state === 'queued' || state === 'running';
});

/** D1: catalog lives in the scene zone and only on the Scene stage. */
const showCatalog = computed(() => stage.value === 'scene');

/** D3: insert allowed only at the Scene stage, with a loaded scenario. */
const canCatalogInsert = computed(
  () => stage.value === 'scene' && Boolean(scenario.value) && !jobRunning.value,
);

const catalogInsertHint = computed(() => {
  if (stage.value !== 'scene') {
    return 'Вставка источников — только на этапе Scene';
  }
  if (!scenario.value) {
    return 'Load a scenario first';
  }
  if (jobRunning.value) {
    return 'Unavailable while a job is running';
  }
  return '';
});

async function onRun(): Promise<void> {
  if (!sceneId.value || !scenario.value || submitting.value) {
    return;
  }
  // Land any debounced moves first — the run reads the server snapshot.
  flushPatch();
  submitting.value = true;
  submitError.value = '';
  // Heat-map and rays belong to a finished run; drop the previous ones now.
  fieldData.value = null;
  raysData.value = null;
  try {
    // SimParams overlay on the stored snapshot: run keeps sceneVersion in
    // its digest while engine/methodParams come from the form.
    const res = await api.submitSimulationForScene(sceneId.value, {
      engine: selectedEngineId.value || undefined,
      methodParams: methodParams.value,
    });
    currentJobId.value = res.jobId;
    activeJob.value = {
      jobId: res.jobId,
      state: 'queued',
      fraction: 0,
      stage: 'queued',
    };
    // D2: flow follows the pipeline — a submit belongs to the Run stage.
    stage.value = 'run';
  } catch (err) {
    submitError.value = errText(err);
  } finally {
    submitting.value = false;
  }
}

// --- interactive move (scene.patch) ---
const selection = ref<EntityRef | null>(null);
const patchError = ref('');
let patchTimer: ReturnType<typeof setTimeout> | null = null;
let pendingOps: Array<Record<string, unknown>> = [];

function applyMove(ref: EntityRef, position: [number, number, number]): void {
  const sc = scenario.value;
  if (!sc || ref.entityType === 'face') {
    return;
  }
  const list = ref.entityType === 'source' ? sc.sources : sc.receivers;
  const entity = (list ?? []).find((e) => e.id === ref.id);
  if (entity) {
    entity.position = position;
  }
}

/** Drop optimistic state and take the server snapshot again (scene.error). */
async function resyncScenario(): Promise<void> {
  if (!sceneId.value) {
    return;
  }
  try {
    const loaded = await api.getScenario(sceneId.value);
    scenario.value = loaded.scenario;
    sceneVersion.value = loaded.sceneVersion;
  } catch {
    // Keep the optimistic state; the error text already tells the user.
  }
}

function flushPatch(): void {
  patchTimer = null;
  if (pendingOps.length === 0) {
    return;
  }
  const ops = pendingOps;
  pendingOps = [];
  if (sceneId.value && socket) {
    socket.scenePatch(sceneId.value, ops);
  }
}

function onEntityMove(ref: EntityRef, position: [number, number, number]): void {
  if (!sceneId.value) {
    return;
  }
  applyMove(ref, position);
  // Coalesce: only the latest position per entity rides in the next patch.
  const key = `${ref.entityType}:${ref.id}`;
  const op = {
    op: 'move',
    entityType: ref.entityType,
    id: ref.id,
    position: [position[0], position[1], position[2]],
  };
  const existing = pendingOps.findIndex(
    (o) => `${o.entityType}:${o.id}` === key,
  );
  if (existing >= 0) {
    pendingOps[existing] = op;
  } else {
    pendingOps.push(op);
  }
  // Trailing debounce: one scene.patch per pause in the drag, not per pixel.
  if (patchTimer !== null) {
    clearTimeout(patchTimer);
  }
  patchTimer = setTimeout(flushPatch, 150);
}

function onSelect(ref: EntityRef | null): void {
  selection.value = ref;
}

/** Face id of the current selection (A2/A3 share one selection channel). */
const selectedFace = computed(() =>
  selection.value?.entityType === 'face' ? selection.value.id : null,
);

/** B1: source-select panel (orientation + polar) follows the viewport pick. */
const selectedSourceId = computed(() =>
  selection.value?.entityType === 'source' ? selection.value.id : '',
);

/**
 * B1: yaw/pitch edit. scene.patch has no rotate op in v1, so orientation is
 * persisted like catalog insert — a fresh scenario snapshot — and the
 * selection is kept so the inspector stays on the edited source.
 */
function onSourceOrientation(
  sourceId: string,
  yawDeg: number,
  pitchDeg: number,
): void {
  const sc = scenario.value;
  if (!sc || jobRunning.value) {
    return;
  }
  const sources = (sc.sources ?? []).map((s) =>
    s.id === sourceId
      ? { ...s, orientation: { yawDeg, pitchDeg } }
      : s,
  );
  void onScenarioSubmit({ ...sc, sources }, { keepSelection: true });
}

/** MaterialsPanel row click → same face selection as a viewport click. */
function onSelectFace(face: string | null): void {
  selection.value = face ? { entityType: 'face', id: face } : null;
}

async function onCancel(): Promise<void> {
  const jobId = currentJobId.value || activeJob.value?.jobId || '';
  if (!jobId) {
    return;
  }
  try {
    const status = await api.cancelJob(jobId);
    activeJob.value = {
      jobId,
      state: status.state,
      fraction: activeJob.value?.fraction ?? 0,
      stage: status.state,
      error: status.error || undefined,
    };
    void refreshRuns();
  } catch (err) {
    submitError.value = errText(err);
  }
}

// --- run detail ---
/** Heat-map payload for the viewport; refreshed whenever a run is opened. */
async function loadFieldForJob(jobId: string): Promise<void> {
  try {
    fieldData.value = await api.jobField(jobId);
  } catch {
    // 404 "field not available" is normal for engines without the artifact.
    fieldData.value = null;
  }
}

async function loadIrWaveForJob(jobId: string): Promise<void> {
  irWave.value = null;
  irWaveError.value = '';
  irWaveLoading.value = true;
  try {
    irWave.value = parseWavFloat32(await api.fetchIrWavBuffer(jobId));
  } catch (err) {
    irWaveError.value = errText(err);
  } finally {
    irWaveLoading.value = false;
  }
}

async function openRunDetail(run: RunSummary): Promise<void> {
  stage.value = 'results';
  runDetailOpen.value = true;
  runDetailLoading.value = true;
  runDetailError.value = '';
  runResultError.value = '';
  runResult.value = null;
  // Field/IR/rays follow the opened run; cleared first so stale data never shows.
  fieldData.value = null;
  raysData.value = null;
  irWave.value = null;
  irWaveError.value = '';
  try {
    runDetail.value = await api.getRun(run.id);
  } catch (err) {
    runDetail.value = null;
    runDetailError.value = errText(err);
  } finally {
    runDetailLoading.value = false;
  }
  try {
    runResult.value = await api.jobResult(run.jobId);
    runResultError.value = '';
    raysData.value = runResult.value?.rays ?? null;
  } catch (err) {
    runResult.value = null;
    runResultError.value = errText(err);
  }
  void loadFieldForJob(run.jobId);
  if (runResult.value?.ir) {
    void loadIrWaveForJob(run.jobId);
  }
}

async function refreshRunDetail(): Promise<void> {
  const detail = runDetail.value;
  if (!detail) {
    return;
  }
  await openRunDetail(detail);
}

async function onDownloadIr(jobId: string): Promise<void> {
  try {
    await api.downloadIrWav(jobId, `ir-${jobId}.wav`);
  } catch (err) {
    runResultError.value = errText(err);
  }
}

// --- catalog insert ---
/** Center of a rect room; mesh rooms fall back to a small default box. */
function roomCenter(sc: Scenario): [number, number, number] {
  const size = sc.room?.type === 'rect' && sc.room.size ? sc.room.size : null;
  if (size) {
    return [size.w / 2, size.h / 2, size.d / 2];
  }
  return [1, 1, 1];
}

function nextSourceId(sc: Scenario, entry: SourceCatalogEntry): string {
  const base = `src-${entry.id}`;
  const taken = new Set((sc.sources ?? []).map((s) => s.id));
  if (!taken.has(base)) {
    return base;
  }
  let n = 2;
  while (taken.has(`${base}-${n}`)) {
    n++;
  }
  return `${base}-${n}`;
}

/**
 * Insert a catalog source at the room center (nudged off existing sources
 * so markers stay clickable) and re-store the scenario — scene.patch has no
 * add-source op, so the insert path is a fresh POST /v1/scenarios snapshot.
 */
function onCatalogInsert(entry: SourceCatalogEntry): void {
  const sc = scenario.value;
  if (!sc) {
    return;
  }
  const pos = roomCenter(sc);
  const n = (sc.sources ?? []).length;
  // Small plan-view offset per index keeps stacked inserts distinguishable.
  const offset = 0.25 * n;
  const source = {
    id: nextSourceId(sc, entry),
    position: [pos[0] + offset, pos[1], pos[2] + offset] as [number, number, number],
    powerDb: entry.powerDb,
    // B2: carry the card's pattern so the source polar survives save/load.
    directivity: entry.directivity,
  };
  const next: Scenario = {
    ...sc,
    sources: [...(sc.sources ?? []), source],
  };
  void onScenarioSubmit(next);
}

// --- V0.2 room editor handlers ---
function clearMeshPoll(): void {
  if (meshPollTimer !== null) {
    clearTimeout(meshPollTimer);
    meshPollTimer = null;
  }
}

function onEditorSelect(id: string | null): void {
  editorSelection.value = id;
}

function onEditorAdd(kind: 'wall' | 'solid_box' | 'void' | 'solid_extrude'): void {
  const sc = editorScene.value;
  if (!sc) {
    return;
  }
  let entity: EditorEntity;
  switch (kind) {
    case 'wall':
      entity = makeWall(sc);
      break;
    case 'solid_box':
      entity = makeSolidBox(sc);
      break;
    case 'void':
      entity = makeVoid(sc);
      break;
    case 'solid_extrude':
      entity = makeSolidExtrude(sc);
      break;
  }
  editorScene.value = addEntity(sc, entity);
  editorSelection.value = entity.id;
  // A local edit invalidates the server mesh/report of the previous document.
  meshStatus.value = null;
}

function onEditorDelete(id: string): void {
  const sc = editorScene.value;
  if (!sc) {
    return;
  }
  editorScene.value = removeEntity(sc, id);
  if (editorSelection.value === id) {
    editorSelection.value = null;
  }
  meshStatus.value = null;
}

function onEditorUpdateEntity(entity: EditorEntity): void {
  const sc = editorScene.value;
  if (!sc) {
    return;
  }
  editorScene.value = replaceEntity(sc, entity);
  meshStatus.value = null;
}

function onEditorUpdateMeshSettings(settings: EditorMeshSettings): void {
  const sc = editorScene.value;
  if (!sc) {
    return;
  }
  editorScene.value = { ...sc, meshSettings: settings };
}

function onEditorUpdateQuality(quality: MeshQuality): void {
  const sc = editorScene.value;
  if (!sc) {
    return;
  }
  const base = sc.meshSettings ?? {};
  onEditorUpdateMeshSettings({
    ...base,
    surface: { ...(base.surface ?? {}), quality },
  });
}

function onEditorNewScene(): void {
  clearMeshPoll();
  editorScene.value = createDefaultEditorScene();
  editorSceneId.value = '';
  editorSceneVersion.value = 0;
  editorSelection.value = null;
  editorError.value = '';
  meshStatus.value = null;
  meshError.value = '';
}

function onEditorEmptyScene(): void {
  clearMeshPoll();
  editorScene.value = createEmptyEditorScene();
  editorSceneId.value = '';
  editorSceneVersion.value = 0;
  editorSelection.value = null;
  editorError.value = '';
  meshStatus.value = null;
  meshError.value = '';
}

async function onEditorSave(): Promise<void> {
  const sc = editorScene.value;
  if (!sc || editorBusy.value) {
    return;
  }
  editorBusy.value = true;
  editorError.value = '';
  try {
    if (editorSceneId.value) {
      const res = await api.putEditorScene(editorSceneId.value, sc);
      editorSceneId.value = res.sceneId;
      editorSceneVersion.value = res.sceneVersion;
      // PUT drops any previously generated mesh/report server-side.
      meshStatus.value = null;
    } else {
      const res = await api.createEditorScene(sc);
      editorSceneId.value = res.sceneId;
      editorSceneVersion.value = res.sceneVersion;
    }
  } catch (err) {
    editorError.value = errText(err);
  } finally {
    editorBusy.value = false;
  }
}

async function onEditorLoad(sceneId: string): Promise<void> {
  if (editorBusy.value) {
    return;
  }
  editorBusy.value = true;
  editorError.value = '';
  clearMeshPoll();
  try {
    const loaded = await api.getEditorScene(sceneId);
    editorScene.value = loaded.scene;
    editorSceneId.value = loaded.sceneId;
    editorSceneVersion.value = loaded.sceneVersion;
    editorSelection.value = null;
    meshStatus.value = null;
    meshError.value = '';
    // Best-effort: show the current mesh state if one was generated earlier.
    try {
      meshStatus.value = await api.getEditorMesh(loaded.sceneId);
    } catch {
      meshStatus.value = null;
    }
  } catch (err) {
    editorError.value = errText(err);
  } finally {
    editorBusy.value = false;
  }
}

function pollMeshStatus(): void {
  clearMeshPoll();
  const id = editorSceneId.value;
  if (!id) {
    return;
  }
  meshPollTimer = setTimeout(async () => {
    try {
      const status = await api.getEditorMesh(id);
      if (editorSceneId.value !== id) {
        return;
      }
      meshStatus.value = status;
      if (status.state === 'queued' || status.state === 'running') {
        pollMeshStatus();
      }
    } catch (err) {
      if (editorSceneId.value === id) {
        meshError.value = errText(err);
        meshGenerating.value = false;
      }
    }
  }, 400);
}

async function onEditorGenerateMesh(): Promise<void> {
  if (!editorScene.value || meshGenerating.value) {
    return;
  }
  // Persist local edits first (POST when there is no server id yet) so the
  // mesher always sees the current document.
  await onEditorSave();
  if (editorError.value || !editorSceneId.value) {
    return;
  }
  meshGenerating.value = true;
  meshError.value = '';
  try {
    const status = await api.generateEditorMesh(editorSceneId.value);
    meshStatus.value = status;
    if (status.state === 'queued' || status.state === 'running') {
      pollMeshStatus();
    }
  } catch (err) {
    meshError.value = errText(err);
  } finally {
    meshGenerating.value = false;
  }
}

async function onEditorDownloadObj(): Promise<void> {
  const id = editorSceneId.value;
  if (!id) {
    return;
  }
  try {
    await api.downloadEditorMeshObj(id, `mesh-${id}.obj`);
  } catch (err) {
    meshError.value = errText(err);
  }
}

/**
 * «В расчёт»: compile the editor Scene → Scenario (to-scenario), store it via
 * the existing POST /v1/scenarios path, then submit a simulation with the
 * current engine/methodParams — the normal Run pipeline.
 */
async function onEditorToScenario(): Promise<void> {
  if (!editorScene.value || toScenarioSubmitting.value || jobRunning.value) {
    return;
  }
  // Mesh generation and to-scenario both read the stored document — persist first.
  await onEditorSave();
  if (editorError.value || !editorSceneId.value) {
    return;
  }
  toScenarioSubmitting.value = true;
  toScenarioError.value = '';
  try {
    const compiled = await api.editorToScenario(editorSceneId.value);
    await onScenarioSubmit(compiled);
    if (loadError.value) {
      toScenarioError.value = loadError.value;
      return;
    }
    await onRun();
    if (submitError.value) {
      toScenarioError.value = submitError.value;
    } else {
      stage.value = 'run';
    }
  } catch (err) {
    toScenarioError.value = errText(err);
  } finally {
    toScenarioSubmitting.value = false;
  }
}

const selectedEditorEntity = computed(() =>
  findEntity(editorScene.value, editorSelection.value),
);

const canEditorGenerate = computed(() => Boolean(editorScene.value));
const canEditorSubmit = computed(
  () => Boolean(editorScene.value) && !jobRunning.value,
);

// --- WS job fan-out → status bar ---
let socket: EventsSocket | null = null;
let healthTimer: ReturnType<typeof setInterval> | null = null;

async function onJobTerminal(jobId: string, state: ActiveJob['state']): Promise<void> {
  await refreshRuns();
  // D2: finished work lands on Results (opens detail below when it is ours).
  if (stage.value === 'run') {
    stage.value = 'results';
  }
  // Open the finished run automatically when it is the one we submitted.
  if (jobId && jobId === currentJobId.value && state === 'succeeded') {
    const match = runs.value.find((run) => run.jobId === jobId);
    if (match) {
      await openRunDetail(match);
    }
  }
}

function onWsEvent(event: Record<string, unknown>): void {
  const kind = String(event.event ?? '');
  const jobId = String(event.jobId ?? '');
  if (kind === 'job.progress') {
    activeJob.value = {
      jobId,
      state: 'running',
      fraction: Number(event.fraction ?? 0),
      stage: String(event.stage ?? ''),
    };
  } else if (kind === 'job.succeeded') {
    activeJob.value = { jobId, state: 'succeeded', fraction: 1, stage: 'done' };
    void onJobTerminal(jobId, 'succeeded');
  } else if (kind === 'job.failed') {
    activeJob.value = {
      jobId,
      state: 'failed',
      fraction: 0,
      stage: 'failed',
      error: String(event.error ?? ''),
    };
    void onJobTerminal(jobId, 'failed');
  } else if (kind === 'job.cancelled') {
    activeJob.value = { jobId, state: 'cancelled', fraction: 0, stage: 'cancelled' };
    void onJobTerminal(jobId, 'cancelled');
  } else if (kind === 'scene.patched') {
    if (String(event.sceneId ?? '') === sceneId.value) {
      const version = Number(event.sceneVersion ?? 0);
      if (Number.isFinite(version) && version > 0) {
        sceneVersion.value = version;
      }
      patchError.value = '';
    }
  } else if (kind === 'scene.error') {
    if (!event.sceneId || String(event.sceneId) === sceneId.value) {
      patchError.value = String(event.detail ?? 'scene.patch failed');
      // Drop optimistic positions the server rejected.
      void resyncScenario();
    }
  }
}

onMounted(() => {
  void loadEngines();
  void loadCatalog();
  void refreshRuns();
  void refreshHealth();
  healthTimer = setInterval(() => void refreshHealth(), 15_000);

  socket = new EventsSocket({
    onStatus: (s) => {
      wsStatus.value = s;
    },
    onEvent: onWsEvent,
  });
  socket.subscribeJobs('*');
  socket.connect();
});

onBeforeUnmount(() => {
  if (healthTimer !== null) {
    clearInterval(healthTimer);
    healthTimer = null;
  }
  clearMeshPoll();
  socket?.close();
  socket = null;
});
</script>

<template>
  <div class="shell">
    <header class="topbar">
      <span class="title">Acoustic-Vision</span>
      <span class="dim mono">{{ apiBaseUrl }}</span>
    </header>

    <StageBar :stage="stage" @update:stage="stage = $event" />

    <div class="workspace">
      <aside class="col left">
        <!-- V0.2 room editor: entity tree + scene document I/O -->
        <section v-if="stage === 'editor'" class="stage-section" data-active="true">
          <header class="stage-label">
            <span class="stage-no">E</span> Room editor
          </header>
          <EntityTree
            :scene="editorScene"
            :selected-id="editorSelection"
            :scene-id="editorSceneId"
            :scene-version="editorSceneVersion"
            :saving="editorBusy"
            :loading="editorBusy"
            :error="editorError"
            @select="onEditorSelect"
            @add="onEditorAdd"
            @delete="onEditorDelete"
            @save="onEditorSave"
            @load="onEditorLoad"
            @new-scene="onEditorNewScene"
            @clear-error="editorError = ''"
          />
          <div class="editor-extra">
            <button class="ghost" :disabled="editorBusy" @click="onEditorEmptyScene">
              Empty scene
            </button>
            <button class="ghost" :disabled="editorBusy" @click="onEditorNewScene">
              Default shoebox
            </button>
          </div>
        </section>

        <template v-else>
          <section class="stage-section" :data-active="stage === 'scene'">
            <header class="stage-label">
              <span class="stage-no">1</span> Scene
            </header>
            <LoadScenarioPanel
              :loading="loadLoading"
              :error="loadError"
              :scene-id="sceneId"
              :scene-version="sceneVersion"
              @submit="onScenarioSubmit"
            />
            <!-- D1: catalog is scene-stage only, never in the compute zone -->
            <CatalogPanel
              v-if="showCatalog"
              :catalog="catalog"
              :loading="catalogLoading"
              :error="catalogError"
              :can-insert="canCatalogInsert"
              :insert-hint="catalogInsertHint"
              @refresh="loadCatalog"
              @insert="onCatalogInsert"
            />
          </section>

          <section class="stage-section" :data-active="stage === 'objects'">
            <header class="stage-label">
              <span class="stage-no">2</span> Objects / Sources
            </header>
            <SceneOutlinePanel
              :scenario="scenario"
              :scene-id="sceneId"
              :scene-version="sceneVersion"
            />
            <SourceInspector
              v-if="selectedSourceId"
              :scenario="scenario"
              :source-id="selectedSourceId"
              :catalog-entries="catalog?.sources ?? []"
              :disabled="jobRunning"
              @orientation="onSourceOrientation"
            />
            <MaterialsPanel
              :scenario="scenario"
              :selected-face="selectedFace"
              @select-face="onSelectFace"
            />
          </section>
        </template>
      </aside>

      <main class="col center">
        <Viewport
          :scenario="stage === 'editor' ? null : scenario"
          :selection="selection"
          :field="fieldData"
          :rays="raysData"
          :editor-scene="stage === 'editor' ? editorScene : null"
          :editor-selection="editorSelection"
          @select="onSelect"
          @move="onEntityMove"
          @select-editor="onEditorSelect"
        />
        <p v-if="patchError" class="error-text patch-error">{{ patchError }}</p>
      </main>

      <aside class="col right">
        <!-- V0.2 room editor: inspector + mesh panel -->
        <template v-if="stage === 'editor'">
          <section class="stage-section" data-active="true">
            <header class="stage-label">
              <span class="stage-no">E</span> Inspector
            </header>
            <EntityInspector
              :entity="selectedEditorEntity"
              :materials="editorScene?.materials ?? []"
              :mesh-settings="editorScene?.meshSettings ?? null"
              :disabled="editorBusy"
              @update-entity="onEditorUpdateEntity"
              @update-mesh-settings="onEditorUpdateMeshSettings"
            />
          </section>
          <section class="stage-section" data-active="true">
            <header class="stage-label">
              <span class="stage-no">E</span> Mesh / Compute
            </header>
            <MeshPanel
              :scene-id="editorSceneId"
              :mesh-settings="editorScene?.meshSettings ?? null"
              :mesh-status="meshStatus"
              :generating="meshGenerating"
              :error="meshError || toScenarioError"
              :can-generate="canEditorGenerate"
              :can-submit="canEditorSubmit"
              :submitting="toScenarioSubmitting"
              @update-quality="onEditorUpdateQuality"
              @generate="onEditorGenerateMesh"
              @download-obj="onEditorDownloadObj"
              @to-scenario="onEditorToScenario"
            />
          </section>
        </template>

        <template v-else>
          <section class="stage-section" :data-active="stage === 'engines'">
            <header class="stage-label">
              <span class="stage-no">3</span> Engines
            </header>
            <EnginePanel
              v-model="selectedEngineId"
              :engines="engines"
              :loading="enginesLoading"
              :error="enginesError"
              @refresh="loadEngines"
            />
            <EngineParamsForm
              :schema="selectedEngine?.paramsSchema"
              :initial="storedMethodParams()"
              :disabled="submitting || jobRunning"
              v-model="methodParams"
            />
          </section>

          <section class="stage-section" :data-active="stage === 'run'">
            <header class="stage-label">
              <span class="stage-no">4</span> Run
            </header>
            <RunControls
              :has-scene="Boolean(sceneId && scenario)"
              :running="submitting || jobRunning"
              :can-cancel="jobRunning"
              :engine-id="selectedEngineId"
              :error="submitError"
              @run="onRun"
              @cancel="onCancel"
            />
          </section>

          <section class="stage-section" :data-active="stage === 'results'">
            <header class="stage-label">
              <span class="stage-no">5</span> Results
            </header>
            <RunsPanel
              :runs="runs"
              :loading="runsLoading"
              :error="runsError"
              :selected-run-id="runDetail?.id ?? ''"
              @refresh="refreshRuns"
              @select="openRunDetail"
            />
          </section>
        </template>
      </aside>
    </div>

    <RunDetailPanel
      :open="runDetailOpen"
      :detail="runDetail"
      :result="runResult"
      :result-error="runResultError || runDetailError"
      :loading="runDetailLoading"
      :ir-wave="irWave"
      :ir-wave-error="irWaveError"
      :ir-wave-loading="irWaveLoading"
      @close="runDetailOpen = false"
      @refresh="refreshRunDetail"
      @download-ir="onDownloadIr"
    />

    <StatusBar
      :api-base="apiBaseUrl"
      :health="health"
      :health-error="healthError"
      :ws-status="wsStatus"
      :job="activeJob"
    />
  </div>
</template>

<style scoped>
.shell {
  display: flex;
  flex-direction: column;
  height: 100%;
}

.topbar {
  display: flex;
  align-items: baseline;
  gap: 16px;
  padding: 8px 14px;
  border-bottom: 1px solid var(--border);
  background: var(--bg-panel);
}

.title {
  font-size: 15px;
  font-weight: 600;
}

.workspace {
  flex: 1;
  min-height: 0;
  display: grid;
  grid-template-columns: 300px 1fr 360px;
  gap: 8px;
  padding: 8px;
}

.col {
  min-height: 0;
  display: flex;
  flex-direction: column;
  gap: 8px;
  overflow: auto;
}

.stage-section {
  display: flex;
  flex-direction: column;
  gap: 8px;
  padding-left: 8px;
  border-left: 2px solid transparent;
}

.stage-section[data-active='true'] {
  border-left-color: var(--accent);
}

.stage-label {
  display: flex;
  align-items: center;
  gap: 6px;
  color: var(--text-dim);
  font-size: 11px;
  font-weight: 600;
  text-transform: uppercase;
  letter-spacing: 0.04em;
}

.stage-no {
  display: inline-flex;
  align-items: center;
  justify-content: center;
  width: 16px;
  height: 16px;
  border-radius: 50%;
  background: rgba(79, 140, 255, 0.18);
  color: var(--accent);
  font-size: 10px;
}

.center {
  overflow: hidden;
}

.editor-extra {
  display: flex;
  gap: 6px;
}

.editor-extra .ghost {
  background: transparent;
  font-size: 11px;
}
</style>
