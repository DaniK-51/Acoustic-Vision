<script setup lang="ts">
import { computed, ref, watch } from 'vue';
import type { Directivity, Scenario, SourceCatalogEntry } from '../api/types';
import PolarChart from './PolarChart.vue';

// B1/B3 source-select panel: yaw/pitch numbers, optional compass ring, polar
// chart of the source's directivity (source override, else catalog card).

const props = defineProps<{
  scenario: Scenario | null;
  /** Selected source id ('' = none). */
  sourceId: string;
  /** Catalog cards — used to show the card's polar when source has no override. */
  catalogEntries: SourceCatalogEntry[];
  disabled?: boolean;
}>();

const emit = defineEmits<{
  orientation: [sourceId: string, yawDeg: number, pitchDeg: number];
}>();

const source = computed(() =>
  props.scenario?.sources.find((s) => s.id === props.sourceId) ?? null,
);

const yawDeg = computed(() => source.value?.orientation?.yawDeg ?? 0);
const pitchDeg = computed(() => source.value?.orientation?.pitchDeg ?? 0);

// Editable mirrors so typing does not fight the prop stream.
const yawInput = ref(0);
const pitchInput = ref(0);
watch(
  [() => props.sourceId, yawDeg, pitchDeg],
  () => {
    yawInput.value = yawDeg.value;
    pitchInput.value = pitchDeg.value;
  },
  { immediate: true },
);

/** Catalog card matching the source (insert id scheme `src-<entry.id>…`). */
const catalogCard = computed(() => {
  const id = props.sourceId;
  return (
    props.catalogEntries.find(
      (e) => id === `src-${e.id}` || id.startsWith(`src-${e.id}-`),
    ) ?? null
  );
});

const effectiveDirectivity = computed<Directivity | null>(() => {
  if (source.value?.directivity) {
    return source.value.directivity;
  }
  return catalogCard.value?.directivity ?? null;
});

function emitOrientation(): void {
  const id = props.sourceId;
  if (!id || props.disabled) {
    return;
  }
  const yaw = Number(yawInput.value);
  const pitch = Number(pitchInput.value);
  if (!Number.isFinite(yaw) || !Number.isFinite(pitch)) {
    return;
  }
  emit('orientation', id, yaw, pitch);
}

// --- compass ring (optional yaw control): click/drag sets yaw ---
const ringRef = ref<SVGSVGElement | null>(null);

function yawFromPointer(event: PointerEvent): number | null {
  const svg = ringRef.value;
  if (!svg) {
    return null;
  }
  const rect = svg.getBoundingClientRect();
  const dx = event.clientX - (rect.left + rect.width / 2);
  const dy = event.clientY - (rect.top + rect.height / 2);
  if (dx === 0 && dy === 0) {
    return null;
  }
  // Screen: +X right, +Z up (top-down plan). yaw 0 = +Z (top), positive
  // toward +X (clockwise on screen).
  const deg = (Math.atan2(dx, -dy) * 180) / Math.PI;
  return ((deg % 360) + 360) % 360;
}

function onRingPointer(event: PointerEvent): void {
  if (props.disabled) {
    return;
  }
  const yaw = yawFromPointer(event);
  if (yaw === null) {
    return;
  }
  yawInput.value = Math.round(yaw * 10) / 10;
  emitOrientation();
}

function onRingDown(event: PointerEvent): void {
  (event.currentTarget as SVGSVGElement).setPointerCapture(event.pointerId);
  onRingPointer(event);
}

function onRingMove(event: PointerEvent): void {
  if (event.buttons & 1) {
    onRingPointer(event);
  }
}

/** Pointer tip in SVG coords for the current yaw (viewBox 0 0 100 100). */
const compassTip = computed(() => {
  const rad = (yawDeg.value * Math.PI) / 180;
  // 0° at top, clockwise toward +X.
  const x = 50 + 34 * Math.sin(rad);
  const y = 50 - 34 * Math.cos(rad);
  return { x, y };
});
</script>

<template>
  <section v-if="source" class="panel">
    <header class="panel-head">
      <h2>Source orientation</h2>
      <span class="dim mono">{{ source.id }}</span>
    </header>

    <div class="orient-row">
      <label>
        yaw
        <input
          v-model.number="yawInput"
          type="number"
          step="1"
          :disabled="disabled"
          @change="emitOrientation"
        />
        °
      </label>
      <label>
        pitch
        <input
          v-model.number="pitchInput"
          type="number"
          step="1"
          :disabled="disabled"
          @change="emitOrientation"
        />
        °
      </label>
      <svg
        ref="ringRef"
        class="compass"
        viewBox="0 0 100 100"
        role="img"
        aria-label="Compass ring (yaw)"
        @pointerdown="onRingDown"
        @pointermove="onRingMove"
      >
        <circle cx="50" cy="50" r="40" class="ring" />
        <line x1="50" y1="14" x2="50" y2="22" class="tick" />
        <line x1="50" y1="78" x2="50" y2="86" class="tick" />
        <line x1="14" y1="50" x2="22" y2="50" class="tick" />
        <line x1="78" y1="50" x2="86" y2="50" class="tick" />
        <text x="50" y="10" class="label" text-anchor="middle">+Z</text>
        <text x="94" y="53" class="label" text-anchor="middle">+X</text>
        <line
          x1="50"
          y1="50"
          :x2="compassTip.x"
          :y2="compassTip.y"
          class="needle"
        />
        <circle :cx="compassTip.x" :cy="compassTip.y" r="3.5" class="tip" />
      </svg>
    </div>

    <p class="dim hint">
      yaw 0° → +Z, positive toward +X · pitch positive → up
    </p>

    <PolarChart :directivity="effectiveDirectivity" />
    <p v-if="!effectiveDirectivity" class="dim hint">
      no directivity data — showing omni
    </p>
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
  gap: 8px;
}

.panel-head {
  display: flex;
  align-items: baseline;
  justify-content: space-between;
  gap: 8px;
}

h2 {
  margin: 0;
  font-size: 13px;
  font-weight: 600;
}

.orient-row {
  display: flex;
  align-items: center;
  gap: 10px;
  flex-wrap: wrap;
}

.orient-row label {
  display: inline-flex;
  align-items: center;
  gap: 4px;
  font-size: 11px;
}

.orient-row input {
  width: 64px;
  font-size: 11px;
  padding: 2px 4px;
}

.compass {
  width: 72px;
  height: 72px;
  cursor: crosshair;
  touch-action: none;
}

.ring {
  fill: var(--bg-inset);
  stroke: var(--border);
  stroke-width: 2;
}

.tick {
  stroke: var(--text-dim);
  stroke-width: 2;
}

.label {
  fill: var(--text-dim);
  font-size: 9px;
}

.needle {
  stroke: var(--accent);
  stroke-width: 2.5;
  stroke-linecap: round;
}

.tip {
  fill: var(--accent);
}

.hint {
  margin: 0;
  font-size: 10px;
}
</style>
