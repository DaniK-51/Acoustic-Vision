<script setup lang="ts">
import { computed, onBeforeUnmount, onMounted, ref, watch } from 'vue';
import type { IrWave } from '../api/wav';

// Raw-canvas IR chart (no chart lib): amplitude vs time, optional dB view.
// Samples come from parseWavFloat32 — same float32 ir.wav the download serves.

const props = defineProps<{
  wave: IrWave | null;
  error: string;
  loading: boolean;
}>();

const canvasRef = ref<HTMLCanvasElement | null>(null);
const logScale = ref(false);
const channel = ref(0);
let resizeObserver: ResizeObserver | null = null;

const channelCount = computed(() => props.wave?.channels ?? 0);

function channelLabel(i: number): string {
  return `ch ${i + 1}`;
}

function draw(): void {
  const canvas = canvasRef.value;
  const wave = props.wave;
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

  // Frame + zero line even when empty, so the slot does not look broken.
  ctx.strokeStyle = '#2a3342';
  ctx.lineWidth = 1;
  ctx.strokeRect(0.5, 0.5, width - 1, height - 1);

  if (!wave || wave.frames === 0) {
    return;
  }

  const ch = Math.min(channel.value, wave.channels - 1);
  const frames = wave.frames;
  const stride = wave.channels;

  // Peak per displayed channel: dB view is relative, linear is scaled to peak.
  let peak = 0;
  for (let i = 0; i < frames; i++) {
    const v = Math.abs(wave.samples[i * stride + ch]);
    if (v > peak) {
      peak = v;
    }
  }
  const refAmp = peak > 0 ? peak : 1;

  const padL = 36;
  const padR = 8;
  const padT = 8;
  const padB = 18;
  const plotW = width - padL - padR;
  const plotH = height - padT - padB;

  // Axes furniture.
  ctx.fillStyle = '#8b96a8';
  ctx.font = '10px system-ui, sans-serif';
  ctx.textAlign = 'right';
  ctx.textBaseline = 'middle';
  const durationSec = frames / wave.sampleRate;

  if (logScale.value) {
    // dB relative to peak: 0 … -80 dB mapped top → bottom.
    const dbFloor = -80;
    for (let db = 0; db >= dbFloor; db -= 20) {
      const y = padT + ((0 - db) / (0 - dbFloor)) * plotH;
      ctx.strokeStyle = '#2a3342';
      ctx.beginPath();
      ctx.moveTo(padL, y);
      ctx.lineTo(padL + plotW, y);
      ctx.stroke();
      ctx.fillText(`${db}`, padL - 4, y);
    }
    ctx.save();
    ctx.beginPath();
    ctx.rect(padL, padT, plotW, plotH);
    ctx.clip();
    ctx.strokeStyle = '#4f8cff';
    ctx.lineWidth = 1;
    ctx.beginPath();
    for (let x = 0; x < plotW; x++) {
      // Min/max envelope per pixel column keeps spikes visible when decimated.
      const i0 = Math.floor((x / plotW) * frames);
      const i1 = Math.max(i0 + 1, Math.floor(((x + 1) / plotW) * frames));
      let maxDb = dbFloor;
      for (let i = i0; i < i1 && i < frames; i++) {
        const a = Math.abs(wave.samples[i * stride + ch]) / refAmp;
        const db = a > 0 ? Math.max(dbFloor, 20 * Math.log10(a)) : dbFloor;
        if (db > maxDb) {
          maxDb = db;
        }
      }
      const y = padT + ((0 - maxDb) / (0 - dbFloor)) * plotH;
      if (x === 0) {
        ctx.moveTo(padL + x, y);
      } else {
        ctx.lineTo(padL + x, y);
      }
    }
    ctx.stroke();
    ctx.restore();
  } else {
    // Linear amplitude around zero.
    const mid = padT + plotH / 2;
    ctx.strokeStyle = '#2a3342';
    ctx.beginPath();
    ctx.moveTo(padL, mid);
    ctx.lineTo(padL + plotW, mid);
    ctx.stroke();
    ctx.fillText('0', padL - 4, mid);
    ctx.fillText(peak.toExponential(1), padL - 4, padT + 4);
    ctx.fillText(`-${peak.toExponential(1)}`, padL - 4, padT + plotH - 4);

    ctx.save();
    ctx.beginPath();
    ctx.rect(padL, padT, plotW, plotH);
    ctx.clip();
    ctx.strokeStyle = '#4f8cff';
    ctx.lineWidth = 1;
    ctx.beginPath();
    for (let x = 0; x < plotW; x++) {
      const i0 = Math.floor((x / plotW) * frames);
      const i1 = Math.max(i0 + 1, Math.floor(((x + 1) / plotW) * frames));
      let vmin = 0;
      let vmax = 0;
      for (let i = i0; i < i1 && i < frames; i++) {
        const v = wave.samples[i * stride + ch];
        if (v < vmin) {
          vmin = v;
        }
        if (v > vmax) {
          vmax = v;
        }
      }
      const y0 = mid - (vmax / refAmp) * (plotH / 2);
      const y1 = mid - (vmin / refAmp) * (plotH / 2);
      ctx.moveTo(padL + x, y0);
      ctx.lineTo(padL + x, y1);
    }
    ctx.stroke();
    ctx.restore();
  }

  // Time axis labels.
  ctx.textAlign = 'center';
  ctx.textBaseline = 'top';
  const ticks = 4;
  for (let t = 0; t <= ticks; t++) {
    const sec = (durationSec * t) / ticks;
    const x = padL + (plotW * t) / ticks;
    ctx.fillText(`${sec.toFixed(2)}s`, x, padT + plotH + 4);
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

watch([() => props.wave, logScale, channel], () => draw());
</script>

<template>
  <div class="ir-chart">
    <div class="chart-head">
      <label class="log-toggle">
        <input v-model="logScale" type="checkbox" />
        dB (rel. peak)
      </label>
      <label v-if="channelCount > 1">
        channel
        <select v-model.number="channel">
          <option v-for="i in channelCount" :key="i - 1" :value="i - 1">
            {{ channelLabel(i - 1) }}
          </option>
        </select>
      </label>
      <span v-if="wave" class="dim">
        {{ wave.sampleRate }} Hz · {{ wave.frames }} samples
      </span>
    </div>
    <p v-if="loading" class="dim">Loading IR…</p>
    <p v-else-if="error" class="error-text">{{ error }}</p>
    <p v-else-if="!wave" class="dim">No IR samples.</p>
    <canvas v-show="wave && !error" ref="canvasRef" class="chart-canvas"></canvas>
  </div>
</template>

<style scoped>
.ir-chart {
  display: flex;
  flex-direction: column;
  gap: 6px;
}

.chart-head {
  display: flex;
  align-items: center;
  gap: 12px;
  font-size: 11px;
}

.chart-head label {
  display: flex;
  align-items: center;
  gap: 4px;
}

.chart-head select {
  background: var(--bg-inset);
  color: inherit;
  border: 1px solid var(--border);
  border-radius: 4px;
  font: inherit;
  padding: 1px 4px;
}

.chart-canvas {
  width: 100%;
  height: 160px;
  background: var(--bg-inset);
  border: 1px solid var(--border);
  border-radius: 4px;
}
</style>
