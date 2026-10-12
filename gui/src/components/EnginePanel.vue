<script setup lang="ts">
import { computed, ref } from 'vue';
import type { EngineDescriptor } from '../api/types';
import { ARTIFACT_LABELS, engineDoc } from '../engine-docs';
import { parseObjectSchema, type SchemaProperty } from '../params-schema';

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

// E1: in-app engine doc card (content from engine-docs.ts, params from paramsSchema).
const aboutOpen = ref(false);
const doc = computed(() =>
  props.modelValue ? engineDoc(props.modelValue) : null,
);

interface ParamRow {
  name: string;
  type: string;
  defaultText: string;
  rangeText: string;
  hint: string;
}

function rangeText(prop: SchemaProperty): string {
  const lo = prop.minimum ?? prop.exclusiveMinimum;
  const hi = prop.maximum ?? prop.exclusiveMaximum;
  if (lo === undefined && hi === undefined) {
    return '—';
  }
  const loText = lo === undefined ? '…' : (prop.exclusiveMinimum !== undefined ? `>${lo}` : `≥${lo}`);
  const hiText = hi === undefined ? '…' : (prop.exclusiveMaximum !== undefined ? `<${hi}` : `≤${hi}`);
  return `${loText} … ${hiText}`;
}

function defaultText(prop: SchemaProperty): string {
  if (prop.default === undefined) {
    return '—';
  }
  return typeof prop.default === 'string' ? prop.default : JSON.stringify(prop.default);
}

const paramRows = computed<ParamRow[]>(() => {
  if (!selected.value) {
    return [];
  }
  const { properties } = parseObjectSchema(selected.value.paramsSchema);
  const hints = doc.value?.paramHints ?? {};
  return Object.entries(properties).map(([name, prop]) => ({
    name,
    type: prop.type ?? '—',
    defaultText: defaultText(prop),
    rangeText: rangeText(prop),
    hint: hints[name] ?? prop.description ?? '',
  }));
});

function artifactLabel(kind: string): string {
  return ARTIFACT_LABELS[kind] ?? kind;
}
</script>

<template>
  <section class="panel">
    <header class="panel-head">
      <h2>Engines</h2>
      <button :disabled="loading" @click="emit('refresh')">Refresh</button>
    </header>

    <p v-if="error" class="error-text">{{ error }}</p>
    <p v-else-if="loading && engines.length === 0" class="dim">Loading…</p>

    <div class="row">
      <label class="method-label">
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
      <button
        class="about-btn"
        type="button"
        :disabled="!selected"
        title="О методе"
        aria-label="О методе"
        @click="aboutOpen = true"
      >
        ?
      </button>
    </div>

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

    <!-- E1: about card (modal) — name/id/version, what it computes, artifacts,
         when to use / limits, params from paramsSchema + hints, docs link -->
    <div v-if="aboutOpen && selected" class="overlay" @click.self="aboutOpen = false">
      <section class="card" role="dialog" aria-label="О методе">
        <header class="card-head">
          <h2>О методе</h2>
          <button class="ghost" @click="aboutOpen = false">Закрыть</button>
        </header>

        <table class="meta">
          <tbody>
            <tr>
              <th>name</th>
              <td>{{ selected.name }}</td>
            </tr>
            <tr>
              <th>id</th>
              <td class="mono">{{ selected.id }}</td>
            </tr>
            <tr>
              <th>version</th>
              <td class="mono">{{ selected.version }}</td>
            </tr>
          </tbody>
        </table>

        <h3>Что считает</h3>
        <p v-for="(para, i) in doc?.summary ?? []" :key="i" class="prose">{{ para }}</p>
        <p v-if="!doc?.summary?.length" class="dim prose">
          Описание для этого метода ещё не добавлено; контракт — docs/engines.md.
        </p>

        <h3>Артефакты</h3>
        <ul class="artifacts">
          <li v-for="kind in selected.artifacts" :key="kind">
            <span class="mono">{{ kind }}</span>
            <span class="dim"> — {{ artifactLabel(kind) }}</span>
          </li>
          <li v-if="selected.artifacts.length === 0" class="dim">—</li>
        </ul>

        <h3>Когда использовать / ограничения</h3>
        <ul class="usage">
          <li v-for="(item, i) in doc?.usage ?? []" :key="i">{{ item }}</li>
          <li v-if="!doc?.usage?.length" class="dim">
            rect: {{ selected.caps.supportsRect ? 'да' : 'нет' }} ·
            mesh: {{ selected.caps.supportsMesh ? 'да' : 'нет' }}
          </li>
        </ul>

        <h3>Параметры</h3>
        <p v-if="paramRows.length === 0" class="dim prose">
          Метод не принимает methodParams.
        </p>
        <table v-else class="params">
          <thead>
            <tr>
              <th>имя</th>
              <th>тип</th>
              <th>default</th>
              <th>range</th>
              <th>подсказка</th>
            </tr>
          </thead>
          <tbody>
            <tr v-for="row in paramRows" :key="row.name">
              <td class="mono">{{ row.name }}</td>
              <td class="dim">{{ row.type }}</td>
              <td class="mono">{{ row.defaultText }}</td>
              <td class="mono">{{ row.rangeText }}</td>
              <td class="hint">{{ row.hint || '—' }}</td>
            </tr>
          </tbody>
        </table>

        <p class="more dim">подробнее — <span class="mono">docs/engines.md</span></p>
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

.method-label {
  display: flex;
  align-items: center;
  gap: 8px;
  flex: 1;
  min-width: 0;
}

.method-label select {
  flex: 1;
  min-width: 0;
}

.about-btn {
  flex: none;
  width: 28px;
  padding: 5px 0;
  font-weight: 600;
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
  width: min(520px, 100%);
  max-height: 100%;
  overflow: auto;
  padding: 14px 16px;
}

.card-head {
  display: flex;
  align-items: center;
  justify-content: space-between;
  margin-bottom: 10px;
}

.card-head h2 {
  font-size: 14px;
}

.ghost {
  background: transparent;
}

.meta th {
  width: 70px;
}

h3 {
  margin: 12px 0 6px;
  font-size: 12px;
  font-weight: 600;
}

.prose {
  margin: 0 0 6px;
  font-size: 12px;
  white-space: normal;
}

.artifacts,
.usage {
  margin: 0;
  padding-left: 18px;
  font-size: 12px;
}

.artifacts li,
.usage li {
  margin-bottom: 3px;
  white-space: normal;
}

.params th,
.params td {
  white-space: normal;
}

.params .hint {
  color: var(--text-dim);
  font-size: 11px;
}

.more {
  margin: 12px 0 0;
  font-size: 11px;
}
</style>
