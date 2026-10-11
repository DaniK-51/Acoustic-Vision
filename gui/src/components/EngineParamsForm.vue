<script setup lang="ts">
import { computed, reactive, ref, watch } from 'vue';
import {
  formValuesFrom,
  parseObjectSchema,
  type SchemaProperty,
} from '../params-schema';

const props = defineProps<{
  schema: Record<string, unknown> | null | undefined;
  /** Stored scenario.params.methodParams, overlaid on schema defaults. */
  initial?: Record<string, unknown> | null;
  disabled?: boolean;
}>();

const emit = defineEmits<{
  'update:modelValue': [value: Record<string, unknown>];
}>();

const parsed = computed(() => parseObjectSchema(props.schema));
const rows = computed(() => Object.entries(parsed.value.properties));

const values = reactive<Record<string, unknown>>({});
const rawText = ref('');
const rawError = ref('');

function rebuild(): void {
  for (const key of Object.keys(values)) {
    delete values[key];
  }
  if (!parsed.value.renderable) {
    rawText.value = JSON.stringify(formValuesFrom(props.schema, props.initial), null, 2);
    rawError.value = '';
    return;
  }
  const next = formValuesFrom(props.schema, props.initial);
  for (const [key, value] of Object.entries(next)) {
    values[key] = value;
  }
}

function emitValues(): void {
  if (parsed.value.renderable) {
    emit('update:modelValue', { ...values });
    return;
  }
  try {
    const parsedRaw: unknown = rawText.value.trim() ? JSON.parse(rawText.value) : {};
    if (
      parsedRaw == null ||
      typeof parsedRaw !== 'object' ||
      Array.isArray(parsedRaw)
    ) {
      rawError.value = 'methodParams must be a JSON object.';
      return;
    }
    rawError.value = '';
    emit('update:modelValue', parsedRaw as Record<string, unknown>);
  } catch (err) {
    rawError.value = err instanceof Error ? err.message : String(err);
  }
}

function onNumber(key: string, event: Event, prop: SchemaProperty): void {
  const input = event.target as HTMLInputElement;
  const parsedNum = input.valueAsNumber;
  if (Number.isNaN(parsedNum)) {
    return;
  }
  let value = parsedNum;
  if (prop.type === 'integer') {
    value = Math.trunc(value);
  }
  if (prop.minimum !== undefined && value < prop.minimum) {
    value = prop.minimum;
  }
  if (prop.maximum !== undefined && value > prop.maximum) {
    value = prop.maximum;
  }
  values[key] = value;
}

function inputMin(prop: SchemaProperty): number | undefined {
  return prop.minimum ?? prop.exclusiveMinimum;
}

function inputMax(prop: SchemaProperty): number | undefined {
  return prop.maximum ?? prop.exclusiveMaximum;
}

function numberText(value: unknown): string {
  return typeof value === 'number' && Number.isFinite(value) ? String(value) : '';
}

watch(
  () => [props.schema, props.initial] as const,
  () => {
    rebuild();
    emitValues();
  },
  { deep: true, immediate: true },
);

watch(
  values,
  () => {
    if (parsed.value.renderable) {
      emitValues();
    }
  },
  { deep: true },
);
</script>

<template>
  <section class="panel">
    <header class="panel-head">
      <h2>Method params</h2>
      <span class="dim mono">{{ parsed.renderable ? `${rows.length} fields` : 'raw schema' }}</span>
    </header>

    <p v-if="!parsed.renderable" class="dim">
      Schema outside the supported subset — edit methodParams as JSON.
    </p>

    <div v-if="parsed.renderable && rows.length === 0" class="dim empty">
      Engine takes no methodParams.
    </div>

    <form v-if="parsed.renderable" class="fields" @submit.prevent>
      <label v-for="[key, prop] in rows" :key="key" class="field">
        <span class="label" :title="prop.description || key">
          {{ key }}
          <span v-if="prop.type === 'integer'" class="dim">int</span>
        </span>

        <select
          v-if="prop.type === 'string' && prop.enum"
          v-model="values[key] as string"
          :disabled="disabled"
        >
          <option v-for="opt in prop.enum" :key="String(opt)" :value="opt">
            {{ opt }}
          </option>
        </select>

        <input
          v-else-if="prop.type === 'boolean'"
          type="checkbox"
          class="check"
          :checked="Boolean(values[key])"
          :disabled="disabled"
          @change="values[key] = ($event.target as HTMLInputElement).checked"
        />

        <input
          v-else-if="prop.type === 'number' || prop.type === 'integer'"
          type="number"
          :value="numberText(values[key])"
          :min="inputMin(prop)"
          :max="inputMax(prop)"
          :step="prop.type === 'integer' ? 1 : 'any'"
          :disabled="disabled"
          @change="onNumber(key, $event, prop)"
        />

        <input
          v-else
          type="text"
          :value="String(values[key] ?? '')"
          :disabled="disabled"
          @input="values[key] = ($event.target as HTMLInputElement).value"
        />
      </label>
    </form>

    <template v-if="!parsed.renderable">
      <textarea
        v-model="rawText"
        rows="8"
        spellcheck="false"
        :disabled="disabled"
        @change="emitValues"
      ></textarea>
      <p v-if="rawError" class="error-text">{{ rawError }}</p>
      <details class="schema-raw">
        <summary class="dim">paramsSchema</summary>
        <pre class="mono">{{ JSON.stringify(schema ?? {}, null, 2) }}</pre>
      </details>
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
  align-items: center;
  justify-content: space-between;
  margin-bottom: 8px;
}

h2 {
  margin: 0;
  font-size: 13px;
  font-weight: 600;
}

.empty {
  margin: 0;
  font-size: 12px;
}

.fields {
  display: flex;
  flex-direction: column;
  gap: 6px;
}

.field {
  display: grid;
  grid-template-columns: 1fr 110px;
  align-items: center;
  gap: 8px;
  font-size: 12px;
}

.label {
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}

.field input[type='number'],
.field input[type='text'],
.field select {
  width: 100%;
  min-width: 0;
}

.field .check {
  justify-self: start;
}

textarea {
  width: 100%;
}

.schema-raw {
  margin-top: 8px;
}

.schema-raw pre {
  margin: 6px 0 0;
  padding: 6px 8px;
  background: var(--bg-inset);
  border: 1px solid var(--border);
  border-radius: 4px;
  font-size: 11px;
  overflow: auto;
  max-height: 160px;
}
</style>
