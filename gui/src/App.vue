<script setup lang="ts">
import { onBeforeUnmount, onMounted, ref } from 'vue';
import { api, ApiError } from './api/client';
import type {
  EngineDescriptor,
  HealthResponse,
  RunSummary,
  Scenario,
  SourceCatalog,
} from './api/types';
import { EventsSocket, type WsStatus } from './api/ws';
import { apiBaseUrl } from './config';
import type { ActiveJob } from './ui-types';
import CatalogPanel from './components/CatalogPanel.vue';
import EnginePanel from './components/EnginePanel.vue';
import LoadScenarioPanel from './components/LoadScenarioPanel.vue';
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

function errText(err: unknown): string {
  if (err instanceof ApiError) {
    return err.detail ? `${err.title}: ${err.detail}` : err.title;
  }
  return err instanceof Error ? err.message : String(err);
}

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
  } catch (err) {
    sceneId.value = '';
    sceneVersion.value = 0;
    loadError.value = errText(err);
  } finally {
    loadLoading.value = false;
  }
}

// --- WS job fan-out → status bar ---
let socket: EventsSocket | null = null;
let healthTimer: ReturnType<typeof setInterval> | null = null;

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
    void refreshRuns();
  } else if (kind === 'job.failed') {
    activeJob.value = {
      jobId,
      state: 'failed',
      fraction: 0,
      stage: 'failed',
      error: String(event.error ?? ''),
    };
    void refreshRuns();
  } else if (kind === 'job.cancelled') {
    activeJob.value = { jobId, state: 'cancelled', fraction: 0, stage: 'cancelled' };
    void refreshRuns();
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
          @refresh="refreshRuns"
        />
      </aside>
    </div>

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
