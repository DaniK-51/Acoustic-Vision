<script setup lang="ts">
import { computed, onBeforeUnmount, onMounted, ref, watch } from 'vue';
import { api, ApiError } from './api/client';
import type {
  EngineDescriptor,
  HealthResponse,
  JobResult,
  RunDetail,
  RunSummary,
  Scenario,
  SourceCatalog,
} from './api/types';
import { EventsSocket, type WsStatus } from './api/ws';
import { apiBaseUrl } from './config';
import { formValuesFrom } from './params-schema';
import type { ActiveJob } from './ui-types';
import CatalogPanel from './components/CatalogPanel.vue';
import EnginePanel from './components/EnginePanel.vue';
import EngineParamsForm from './components/EngineParamsForm.vue';
import LoadScenarioPanel from './components/LoadScenarioPanel.vue';
import RunControls from './components/RunControls.vue';
import RunDetailPanel from './components/RunDetail.vue';
import RunsPanel from './components/RunsPanel.vue';
import SceneOutlinePanel from './components/SceneOutlinePanel.vue';
import StatusBar from './components/StatusBar.vue';
import Viewport from './components/Viewport.vue';

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

async function onScenarioSubmit(payload: Scenario | Record<string, unknown>): Promise<void> {
  loadLoading.value = true;
  loadError.value = '';
  try {
    const res = await api.createScenario(payload);
    sceneId.value = res.sceneId;
    sceneVersion.value = res.sceneVersion;
    scenario.value = payload as Scenario;
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

async function onRun(): Promise<void> {
  if (!sceneId.value || !scenario.value || submitting.value) {
    return;
  }
  submitting.value = true;
  submitError.value = '';
  try {
    // Baseline = what a bare {sceneId} submit would run: schema defaults
    // overlaid with the snapshot's methodParams (engines default the rest).
    const baseline = formValuesFrom(
      selectedEngine.value?.paramsSchema,
      storedMethodParams(),
    );
    const res = await api.submitSimulationForScene(sceneId.value, scenario.value, {
      engine: selectedEngineId.value || undefined,
      methodParams: methodParams.value,
      baselineMethodParams: baseline,
    });
    currentJobId.value = res.jobId;
    activeJob.value = {
      jobId: res.jobId,
      state: 'queued',
      fraction: 0,
      stage: 'queued',
    };
  } catch (err) {
    submitError.value = errText(err);
  } finally {
    submitting.value = false;
  }
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
async function openRunDetail(run: RunSummary): Promise<void> {
  runDetailOpen.value = true;
  runDetailLoading.value = true;
  runDetailError.value = '';
  runResultError.value = '';
  runResult.value = null;
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
  } catch (err) {
    runResult.value = null;
    runResultError.value = errText(err);
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

// --- WS job fan-out → status bar ---
let socket: EventsSocket | null = null;
let healthTimer: ReturnType<typeof setInterval> | null = null;

async function onJobTerminal(jobId: string, state: ActiveJob['state']): Promise<void> {
  await refreshRuns();
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

    <div class="workspace">
      <aside class="col left">
        <LoadScenarioPanel
          :loading="loadLoading"
          :error="loadError"
          :scene-id="sceneId"
          :scene-version="sceneVersion"
          @submit="onScenarioSubmit"
        />
        <SceneOutlinePanel
          :scenario="scenario"
          :scene-id="sceneId"
          :scene-version="sceneVersion"
        />
      </aside>

      <main class="col center">
        <Viewport :scenario="scenario" />
      </main>

      <aside class="col right">
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
        <RunControls
          :has-scene="Boolean(sceneId && scenario)"
          :running="submitting || jobRunning"
          :can-cancel="jobRunning"
          :engine-id="selectedEngineId"
          :error="submitError"
          @run="onRun"
          @cancel="onCancel"
        />
        <CatalogPanel
          :catalog="catalog"
          :loading="catalogLoading"
          :error="catalogError"
          @refresh="loadCatalog"
        />
        <RunsPanel
          :runs="runs"
          :loading="runsLoading"
          :error="runsError"
          :selected-run-id="runDetail?.id ?? ''"
          @refresh="refreshRuns"
          @select="openRunDetail"
        />
      </aside>
    </div>

    <RunDetailPanel
      :open="runDetailOpen"
      :detail="runDetail"
      :result="runResult"
      :result-error="runResultError || runDetailError"
      :loading="runDetailLoading"
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

.center {
  overflow: hidden;
}
</style>
