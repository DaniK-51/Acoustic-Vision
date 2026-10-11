<script setup lang="ts">
import { computed } from 'vue';
import type { HealthResponse } from '../api/types';
import type { WsStatus } from '../api/ws';
import type { ActiveJob } from '../ui-types';

const props = defineProps<{
  apiBase: string;
  health: HealthResponse | null;
  healthError: string;
  wsStatus: WsStatus;
  job: ActiveJob | null;
}>();

const healthLabel = computed(() => {
  if (props.healthError && !props.health) {
    return 'offline';
  }
  return props.health?.status ?? 'unknown';
});

const jobLabel = computed(() => {
  const job = props.job;
  if (!job) {
    return 'no active job';
  }
  const pct = Math.round((job.fraction ?? 0) * 100);
  return `${job.jobId} · ${job.state} · ${pct}% · ${job.stage || '—'}`;
});
</script>

<template>
  <footer class="status">
    <span class="item">
      <span class="dot" :data-health="healthError && !health ? 'down' : 'up'"></span>
      health: {{ healthLabel }}
      <span v-if="health" class="dim"> v{{ health.version }}</span>
      <span v-if="healthError && !health" class="dim"> — {{ healthError }}</span>
    </span>

    <span class="item">
      ws:
      <span class="mono" :data-ws="wsStatus">{{ wsStatus }}</span>
    </span>

    <span class="item job">
      <span class="job-label">{{ jobLabel }}</span>
      <span v-if="job && job.state === 'running'" class="bar">
        <span class="fill" :style="{ width: `${Math.round((job.fraction ?? 0) * 100)}%` }"></span>
      </span>
      <span v-if="job?.error" class="error-text">{{ job.error }}</span>
    </span>

    <span class="item dim mono api">{{ apiBase }}</span>
  </footer>
</template>

<style scoped>
.status {
  display: flex;
  align-items: center;
  gap: 18px;
  padding: 6px 14px;
  border-top: 1px solid var(--border);
  background: var(--bg-panel);
  font-size: 12px;
  flex-wrap: wrap;
}

.item {
  display: inline-flex;
  align-items: center;
  gap: 6px;
}

.job {
  flex: 1;
  min-width: 200px;
}

.job-label {
  font-family: var(--mono);
}

.dot {
  width: 8px;
  height: 8px;
  border-radius: 50%;
  display: inline-block;
}

.dot[data-health='up'] {
  background: var(--ok);
}

.dot[data-health='down'] {
  background: var(--err);
}

[data-ws='open'] {
  color: var(--ok);
}

[data-ws='connecting'] {
  color: var(--warn);
}

[data-ws='closed'] {
  color: var(--err);
}

.bar {
  flex: 1;
  max-width: 220px;
  height: 6px;
  border-radius: 3px;
  background: var(--bg-inset);
  overflow: hidden;
}

.fill {
  display: block;
  height: 100%;
  background: var(--accent);
}

.api {
  margin-left: auto;
}
</style>
