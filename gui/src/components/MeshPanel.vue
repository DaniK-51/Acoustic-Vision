<script setup lang="ts">
import { computed } from 'vue';
import type {
  EditorMeshSettings,
  EditorMeshStatus,
  MeshQuality,
} from '../api/types';

// Bottom-sheet style mesh generator panel (room-editor-mesh.md §5):
// quality preset → POST /v1/scenes/{id}/mesh, then report stats + warnings
// and a mesh.obj download. «Врасчёт» hands the compiled Scenario to the
// existing simulation submit path (to-scenario → POST /v1/simulations).
const props = defineProps<{
  sceneId: string;
  meshSettings: EditorMeshSettings | null;
  meshStatus: EditorMeshStatus | null;
  generating: boolean;
  error: string;
  canGenerate: boolean;
  canSubmit: boolean;
  submitting: boolean;
}>();

const emit = defineEmits<{
  updateQuality: [quality: MeshQuality];
  generate: [];
  downloadObj: [];
  toScenario: [];
}>();

const quality = computed<MeshQuality>(
  () => props.meshSettings?.surface?.quality ?? 'draft',
);

const report = computed(() => props.meshStatus?.report ?? null);

const state = computed(() => props.meshStatus?.state ?? '');

const busy = computed(
  () =>
    props.generating ||
    state.value === 'queued' ||
    state.value === 'running',
);
</script>

<template>
  <section class="panel">
    <header class="panel-head">
      <h2>Mesh</h2>
      <span v-if="meshStatus" class="dim mono">
        {{ meshStatus.jobId || '—' }}
        <template v-if="state"> · {{ state }}</template>
      </span>
    </header>

    <div class="row">
      <label class="quality">
        Quality
        <select
          :value="quality"
          :disabled="busy"
          @change="
            emit(
              'updateQuality',
              ($event.target as HTMLInputElement).value as MeshQuality,
            )
          "
        >
          <option value="draft">draft</option>
          <option value="standard">standard</option>
          <option value="fine">fine</option>
        </select>
      </label>
      <button
        class="primary"
        :disabled="!canGenerate || busy"
        :title="sceneId ? 'POST /v1/scenes/{id}/mesh' : 'Create / load a scene first'"
        @click="emit('generate')"
      >
        {{ busy ? 'Generating…' : 'Generate' }}
      </button>
    </div>

    <p v-if="error" class="error-text">{{ error }}</p>
    <p v-else-if="meshStatus?.error" class="error-text">{{ meshStatus.error }}</p>

    <template v-if="report">
      <div class="stats mono">
        <span>{{ report.stats.triangles }} tris</span>
        <span>{{ report.stats.vertices }} verts</span>
        <span>{{ report.stats.zones.length }} zones</span>
      </div>
      <div v-if="report.bbox?.length >= 2" class="dim mono bbox">
        bbox [{{ report.bbox[0]?.join(', ') }}] … [{{ report.bbox[1]?.join(', ') }}]
      </div>
      <div v-if="report.settingsUsed" class="dim mono settings">
        settings: {{ JSON.stringify(report.settingsUsed) }}
      </div>
      <div v-if="report.warnings?.length" class="warnings">
        <div class="warn-title">Warnings ({{ report.warnings.length }})</div>
        <ul>
          <li v-for="(w, i) in report.warnings" :key="i">{{ w }}</li>
        </ul>
      </div>
      <div class="row">
        <button :disabled="!sceneId" @click="emit('downloadObj')">
          Download mesh.obj
        </button>
      </div>
    </template>
    <p v-else-if="!busy" class="dim hint">
      No mesh yet. Generate writes the surface mesh and returns a report.
    </p>

    <div class="submit-row">
      <button
        class="primary"
        :disabled="!canSubmit || submitting"
        title="POST /v1/scenes/{id}/to-scenario → submit simulation"
        @click="emit('toScenario')"
      >
        {{ submitting ? 'Compiling…' : 'В расчёт' }}
      </button>
      <p class="dim hint">
        to-scenario → store snapshot → POST /v1/simulations (rect →
        image-source, mesh → ray-tracing).
      </p>
    </div>
  </section>
</template>

<style scoped>
.panel {
  background: var(--bg-panel);
  border: 1px solid var(--border);
  border-radius: 6px;
  padding: 10px 12px;
}

.panel-head {
  display: flex;
  align-items: baseline;
  justify-content: space-between;
  gap: 8px;
  margin-bottom: 8px;
}

h2 {
  margin: 0;
  font-size: 13px;
  font-weight: 600;
}

.row {
  display: flex;
  align-items: center;
  gap: 8px;
  margin-bottom: 8px;
}

.quality {
  display: flex;
  align-items: center;
  gap: 6px;
  font-size: 12px;
  color: var(--text-dim);
}

.quality select {
  min-width: 100px;
}

.primary {
  flex: 1;
  background: var(--accent);
  border-color: transparent;
  color: #fff;
}

.primary:hover:not(:disabled) {
  background: #6ba0ff;
}

.stats {
  display: flex;
  gap: 12px;
  font-size: 12px;
  margin-bottom: 4px;
}

.bbox,
.settings {
  font-size: 10px;
  word-break: break-all;
  margin-bottom: 4px;
}

.warnings {
  margin: 6px 0;
  max-height: 120px;
  overflow: auto;
}

.warn-title {
  color: var(--warn);
  font-size: 11px;
  font-weight: 600;
  margin-bottom: 2px;
}

.warnings ul {
  margin: 0;
  padding-left: 16px;
  font-size: 11px;
  color: var(--warn);
}

.submit-row {
  margin-top: 10px;
  padding-top: 8px;
  border-top: 1px solid var(--border);
}

.hint {
  margin: 4px 0 0;
  font-size: 11px;
}
</style>
