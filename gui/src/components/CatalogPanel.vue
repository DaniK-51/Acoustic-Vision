<script setup lang="ts">
import { ref } from 'vue';
import type { SourceCatalog, SourceCatalogEntry } from '../api/types';
import PolarChart from './PolarChart.vue';

defineProps<{
  catalog: SourceCatalog | null;
  loading: boolean;
  error: string;
  /** False outside the Scene stage or when no scenario is loaded (D3). */
  canInsert: boolean;
  /** Why insert is unavailable — shown in the button tooltip. */
  insertHint: string;
}>();

const emit = defineEmits<{
  refresh: [];
  insert: [entry: SourceCatalogEntry];
}>();

// D4: simple about card — name, category, powerDb, directivity, disclaimer.
// B3: polar chart of the card's directivity curves / analytic preset.
const aboutEntry = ref<SourceCatalogEntry | null>(null);
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
            <th></th>
          </tr>
        </thead>
        <tbody>
          <tr v-for="entry in catalog.sources" :key="entry.id">
            <td class="mono">{{ entry.id }}</td>
            <td :title="entry.manufacturer">{{ entry.name }}</td>
            <td>{{ entry.category ?? '—' }}</td>
            <td>{{ entry.powerDb.toFixed(1) }}</td>
            <td>{{ entry.directivity.type }}</td>
            <td class="actions">
              <button class="linkish" title="Карточка источника" @click="aboutEntry = entry">
                О источнике
              </button>
              <button
                class="linkish"
                :disabled="!canInsert"
                :title="canInsert ? 'Add source to the loaded scenario' : insertHint"
                @click="emit('insert', entry)"
              >
                Вставить источник
              </button>
            </td>
          </tr>
          <tr v-if="catalog.sources.length === 0">
            <td colspan="6" class="dim">Catalog is empty.</td>
          </tr>
        </tbody>
      </table>
      <p class="dim disclaimer">{{ catalog.disclaimer }}</p>
    </div>

    <!-- D4: about card (deliberately not a PDF datasheet) -->
    <div v-if="aboutEntry" class="overlay" @click.self="aboutEntry = null">
      <section class="card" role="dialog" aria-label="О источнике">
        <header class="card-head">
          <h2>О источнике</h2>
          <button class="ghost" @click="aboutEntry = null">Закрыть</button>
        </header>
        <table>
          <tbody>
            <tr>
              <th>name</th>
              <td>{{ aboutEntry.name }}</td>
            </tr>
            <tr>
              <th>category</th>
              <td>{{ aboutEntry.category ?? '—' }}</td>
            </tr>
            <tr>
              <th>power</th>
              <td>{{ aboutEntry.powerDb.toFixed(1) }} dB</td>
            </tr>
            <tr>
              <th>directivity</th>
              <td>{{ aboutEntry.directivity.type }}</td>
            </tr>
          </tbody>
        </table>
        <!-- B3: polar (angle × dB) for the card's directivity -->
        <PolarChart :directivity="aboutEntry.directivity" :height="200" />
        <p class="dim disclaimer">{{ aboutEntry.disclaimer }}</p>
      </section>
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

.actions {
  display: flex;
  gap: 4px;
}

.linkish {
  padding: 2px 8px;
  font-size: 11px;
  white-space: nowrap;
}

.disclaimer {
  margin: 8px 0 0;
  font-size: 11px;
  white-space: normal;
}

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

.card {
  background: var(--bg-panel);
  border: 1px solid var(--border);
  border-radius: 8px;
  width: min(420px, 100%);
  padding: 14px 16px;
}

.card-head {
  display: flex;
  align-items: center;
  justify-content: space-between;
  margin-bottom: 10px;
}

.card th {
  width: 90px;
}

.ghost {
  background: transparent;
}
</style>
