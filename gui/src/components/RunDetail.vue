<script setup lang="ts">
import { computed } from 'vue';
import type { EarlyReflection, JobResult, Rt60Entry, RunDetail } from '../api/types';
import type { IrWave } from '../api/wav';
import IrChart from './IrChart.vue';

const props = defineProps<{
  open: boolean;
  detail: RunDetail | null;
  result: JobResult | null;
  resultError: string;
  loading: boolean;
  irWave: IrWave | null;
  irWaveError: string;
  irWaveLoading: boolean;
}>();

const emit = defineEmits<{ close: []; downloadIr: [jobId: string]; refresh: [] }>();

function fmt(value: unknown, digits = 3): string {
  if (typeof value !== 'number' || !Number.isFinite(value)) {
    return '—';
  }
  return value.toFixed(digits);
}

function bandLabel(key: string): string {
  return key === '0' ? 'broad' : key;
}

function sortBands<T>(entries: Array<[string, T]>): Array<[string, T]> {
  return [...entries].sort((a, b) => Number(a[0]) - Number(b[0]));
}

const rt60Rows = computed(() =>
  sortBands<Rt60Entry>(Object.entries(props.result?.metrics.rt60 ?? {})),
);
const edtRows = computed(() =>
  sortBands<number>(Object.entries(props.result?.metrics.edt ?? {})),
);
const early = computed<EarlyReflection[]>(
  () => props.result?.metrics.earlyReflections ?? [],
);
const ir = computed(() => props.result?.ir ?? null);
const field = computed(() => props.result?.field ?? null);
const rays = computed(() => props.result?.rays ?? null);

const earlyPreview = computed(() => early.value.slice(0, 8));

function whenText(iso: string): string {
  if (!iso) {
    return '—';
  }
  const d = new Date(iso);
  return Number.isNaN(d.getTime()) ? iso : d.toLocaleString();
}
</script>

