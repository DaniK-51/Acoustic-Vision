<script setup lang="ts">
import { computed } from 'vue';
import type { Scenario } from '../api/types';
import { fmtBand, materialById, materialColor, surfaceBindings } from '../materials';

// A3: face → material → α per band. Rows are clickable so a face picked here
// syncs the same selection as a viewport click (A2 tooltip + face highlight).
const props = defineProps<{
  scenario: Scenario | null;
  /** Currently selected face key ('floor', 'x+', 'walls', …) or null. */
  selectedFace: string | null;
}>();

const emit = defineEmits<{
  selectFace: [string | null];
}>();

const rows = computed(() =>
  surfaceBindings(props.scenario).map((binding) => {
    const material = materialById(props.scenario, binding.materialId);
    return {
      surface: binding.surface,
      materialId: binding.materialId,
      materialName: material?.name ?? '(missing material)',
      color: materialColor(props.scenario, binding.materialId),
      scattering: material?.scattering,
      bandsHz: material?.bandsHz ?? [],
      absorption: material?.absorption ?? [],
    };
  }),
);

// Column bands: SimParams band list when present, else the first material's.
const bands = computed(() => {
  const sc = props.scenario;
  if (!sc) {
    return [] as number[];
  }
  return sc.params.bandsHz ?? sc.materials[0]?.bandsHz ?? [];
});

function alphaAt(row: (typeof rows.value)[number], band: number): string {
  const idx = row.bandsHz.indexOf(band);
  if (idx < 0 || idx >= row.absorption.length) {
    return '—';
  }
  return row.absorption[idx].toFixed(2);
}

function onRow(surface: string): void {
  emit('selectFace', props.selectedFace === surface ? null : surface);
}
</script>

<template>
  <section class="panel">
    <header class="panel-head">
      <h2>Materials</h2>
      <span class="dim">{{ rows.length }} surface(s)</span>
    </header>

    <p v-if="!scenario" class="dim">No scenario loaded.</p>
    <p v-else-if="rows.length === 0" class="dim">
      No surface → material bindings in this room.
    </p>

    <div v-else class="table-wrap">
      <table>
        <thead>
          <tr>
            <th>Surface</th>
            <th>Material</th>
            <th v-for="band in bands" :key="band" class="num">{{ fmtBand(band) }}</th>
            <th class="num">sc</th>
          </tr>
        </thead>
        <tbody>
          <tr
            v-for="row in rows"
            :key="row.surface"
            :class="{ selected: row.surface === selectedFace }"
            @click="onRow(row.surface)"
          >
            <td class="mono">{{ row.surface }}</td>
            <td>
              <span class="swatch" :style="{ background: row.color }"></span>
              <span class="mono">{{ row.materialId }}</span>
              <span class="dim"> — {{ row.materialName }}</span>
            </td>
            <td v-for="band in bands" :key="band" class="num mono">
              {{ alphaAt(row, band) }}
            </td>
            <td class="num mono dim">
              {{ row.scattering !== undefined ? row.scattering.toFixed(2) : '—' }}
            </td>
          </tr>
        </tbody>
      </table>
      <p class="dim hint">α(f) per band · click a row to select the surface</p>
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
  align-items: baseline;
  justify-content: space-between;
  gap: 8px;
  margin-bottom: 8px;
}

h2 {
  margin: 0;
  font-size: 13px;
  font-weight: 600;
}

.table-wrap {
  overflow-x: auto;
}

.swatch {
  display: inline-block;
  width: 10px;
  height: 10px;
  border-radius: 2px;
  margin-right: 6px;
  vertical-align: baseline;
}

.num {
  text-align: right;
}

tr.selected td {
  background: rgba(79, 140, 255, 0.14);
}

tbody tr {
  cursor: pointer;
}

tbody tr:hover td {
  background: rgba(79, 140, 255, 0.08);
}

.hint {
  margin: 6px 0 0;
  font-size: 11px;
}
</style>
