<script setup lang="ts">
import type { RunSummary } from '../api/types';

defineProps<{
  runs: RunSummary[];
  loading: boolean;
  error: string;
  selectedRunId?: string;
}>();

const emit = defineEmits<{ refresh: []; select: [run: RunSummary] }>();

function shortId(id: string): string {
  return id.length > 14 ? `${id.slice(0, 12)}…` : id;
}

function whenText(iso: string): string {
  if (!iso) {
    return '—';
  }
  const d = new Date(iso);
  return Number.isNaN(d.getTime()) ? iso : d.toLocaleTimeString();
}
</script>

<template>
  <section class="panel">
    <header class="panel-head">
      <h2>Runs</h2>
      <button :disabled="loading" @click="emit('refresh')">Refresh</button>
    </header>

    <p v-if="error" class="error-text">{{ error }}</p>
    <p v-else-if="loading && runs.length === 0" class="dim">Loading…</p>

    <div class="scroll">
      <table>
        <thead>
          <tr>
            <th>runId</th>
            <th>engine</th>
            <th>scenario</th>
            <th>state</th>
            <th>finished</th>
          </tr>
        </thead>
        <tbody>
          <tr
            v-for="run in runs"
            :key="run.id"
            :title="run.id"
            class="run-row"
            :class="{ selected: run.id === selectedRunId }"
            @click="emit('select', run)"
          >
            <td class="mono">{{ shortId(run.id) }}</td>
            <td>{{ run.engine }}</td>
            <td :title="run.scenarioName">{{ run.scenarioName }}</td>
            <td>
              <span class="state" :data-state="run.state">{{ run.state }}</span>
            </td>
            <td class="dim">{{ whenText(run.finishedAt) }}</td>
          </tr>
          <tr v-if="runs.length === 0 && !loading">
            <td colspan="5" class="dim">No runs yet. History is append-on-terminal.</td>
          </tr>
        </tbody>
      </table>
    </div>
    <p class="dim hint">Click a run for metrics · runId ≠ jobId.</p>
  </section>
</template>

<style scoped>
.panel {
  background: var(--bg-panel);
  border: 1px solid var(--border);
  border-radius: 6px;
  padding: 10px 12px;
  display: flex;
  flex-direction: column;
  min-height: 140px;
  flex: 1;
}

.panel-head {
  display: flex;
  align-items: center;
  justify-content: space-between;
  margin-bottom: 8px;
}

h2 {
  margin: 0;
  font-size: 13px;
  font-weight: 600;
}

.scroll {
  overflow: auto;
  min-height: 0;
}

.run-row {
  cursor: pointer;
}

.run-row:hover {
  background: rgba(79, 140, 255, 0.08);
}

.run-row.selected {
  background: rgba(79, 140, 255, 0.16);
}

.state[data-state='succeeded'] {
  color: var(--ok);
}

.state[data-state='failed'] {
  color: var(--err);
}

.state[data-state='cancelled'] {
  color: var(--warn);
}

.hint {
  margin: 8px 0 0;
  font-size: 11px;
}
</style>
