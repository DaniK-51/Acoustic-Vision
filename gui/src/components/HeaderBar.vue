<script setup lang="ts">
import { state } from '../store';
import { apiBaseUrl } from '../config';
</script>

<template>
  <header class="header">
    <div class="brand">Acoustic-Vision</div>
    <label class="engine-label">
      Engine
      <select v-model="state.selectedEngineId">
        <option v-if="state.engines.length === 0" value="">(no engines)</option>
        <option v-for="engine in state.engines" :key="engine.id" :value="engine.id">
          {{ engine.name || engine.id }}
        </option>
      </select>
    </label>
    <span class="muted">{{ apiBaseUrl }}</span>
  </header>
</template>

<style scoped>
.header {
  display: flex;
  align-items: center;
  gap: 16px;
  padding: 10px 14px;
  background: var(--panel);
  border-bottom: 1px solid var(--border);
}

.brand {
  font-size: 16px;
  font-weight: 700;
  letter-spacing: 0.02em;
}

.engine-label {
  display: flex;
  align-items: center;
  gap: 8px;
  font-size: 13px;
  color: var(--text-dim);
}
</style>
