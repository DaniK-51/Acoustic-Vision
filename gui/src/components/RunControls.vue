<script setup lang="ts">
defineProps<{
  hasScene: boolean;
  running: boolean;
  canCancel: boolean;
  engineId: string;
  error: string;
}>();

const emit = defineEmits<{ run: []; cancel: [] }>();
</script>

<template>
  <section class="panel">
    <header class="panel-head">
      <h2>Compute</h2>
      <span class="dim mono">{{ engineId || '—' }}</span>
    </header>

    <div class="row">
      <button class="primary" :disabled="!hasScene || running" @click="emit('run')">
        {{ running ? 'Running…' : 'Расчёт' }}
      </button>
      <button :disabled="!canCancel" @click="emit('cancel')">Отмена</button>
    </div>

    <p v-if="error" class="error-text">{{ error }}</p>
    <p v-else-if="!hasScene" class="dim hint">Load a scenario first (sceneId).</p>
    <p class="dim hint">
      POST /v1/simulations — {sceneId} + engine/methodParams overlay.
    </p>
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
  gap: 8px;
}

.primary {
  flex: 1;
  background: var(--accent);
  border-color: transparent;
  color: #fff;
}

.primary:hover:not(:disabled) {
  background: #6ba0ff;
}

.hint {
  margin: 8px 0 0;
  font-size: 11px;
}
</style>
