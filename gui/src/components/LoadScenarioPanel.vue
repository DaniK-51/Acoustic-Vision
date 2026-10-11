<script setup lang="ts">
import { ref } from 'vue';

const props = defineProps<{
  loading: boolean;
  error: string;
  sceneId: string;
  sceneVersion: number;
}>();

const emit = defineEmits<{
  submit: [scenario: Record<string, unknown>];
}>();

const text = ref('');
const parseError = ref('');
const fileInput = ref<HTMLInputElement | null>(null);

function tryParse(raw: string): Record<string, unknown> | null {
  parseError.value = '';
  if (!raw.trim()) {
    parseError.value = 'Scenario JSON is empty.';
    return null;
  }
  try {
    const parsed: unknown = JSON.parse(raw);
    if (!parsed || typeof parsed !== 'object' || Array.isArray(parsed)) {
      parseError.value = 'Scenario must be a JSON object.';
      return null;
    }
    return parsed as Record<string, unknown>;
  } catch (err) {
    parseError.value = err instanceof Error ? err.message : String(err);
    return null;
  }
}

function submit(): void {
  const scenario = tryParse(text.value);
  if (scenario) {
    emit('submit', scenario);
  }
}

function onFile(event: Event): void {
  const input = event.target as HTMLInputElement;
  const file = input.files?.[0];
  if (!file) {
    return;
  }
  const reader = new FileReader();
  reader.onload = () => {
    text.value = String(reader.result ?? '');
    parseError.value = '';
  };
  reader.onerror = () => {
    parseError.value = `Cannot read ${file.name}`;
  };
  reader.readAsText(file);
  // Allow re-selecting the same file later.
  input.value = '';
}

function useDemo(): void {
  text.value = JSON.stringify(
    {
      schemaVersion: 1,
      name: 'gui-demo',
      room: {
        type: 'rect',
        size: { w: 6.0, h: 2.8, d: 4.5 },
        faces: {
          floor: 'mat-carpet',
          ceiling: 'mat-plaster',
          'x+': 'mat-brick',
          'x-': 'mat-brick',
          'z+': 'mat-brick',
          'z-': 'mat-brick',
        },
      },
      materials: [
        {
          id: 'mat-carpet',
          name: 'Carpet on concrete',
          bandsHz: [125, 250, 500, 1000, 2000, 4000],
          absorption: [0.1, 0.2, 0.3, 0.4, 0.4, 0.4],
          scattering: 0.1,
        },
        {
          id: 'mat-plaster',
          name: 'Plasterboard',
          bandsHz: [125, 250, 500, 1000, 2000, 4000],
          absorption: [0.1, 0.1, 0.15, 0.2, 0.2, 0.2],
          scattering: 0.1,
        },
        {
          id: 'mat-brick',
          name: 'Painted brick',
          bandsHz: [125, 250, 500, 1000, 2000, 4000],
          absorption: [0.1, 0.1, 0.12, 0.15, 0.15, 0.2],
          scattering: 0.1,
        },
      ],
      sources: [{ id: 'src-1', position: [1.2, 1.5, 1.0], powerDb: 80.0 }],
      receivers: [{ id: 'rcv-1', position: [4.0, 1.5, 3.0] }],
      params: {
        engine: 'image-source',
        maxReflectionOrder: 10,
        sampleRate: 16000,
        durationSec: 1.0,
        bandsHz: [125, 250, 500, 1000, 2000, 4000],
        speedOfSound: 343.0,
        capturePaths: false,
        airAbsorption: false,
        signedIr: false,
      },
    },
    null,
    2,
  );
  parseError.value = '';
}
</script>

<template>
  <section class="panel">
    <header class="panel-head">
      <h2>Load scenario</h2>
      <button class="ghost" :disabled="loading" @click="useDemo">Demo</button>
    </header>

    <textarea
      v-model="text"
      rows="10"
      spellcheck="false"
      placeholder='{"schemaVersion":1,"name":"...","room":{...}, ...}'
    ></textarea>

    <div class="row">
      <input ref="fileInput" type="file" accept=".json,application/json" @change="onFile" />
      <button :disabled="loading || !text.trim()" @click="submit">
        {{ loading ? 'Posting…' : 'Load' }}
      </button>
    </div>

    <p v-if="parseError" class="error-text">{{ parseError }}</p>
    <p v-else-if="error" class="error-text">{{ error }}</p>

    <p v-if="sceneId" class="ok mono">
      sceneId: {{ sceneId }} <span class="dim">(v{{ sceneVersion }})</span>
    </p>
    <p class="dim hint">POST /v1/scenarios → stored snapshot; run later via Compute (GUI2).</p>
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

.ghost {
  background: transparent;
}

textarea {
  width: 100%;
}

.row {
  display: flex;
  align-items: center;
  gap: 8px;
  margin-top: 8px;
}

.row input[type='file'] {
  flex: 1;
  min-width: 0;
  border: none;
  background: transparent;
  padding: 0;
  font-size: 11px;
  color: var(--text-dim);
}

.ok {
  color: var(--ok);
  margin: 8px 0 0;
  word-break: break-all;
}

.hint {
  margin: 8px 0 0;
  font-size: 11px;
}
</style>
