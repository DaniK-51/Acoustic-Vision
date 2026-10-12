<script setup lang="ts">
import { computed, onBeforeUnmount, onMounted, ref, watch } from 'vue';
import type { Directivity } from '../api/types';
import { gainAtFreq, resolvePlaneCurve } from '../directivity';

// B3/B4: angle × dB chart, one curve per measured band; the bold curve is the
// picked frequency interpolated between the two nearest curves. Raw canvas —
// same approach as IrChart.vue (no chart lib).

const props = withDefaults(
  defineProps<{
    directivity: Directivity | null;
    /** Which polar plane to show when the source has both. */
    plane?: 'horizontal' | 'vertical';
    /** Picked frequency (Hz); parent may v-model it. */
    freqHz?: number;
    height?: number;
  }>(),
  { plane: 'horizontal', freqHz: 1000, height: 220 },
);

const emit = defineEmits<{ 'update:freqHz': [freqHz: number] }>();

const canvasRef = ref<HTMLCanvasElement | null>(null);
const internalPlane = ref<'horizontal' | 'vertical'>(props.plane);
const internalFreq = ref(props.freqHz);
let resizeObserver: ResizeObserver | null = null;

watch(
  () => props.plane,
  (p) => {
    internalPlane.value = p;
  },
);
watch(
  () => props.freqHz,
  (f) => {
    internalFreq.value = f;
  },
);

const curve = computed(() =>
  resolvePlaneCurve(props.directivity, internalPlane.value),
);

const hasBothPlanes = computed(() =>
  Boolean(props.directivity?.horizontal && props.directivity?.vertical),
);

const typeLabel = computed(() => {
  const t = props.directivity?.type;
  return t ? `type: ${t}` : 'omni (default)';
});

function onFreqInput(event: Event): void {
  const value = Number((event.target as HTMLInputElement).value);
  if (!Number.isFinite(value)) {
    return;
  }
  internalFreq.value = value;
  emit('update:freqHz', value);
}

function onPlaneChange(event: Event): void {
  internalPlane.value = (event.target as HTMLSelectElement).value as
    | 'horizontal'
    | 'vertical';
}

/** Curve color per band: cool→warm ramp over the measured frequencies. */
function bandColor(index: number, count: number, alpha = 1): string {
  const t = count <= 1 ? 0 : index / (count - 1);
  const r = Math.round(80 + 120 * t);
  const g = Math.round(140 - 40 * t);
  const b = Math.round(255 - 140 * t);
  return `rgba(${r}, ${g}, ${b}, ${alpha})`;
}

