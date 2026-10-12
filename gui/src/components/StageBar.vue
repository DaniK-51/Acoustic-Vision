<script setup lang="ts">
import { WORKFLOW_STAGES, type WorkflowStage } from '../ui-types';

defineProps<{ stage: WorkflowStage }>();

const emit = defineEmits<{ 'update:stage': [stage: WorkflowStage] }>();
</script>

<template>
  <nav class="stage-bar" aria-label="Workflow stages">
    <template v-for="(step, index) in WORKFLOW_STAGES" :key="step.id">
      <span v-if="index > 0" class="sep" aria-hidden="true">→</span>
      <button
        type="button"
        class="step"
        :class="{ active: step.id === stage }"
        :aria-current="step.id === stage ? 'step' : undefined"
        :title="`Stage ${index + 1}: ${step.label}`"
        @click="emit('update:stage', step.id)"
      >
        <span class="num">{{ index + 1 }}</span>
        <span class="label">{{ step.label }}</span>
      </button>
    </template>
  </nav>
</template>

<style scoped>
.stage-bar {
  display: flex;
  align-items: center;
  gap: 6px;
  padding: 6px 14px;
  border-bottom: 1px solid var(--border);
  background: var(--bg-panel);
  overflow-x: auto;
}

.step {
  display: inline-flex;
  align-items: center;
  gap: 6px;
  padding: 4px 10px;
  border-radius: 12px;
  font-size: 12px;
  white-space: nowrap;
  background: transparent;
}

.step:hover:not(:disabled) {
  background: #243044;
}

.step.active {
  background: var(--accent);
  border-color: transparent;
  color: #fff;
}

.num {
  display: inline-flex;
  align-items: center;
  justify-content: center;
  width: 16px;
  height: 16px;
  border-radius: 50%;
  background: rgba(255, 255, 255, 0.12);
  font-size: 10px;
  font-weight: 600;
}

.sep {
  color: var(--text-dim);
  font-size: 11px;
}
</style>
