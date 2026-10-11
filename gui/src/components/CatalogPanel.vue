<script setup lang="ts">
import type { SourceCatalog } from '../api/types';

defineProps<{
  catalog: SourceCatalog | null;
  loading: boolean;
  error: string;
}>();

const emit = defineEmits<{ refresh: [] }>();
</script>

<template>
  <section class="panel">
    <header class="panel-head">
      <h2>Source catalog</h2>
      <button :disabled="loading" @click="emit('refresh')">Refresh</button>
    </header>

    <p v-if="error" class="error-text">{{ error }}</p>
    <p v-else-if="loading && !catalog" class="dim">Loading…</p>

    <div v-if="catalog" class="scroll">
      <table>
        <thead>
          <tr>
            <th>id</th>
            <th>name</th>
            <th>category</th>
            <th>power dB</th>
            <th>directivity</th>
          </tr>
        </thead>
        <tbody>
          <tr v-for="entry in catalog.sources" :key="entry.id">
            <td class="mono">{{ entry.id }}</td>
            <td :title="entry.manufacturer">{{ entry.name }}</td>
            <td>{{ entry.category ?? '—' }}</td>
            <td>{{ entry.powerDb.toFixed(1) }}</td>
            <td>{{ entry.directivity.type }}</td>
          </tr>
          <tr v-if="catalog.sources.length === 0">
            <td colspan="5" class="dim">Catalog is empty.</td>
          </tr>
        </tbody>
      </table>
      <p class="dim disclaimer">{{ catalog.disclaimer }}</p>
    </div>
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
  min-height: 160px;
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

.disclaimer {
  margin: 8px 0 0;
  font-size: 11px;
  white-space: normal;
}
</style>