<template>
  <div v-if="open" class="overlay" @click.self="emit('close')">
    <section class="modal" role="dialog" aria-label="Run detail">
      <header class="modal-head">
        <h2>Run detail</h2>
        <div class="head-actions">
          <button :disabled="loading" @click="emit('refresh')">Refresh</button>
          <button class="ghost" @click="emit('close')">Close</button>
        </div>
      </header>

      <p v-if="loading" class="dim">Loading…</p>
      <template v-else-if="detail">
        <table class="meta">
          <tbody>
            <tr>
              <th>runId</th>
              <td class="mono">{{ detail.id }}</td>
              <th>jobId</th>
              <td class="mono">{{ detail.jobId }}</td>
            </tr>
            <tr>
              <th>engine</th>
              <td>{{ detail.engine }}</td>
              <th>state</th>
              <td>
                <span class="state" :data-state="detail.state">{{ detail.state }}</span>
              </td>
            </tr>
            <tr>
              <th>scenario</th>
              <td :title="detail.scenarioName">{{ detail.scenarioName }}</td>
              <th>finished</th>
              <td class="dim">{{ whenText(detail.finishedAt) }}</td>
            </tr>
          </tbody>
        </table>

        <p v-if="resultError" class="error-text">{{ resultError }}</p>

        <template v-if="result">
          <h3>RT60 <span class="dim">(s)</span></h3>
          <table v-if="rt60Rows.length">
            <thead>
              <tr>
                <th>band Hz</th>
                <th>schroederT30</th>
                <th>sabine</th>
                <th>eyring</th>
              </tr>
            </thead>
            <tbody>
              <tr v-for="[band, entry] in rt60Rows" :key="band">
                <td class="mono">{{ bandLabel(band) }}</td>
                <td>{{ fmt(entry.schroederT30) }}</td>
                <td>{{ fmt(entry.sabine) }}</td>
                <td>{{ fmt(entry.eyring) }}</td>
              </tr>
            </tbody>
          </table>
          <p v-else class="dim">No RT60 bands in this result.</p>

          <h3>EDT <span class="dim">(s)</span></h3>
          <table v-if="edtRows.length">
            <thead>
              <tr>
                <th>band Hz</th>
                <th>edt</th>
              </tr>
            </thead>
            <tbody>
              <tr v-for="[band, value] in edtRows" :key="band">
                <td class="mono">{{ bandLabel(band) }}</td>
                <td>{{ fmt(value) }}</td>
              </tr>
            </tbody>
          </table>
          <p v-else class="dim">No EDT bands in this result.</p>

          <h3>
            Early reflections
            <span class="dim">({{ early.length }})</span>
          </h3>
          <table v-if="earlyPreview.length">
            <thead>
              <tr>
                <th>t s</th>
                <th>amp</th>
                <th>order</th>
              </tr>
            </thead>
            <tbody>
              <tr v-for="(item, i) in earlyPreview" :key="i">
                <td>{{ fmt(item.t) }}</td>
                <td>{{ fmt(item.amp) }}</td>
                <td>{{ item.order }}</td>
              </tr>
            </tbody>
          </table>
          <p v-else class="dim">None.</p>
          <p v-if="early.length > earlyPreview.length" class="dim hint">
            …and {{ early.length - earlyPreview.length }} more.
          </p>

          <h3>IR waveform</h3>
          <IrChart
            :wave="irWave"
            :error="irWaveError"
            :loading="irWaveLoading"
          />

          <h3>Artifacts</h3>
          <table class="artifacts">
            <tbody>
              <tr>
                <th>ir.wav</th>
                <td v-if="ir">
                  {{ ir.sampleRate }} Hz · {{ fmt(ir.durationSec, 2) }} s ·
                  {{ ir.channels }} ch
                  <button class="linkish" @click="emit('downloadIr', detail.jobId)">
                    Download
                  </button>
                </td>
                <td v-else class="dim">absent</td>
              </tr>
              <tr>
                <th>field</th>
                <td v-if="field">
                  {{ field.nx }}×{{ field.ny }}×{{ field.nz }}
                  <span v-if="field.unit" class="dim">· {{ field.unit }}</span>
                  <span v-if="field.bandHz !== undefined" class="dim">
                    · band {{ field.bandHz }} Hz
                  </span>
                  <span class="dim">· heat-map in viewport</span>
                </td>
                <td v-else class="dim">absent</td>
              </tr>
              <tr>
                <th>rays</th>
                <td v-if="rays && rays.paths.length > 0">
                  {{ rays.paths.length }} paths
                  <span class="dim">· toggle Rays in viewport</span>
                </td>
                <td v-else-if="rays" class="dim">empty (capturePaths off?)</td>
                <td v-else class="dim">absent</td>
              </tr>
            </tbody>
          </table>
        </template>
      </template>
      <p v-else class="dim">No run selected.</p>
    </section>
  </div>
</template>

<style scoped>
.overlay {
  position: fixed;
  inset: 0;
  background: rgba(0, 0, 0, 0.45);
  display: flex;
  align-items: center;
  justify-content: center;
  z-index: 20;
  padding: 24px;
}

.modal {
  background: var(--bg-panel);
  border: 1px solid var(--border);
  border-radius: 8px;
  width: min(640px, 100%);
  max-height: 100%;
  overflow: auto;
  padding: 14px 16px;
}

.modal-head {
  display: flex;
  align-items: center;
  justify-content: space-between;
  margin-bottom: 10px;
}

.head-actions {
  display: flex;
  gap: 8px;
}

h2 {
  margin: 0;
  font-size: 14px;
  font-weight: 600;
}

h3 {
  margin: 14px 0 6px;
  font-size: 12px;
  font-weight: 600;
}

.meta th {
  width: 70px;
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

.artifacts th {
  width: 70px;
}

.linkish {
  margin-left: 8px;
  padding: 2px 8px;
  font-size: 11px;
}

.ghost {
  background: transparent;
}

.hint {
  margin: 4px 0 0;
  font-size: 11px;
}
</style>
