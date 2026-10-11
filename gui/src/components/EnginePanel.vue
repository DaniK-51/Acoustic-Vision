<script setup lang="ts">
import { computed } from 'vue';
import type { EngineDescriptor } from '../api/types';

const props = defineProps<{
  engines: EngineDescriptor[];
  loading: boolean;
  error: string;
  modelValue: string;
}>();

const emit = defineEmits<{
  'update:modelValue': [value: string];
  refresh: [];
}>();

const selected = computed<EngineDescriptor | undefined>(() =>
  props.engines.find((e) => e.id === props.modelValue),
);
</script>

<template>
  <section class="panel">
    <header class="panel-head">
      <h2>Engines</h2>
      <button :disabled="loading" @click="emit('refresh')">Refresh</button>
    </header>

    <p v-if="error" class="error-text">{{ error }}</p>
    <p v-else-if="loading && engines.length === 0" class="dim">Loading…</p>

    <label class="row">
      <span class="dim">Method</span>
      <select
        :value="modelValue"
        :disabled="engines.length === 0"
        @change="emit('update:modelValue', ($event.target as HTMLSelectElement).value)"
      >
        <option v-if="engines.length === 0" value="">— no engines —</option>
        <option v-for="engine in engines" :key="engine.id" :value="engine.id">
          {{ engine.name }} ({{ engine.id }}) · v{{ engine.version }}
        </option>
      </select>
    </label>

    <table v-if="selected">
      <tbody>
        <tr>
          <th>rect / mesh</th>
          <td>
            {{ selected.caps.supportsRect ? 'rect' : '—' }}
            /
            {{ selected.caps.supportsMesh ? 'mesh' : '—' }}
          </td>
        </tr>
        <tr>
          <th>max order</th>
          <td>{{ selected.caps.maxOrder }}</td>
        </tr>
        <tr>
          <th>bands / rays</th>
          <td>
            {{ selected.caps.supportsBands ? 'bands' : '—' }}
            /
            {{ selected.caps.supportsRayPaths ? 'ray paths' : '—' }}
          </td>
        </tr>
        <tr>
          <th>artifacts</th>
          <td class="mono">{{ selected.artifacts.join(', ') || '—' }}</td>
        </tr>
      </tbody>
    </table>
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
  align-items: center;
  justify-content: space-between;
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

.row select {
  flex: 1;
  min-width: 0;
}
</style>