function draw(): void {
  const canvas = canvasRef.value;
  const data = curve.value;
  if (!canvas) {
    return;
  }
  const width = Math.max(canvas.clientWidth, 1);
  const height = Math.max(canvas.clientHeight, 1);
  const dpr = window.devicePixelRatio || 1;
  canvas.width = Math.round(width * dpr);
  canvas.height = Math.round(height * dpr);
  const ctx = canvas.getContext('2d');
  if (!ctx) {
    return;
  }
  ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
  ctx.clearRect(0, 0, width, height);
  ctx.strokeStyle = '#2a3342';
  ctx.lineWidth = 1;
  ctx.strokeRect(0.5, 0.5, width - 1, height - 1);

  if (!data) {
    ctx.fillStyle = '#8b96a8';
    ctx.font = '11px system-ui, sans-serif';
    ctx.textAlign = 'center';
    ctx.fillText('no polar curves for custom type', width / 2, height / 2);
    return;
  }

  const padL = 36;
  const padR = 10;
  const padT = 10;
  const padB = 22;
  const plotW = width - padL - padR;
  const plotH = height - padT - padB;
  if (plotW <= 0 || plotH <= 0) {
    return;
  }

  const angles = data.anglesDeg;
  const aMin = angles.length > 0 ? Math.min(...angles) : -180;
  const aMax = angles.length > 0 ? Math.max(...angles) : 180;

  // dB window: floor at -40 (preset null floor), top slightly above max gain.
  let gMax = 0;
  for (const row of data.gainDb) {
    for (const g of row) {
      if (g > gMax) gMax = g;
    }
  }
  const top = Math.max(gMax, 0) + 3;
  const bottom = -40;

  const xOf = (angle: number) => padL + ((angle - aMin) / (aMax - aMin || 1)) * plotW;
  const yOf = (gain: number) => padT + ((top - gain) / (top - bottom)) * plotH;

  // dB grid every 10 dB.
  ctx.font = '10px system-ui, sans-serif';
  ctx.textAlign = 'right';
  ctx.textBaseline = 'middle';
  for (let g = Math.ceil(bottom / 10) * 10; g <= top; g += 10) {
    const y = yOf(g);
    ctx.strokeStyle = g === 0 ? '#3a465a' : '#222b38';
    ctx.beginPath();
    ctx.moveTo(padL, y);
    ctx.lineTo(padL + plotW, y);
    ctx.stroke();
    ctx.fillStyle = '#8b96a8';
    ctx.fillText(`${g}`, padL - 4, y);
  }

  // Angle ticks: 0° (front), ±90°, ±180°.
  const angleTicks = [-180, -90, 0, 90, 180].filter(
    (a) => a >= aMin && a <= aMax,
  );
  ctx.textAlign = 'center';
  ctx.textBaseline = 'top';
  for (const a of angleTicks) {
    const x = xOf(a);
    ctx.strokeStyle = a === 0 ? '#3a465a' : '#222b38';
    ctx.beginPath();
    ctx.moveTo(x, padT);
    ctx.lineTo(x, padT + plotH);
    ctx.stroke();
    ctx.fillStyle = '#8b96a8';
    ctx.fillText(`${a}°`, x, padT + plotH + 4);
  }

  const drawRow = (row: number[], style: string, lineWidth: number) => {
    ctx.strokeStyle = style;
    ctx.lineWidth = lineWidth;
    ctx.beginPath();
    row.forEach((gain, i) => {
      const x = xOf(angles[i]);
      const y = yOf(gain);
      if (i === 0) ctx.moveTo(x, y);
      else ctx.lineTo(x, y);
    });
    ctx.stroke();
  };

  // One thin curve per measured band.
  const count = data.freqsHz.length;
  data.gainDb.forEach((row, i) => {
    drawRow(row, bandColor(i, count, 0.45), 1);
  });

  // Interpolated curve at the picked frequency (B4) — bold.
  const picked = gainAtFreq(data, internalFreq.value);
  if (picked.length > 0) {
    drawRow(picked, 'rgba(255, 210, 120, 0.95)', 2.2);
  }
}

onMounted(() => {
  resizeObserver = new ResizeObserver(() => draw());
  if (canvasRef.value) {
    resizeObserver.observe(canvasRef.value);
  }
  draw();
});

onBeforeUnmount(() => {
  resizeObserver?.disconnect();
  resizeObserver = null;
});

watch([curve, internalFreq, internalPlane], () => draw());
</script>

<template>
  <div class="polar-chart">
    <div class="chart-controls">
      <label>
        plane
        <select :value="internalPlane" @change="onPlaneChange">
          <option value="horizontal">horizontal</option>
          <option value="vertical" :disabled="!hasBothPlanes">vertical</option>
        </select>
      </label>
      <label>
        f
        <input
          :value="internalFreq"
          type="number"
          min="20"
          max="20000"
          step="1"
          @change="onFreqInput"
        />
        Hz
      </label>
      <span class="dim">{{ typeLabel }}</span>
    </div>
    <canvas
      ref="canvasRef"
      class="polar-canvas"
      :style="{ height: `${height}px` }"
    ></canvas>
    <div class="legend dim">
      thin — measured bands · bold — {{ Math.round(internalFreq) }} Hz
      (interpolated)
    </div>
  </div>
</template>

<style scoped>
.polar-chart {
  display: flex;
  flex-direction: column;
  gap: 4px;
}

.chart-controls {
  display: flex;
  flex-wrap: wrap;
  align-items: center;
  gap: 10px;
  font-size: 11px;
}

.chart-controls label {
  display: inline-flex;
  align-items: center;
  gap: 4px;
}

.chart-controls select,
.chart-controls input {
  font-size: 11px;
  padding: 1px 4px;
  width: 72px;
}

.polar-canvas {
  width: 100%;
  display: block;
  background: var(--bg-inset);
  border: 1px solid var(--border);
  border-radius: 4px;
}

.legend {
  font-size: 10px;
}
</style>
