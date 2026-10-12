<script setup lang="ts">
import { computed } from 'vue';
import { state } from '../store';

const roomLabel = computed(() => {
  const room = state.scenario?.room;
  if (!room) {
    return '';
  }
  if (room.type === 'rect' && room.size) {
    return `rect ${room.size.w}×${room.size.h}×${room.size.d} m`;
  }
  return room.type;
});
</script>

<template>
  <section class="panel">
    <h2>Scene outline</h2>
    <p v-if="!state.scenario" class="muted">No scenario loaded.</p>
    <template v-else>
      <p class="muted">
        {{ state.scenario.name }} · {{ roomLabel }}
      </p>
      <h3>Sources</h3>
      <table>
        <thead>
          <tr><th>id</th><th>position</th><th>power dB</th></tr>
        </thead>
        <tbody>
          <tr v-for="src in state.scenario.sources" :key="src.id">
            <td>{{ src.id }}</td>
            <td>{{ src.position.join(', ') }}</td>
            <td>{{ src.powerDb }}</td>
          </tr>
          <tr v-if="state.scenario.sources.length === 0">
            <td colspan="3" class="muted">none</td>
          </tr>
        </tbody>
      </table>
      <h3>Receivers</h3>
      <table>
        <thead>
          <tr><th>id</th><th>position</th></tr>
        </thead>
        <tbody>
          <tr v-for="rcv in state.scenario.receivers" :key="rcv.id">
            <td>{{ rcv.id }}</td>
            <td>{{ rcv.position.join(', ') }}</td>
          </tr>
          <tr v-if="state.scenario.receivers.length === 0">
            <td colspan="2" class="muted">none</td>
          </tr>
        </tbody>
      </table>
      <h3>Materials</h3>
      <p class="muted">
        {{ state.scenario.materials.map((m) => m.id).join(', ') || 'none' }}
      </p>
    </template>
  </section>
</template>

<style scoped>
h3 {
  margin: 10px 0 4px;
  font-size: 11px;
  font-weight: 700;
  text-transform: uppercase;
  letter-spacing: 0.05em;
  color: var(--text-dim);
}
</style>
