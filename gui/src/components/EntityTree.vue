<script setup lang="ts">
import { ref } from 'vue';
import type { EditorEntity, EditorScene } from '../api/types';
import { entityLabel, isGeometryEntity } from '../editor-scene';

// Left-column entity list + scene-document I/O. The API has no list endpoint
// for scenes (v1) — Load takes an explicit sceneId; Save is POST (new) or
// PUT (replace) depending on whether the document already has a server id.
const props = defineProps<{
  scene: EditorScene | null;
  selectedId: string | null;
  sceneId: string;
  sceneVersion: number;
  saving: boolean;
  loading: boolean;
  error: string;
}>();

const emit = defineEmits<{
  select: [id: string | null];
  add: [kind: 'wall' | 'solid_box' | 'void' | 'solid_extrude'];
  delete: [id: string];
  save: [];
  load: [sceneId: string];
  newScene: [];
  clearError: [];
}>();

const loadId = ref('');

function typeLabel(entity: EditorEntity): string {
  switch (entity.type) {
    case 'wall':
      return 'wall';
    case 'solid_box':
      return 'solid_box';
    case 'solid_extrude':
      return 'solid_extrude';
    case 'void':
      return 'void';
    case 'source':
      return 'source';
    case 'receiver':
      return 'receiver';
    default:
      return entity.type;
  }
}

function onLoad(): void {
  const id = loadId.value.trim();
  if (id) {
    emit('load', id);
  }
}

function onDelete(): void {
  if (props.selectedId) {
    emit('delete', props.selectedId);
  }
}
</script>

<template>
  <section class="panel">
    <header class="panel-head">
      <h2>Entities</h2>
      <span v-if="sceneId" class="dim mono">
        {{ sceneId }} @ v{{ sceneVersion }}
      </span>
      <span v-else class="dim">not saved</span>
    </header>

    <div class="doc-row">
      <button :disabled="saving || loading" title="New local document" @click="emit('newScene')">
        New
      </button>
      <button
        :disabled="saving || loading || !scene"
        :title="sceneId ? 'PUT /v1/scenes/{id} (replace)' : 'POST /v1/scenes'"
        @click="emit('save')"
      >
        {{ saving ? 'Saving…' : sceneId ? 'Save' : 'Create' }}
      </button>
    </div>

    <div class="doc-row">
      <input
        v-model="loadId"
        class="mono"
        placeholder="scene-id"
        spellcheck="false"
        @keyup.enter="onLoad"
      />
      <button :disabled="loading || !loadId.trim()" @click="onLoad">
        {{ loading ? 'Loading…' : 'Load' }}
      </button>
    </div>

    <p v-if="error" class="error-text" @click="emit('clearError')">{{ error }}</p>

    <div class="add-row">
      <span class="dim">Add:</span>
      <button :disabled="!scene" title="Wall / slab" @click="emit('add', 'wall')">
        + Wall
      </button>
      <button :disabled="!scene" title="Solid box" @click="emit('add', 'solid_box')">
        + Box
      </button>
      <button :disabled="!scene" title="Void (opening volume)" @click="emit('add', 'void')">
        + Void
      </button>
      <button
        :disabled="!scene"
        title="Solid extrude (polygon prism)"
        @click="emit('add', 'solid_extrude')"
      >
        + Extrude
      </button>
    </div>

    <p v-if="!scene" class="dim">No editor scene. Press New or Load.</p>
    <p v-else-if="scene.entities.length === 0" class="dim">
      Empty scene — add walls / boxes above.
    </p>

    <ul v-else class="tree">
      <li
        v-for="entity in scene.entities"
        :key="entity.id"
        class="tree-row"
        :class="{ selected: entity.id === selectedId, geom: isGeometryEntity(entity) }"
        @click="emit('select', entity.id === selectedId ? null : entity.id)"
      >
        <span class="kind mono">{{ typeLabel(entity) }}</span>
        <span class="id mono">{{ entityLabel(entity) }}</span>
      </li>
    </ul>

    <div class="del-row">
      <button :disabled="!selectedId" @click="onDelete">Delete selected</button>
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
  flex-wrap: wrap;
}

h2 {
  margin: 0;
  font-size: 13px;
  font-weight: 600;
}

.doc-row,
.add-row,
.del-row {
  display: flex;
  align-items: center;
  flex-wrap: wrap;
  gap: 6px;
  margin-bottom: 8px;
}

.doc-row input {
  flex: 1;
  min-width: 0;
}

.add-row {
  font-size: 11px;
}

.tree {
  list-style: none;
  margin: 0;
  padding: 0;
  max-height: 260px;
  overflow: auto;
  border: 1px solid var(--border);
  border-radius: 4px;
}

.tree-row {
  display: flex;
  align-items: center;
  gap: 8px;
  padding: 4px 8px;
  cursor: pointer;
  border-bottom: 1px solid var(--border);
  font-size: 12px;
}

.tree-row:last-child {
  border-bottom: none;
}

.tree-row:hover {
  background: #243044;
}

.tree-row.selected {
  background: rgba(79, 140, 255, 0.22);
}

.kind {
  color: var(--text-dim);
  font-size: 10px;
  min-width: 72px;
}

.id {
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}

.error-text {
  margin: 0 0 8px;
  cursor: pointer;
}
</style>
