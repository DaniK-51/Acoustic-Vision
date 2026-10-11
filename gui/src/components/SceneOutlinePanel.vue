<script setup lang="ts">
import { computed } from 'vue';
import type { Scenario } from '../api/types';

const props = defineProps<{
  scenario: Scenario | null;
  sceneId: string;
  sceneVersion: number;
}>();

const roomLine = computed(() => {
  const room = props.scenario?.room;
  if (!room) {
    return '—';
  }
  if (room.type === 'rect' && room.size) {
    const { w, h, d } = room.size;
    return `rect ${w} × ${h} × ${d} m`;
  }
  return room.type;
});

const paramLines = computed(() => {
  const p = props.scenario?.params;
  if (!p) {
    return [] as string[];
  }
  const lines = [`engine: ${p.engine}`];
  if (p.maxReflectionOrder !== undefined) {
    lines.push(`maxReflectionOrder: ${p.maxReflectionOrder}`);
  }
  if (p.sampleRate !== undefined) {
    lines.push(`sampleRate: ${p.sampleRate}`);
  }
  if (p.durationSec !== undefined) {
    lines.push(`durationSec: ${p.durationSec}`);
  }
  if (p.bandsHz && p.bandsHz.length > 0) {
    lines.push(`bandsHz: ${p.bandsHz.join(', ')}`);
  }
  return lines;
});

function fmtPos(pos: [number, number, number] | undefined): string {
  return pos ? `[${pos[0]}, ${pos[1]}, ${pos[2]}]` : '—';
}
</script>

<template>
  <section class="panel">
    <header class="panel-head">
      <h2>Scene outline</h2>
      <span v-if="sceneId" class="dim mono">
        {{ sceneId }} @ v{{ sceneVersion }}
      </span>
    </header>

    <p v-if="!scenario" class="dim">
      No scenario loaded. Paste or upload JSON on the left, then Load.
    </p>

    <template v-else>
      <div class="block">
        <div class="label">Scenario</div>
        <div class="mono">{{ scenario.name ?? '(unnamed)' }}</div>
      </div>

      <div class="block">
        <div class="label">Room</div>
        <div class="mono">{{ roomLine }}</div>
        <div v-if="scenario.room.faces" class="dim mono faces">
          <span v-for="(mat, face) in scenario.room.faces" :key="face">
            {{ face }}→{{ mat }}
          </span>
        </div>
      </div>

      <div class="block">
        <div class="label">Materials ({{ scenario.materials.length }})</div>
        <ul>
          <li v-for="mat in scenario.materials" :key="mat.id" class="mono">
            {{ mat.id }}
            <span class="dim"> — {{ mat.name }}</span>
          </li>
        </ul>
      </div>

      <div class="block">
        <div class="label">Sources ({{ scenario.sources.length }})</div>
        <ul>
          <li v-for="src in scenario.sources" :key="src.id" class="mono">
            {{ src.id }} {{ fmtPos(src.position) }}
            <span class="dim"> {{ src.powerDb }} dB</span>
          </li>
        </ul>
      </div>

      <div class="block">
        <div class="label">Receivers ({{ scenario.receivers.length }})</div>
        <ul>
          <li v-for="rcv in scenario.receivers" :key="rcv.id" class="mono">
            {{ rcv.id }} {{ fmtPos(rcv.position) }}
          </li>
        </ul>
      </div>

      <div class="block">
        <div class="label">Params</div>
        <div v-for="line in paramLines" :key="line" class="mono dim">{{ line }}</div>
      </div>
    </template>
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

.block {
  margin-bottom: 10px;
}

.label {
  color: var(--text-dim);
  font-size: 11px;
  text-transform: uppercase;
  letter-spacing: 0.04em;
  margin-bottom: 2px;
}

ul {
  margin: 0;
  padding-left: 16px;
}

.faces {
  display: flex;
  flex-wrap: wrap;
  gap: 4px 10px;
  margin-top: 2px;
}
</style>
