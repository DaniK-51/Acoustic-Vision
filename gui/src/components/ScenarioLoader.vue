<script setup lang="ts">
import { ref } from 'vue';
import { loadScenarioJson, state, submitSimulation } from '../store';
import type { Scenario } from '../api/types';

const jsonText = ref('');
const parseError = ref('');
const busy = ref(false);

function onFilePicked(event: Event): void {
  const input = event.target as HTMLInputElement;
  const file = input.files?.[0];
  if (!file) {
    return;
  }
  const reader = new FileReader();
  reader.onload = () => {
    jsonText.value = String(reader.result ?? '');
    parseError.value = '';
  };
  reader.readAsText(file);
  input.value = '';
}

function parseScenario(): Scenario {
  const parsed = JSON.parse(jsonText.value) as Scenario;
  if (!parsed || typeof parsed !== 'object') {
    throw new Error('scenario must be a JSON object');
  }
  return parsed;
}

async function upload(): Promise<void> {
  parseError.value = '';
  busy.value = true;
  try {
    const scenario = parseScenario();
    await loadScenarioJson(scenario);
  } catch (error) {
    parseError.value = error instanceof Error ? error.message : String(error);
  } finally {
    busy.value = false;
  }
}

async function run(): Promise<void> {
  busy.value = true;
  try {
    await submitSimulation();
  } finally {
    busy.value = false;
  }
}
</script>

<template>
  <section class="panel">
    <h2>Scenario</h2>
    <div class="row">
      <input type="file" accept=".json,application/json" @change="onFilePicked" />
    </div>
    <textarea
      v-model="jsonText"
      placeholder='Paste scenario JSON here (schemaVersion, room, materials, sources, receivers, params)'
      spellcheck="false"
    />
    <div class="row">
      <button class="primary" :disabled="busy || !jsonText.trim()" @click="upload">
        Upload to API
      </button>
      <button :disabled="busy || !state.sceneId" @click="run">Run simulation</button>
    </div>
    <p v-if="parseError" class="error-text">{{ parseError }}</p>
    <p v-else-if="state.sceneId" class="muted">
      sceneId: <span class="ok-text">{{ state.sceneId }}</span>
      · v{{ state.sceneVersion }}
    </p>
  </section>
</template>
