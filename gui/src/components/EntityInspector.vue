<script setup lang="ts">
import { computed } from 'vue';
import type {
  EditorEntity,
  EditorMaterial,
  EditorMeshSettings,
  MeshQuality,
  Vec3,
  WallEntity,
  WallOpening,
  WallOpeningType,
} from '../api/types';
import { isSolidBox, isSolidExtrude, isVoid, isWall } from '../editor-scene';

// Right-column numeric editor for the selected Scene entity. Emits a whole
// replacement entity (typed fields keep the wire shape honest); the shell
// applies it to editorScene and persists via PUT on Save.
const props = defineProps<{
  entity: EditorEntity | null;
  materials: EditorMaterial[];
  meshSettings: EditorMeshSettings | null;
  disabled: boolean;
}>();

const emit = defineEmits<{
  updateEntity: [entity: EditorEntity];
  updateMeshSettings: [settings: EditorMeshSettings];
}>();

const wall = computed(() => (props.entity && isWall(props.entity) ? props.entity : null));
const box = computed(() => (props.entity && isSolidBox(props.entity) ? props.entity : null));
const extrude = computed(() =>
  props.entity && isSolidExtrude(props.entity) ? props.entity : null,
);
const voidEnt = computed(() => (props.entity && isVoid(props.entity) ? props.entity : null));

function num(raw: string, fallback: number): number {
  const v = Number(raw);
  return Number.isFinite(v) ? v : fallback;
}

function vecFrom(values: [string, string, string], fallback: Vec3): Vec3 {
  return [num(values[0], fallback[0]), num(values[1], fallback[1]), num(values[2], fallback[2])];
}

function onWallField(field: keyof WallEntity, raw: string): void {
  const w = wall.value;
  if (!w || props.disabled) {
    return;
  }
  if (field === 'height' || field === 'thickness') {
    emit('updateEntity', { ...w, [field]: num(raw, w[field] as number) });
    return;
  }
  if (field === 'id' || field === 'name' || field === 'type') {
    emit('updateEntity', { ...w, [field]: raw });
  }
}

function onWallVec(field: 'start' | 'end', axis: 0 | 1 | 2, raw: string): void {
  const w = wall.value;
  if (!w || props.disabled) {
    return;
  }
  const next = [...w[field]] as Vec3;
  next[axis] = num(raw, next[axis]);
  emit('updateEntity', { ...w, [field]: next });
}

function onWallFace(face: 'inner' | 'outer' | 'side', materialId: string): void {
  const w = wall.value;
  if (!w || props.disabled) {
    return;
  }
  emit('updateEntity', {
    ...w,
    faces: { ...w.faces, [face]: materialId },
  });
}

function onOpening(index: number, patch: Partial<WallOpening>): void {
  const w = wall.value;
  if (!w || props.disabled) {
    return;
  }
  const openings = (w.openings ?? []).map((o, i) => (i === index ? { ...o, ...patch } : o));
  emit('updateEntity', { ...w, openings });
}

function addOpening(type: WallOpeningType): void {
  const w = wall.value;
  if (!w || props.disabled) {
    return;
  }
  const openings = [...(w.openings ?? [])];
  openings.push({
    type,
    offset: 0.5,
    width: type === 'door' ? 0.9 : 1.2,
    height: type === 'door' ? 2.1 : 1.4,
    sill: type === 'door' ? 0 : 0.9,
  });
  emit('updateEntity', { ...w, openings });
}

function removeOpening(index: number): void {
  const w = wall.value;
  if (!w || props.disabled) {
    return;
  }
  const openings = (w.openings ?? []).filter((_, i) => i !== index);
  emit('updateEntity', { ...w, openings });
}

function onBoxVec(field: 'origin' | 'size', axis: 0 | 1 | 2, raw: string): void {
  const b = box.value;
  if (!b || props.disabled) {
    return;
  }
  const next = [...b[field]] as Vec3;
  next[axis] = num(raw, next[axis]);
  emit('updateEntity', { ...b, [field]: next });
}

function onVoidVec(field: 'origin' | 'size', axis: 0 | 1 | 2, raw: string): void {
  const v = voidEnt.value;
  if (!v || props.disabled) {
    return;
  }
  const next = [...v[field]] as Vec3;
  next[axis] = num(raw, next[axis]);
  emit('updateEntity', { ...v, [field]: next });
}

function onBoxMaterial(materialId: string): void {
  const b = box.value ?? extrude.value;
  if (!b || props.disabled) {
    return;
  }
  emit('updateEntity', { ...b, materialId });
}

function onExtrudeHeight(raw: string): void {
  const e = extrude.value;
  if (!e || props.disabled) {
    return;
  }
  emit('updateEntity', { ...e, height: num(raw, e.height) });
}

function onEntityName(raw: string): void {
  const ent = props.entity;
  if (!ent || props.disabled) {
    return;
  }
  emit('updateEntity', { ...ent, name: raw });
}

function onQuality(quality: MeshQuality): void {
  const base = props.meshSettings ?? {};
  emit('updateMeshSettings', {
    ...base,
    surface: { ...(base.surface ?? {}), quality },
  });
}

const quality = computed<MeshQuality>(
  () => props.meshSettings?.surface?.quality ?? 'draft',
);
</script>

<template>
  <section class="panel">
    <header class="panel-head">
      <h2>Inspector</h2>
      <span v-if="entity" class="dim mono">{{ entity.type }}</span>
      <span v-else class="dim">mesh settings</span>
    </header>

    <!-- No selection: meshSettings.quality (also mirrored in MeshPanel). -->
    <template v-if="!entity">
      <div class="field">
        <label>Mesh quality</label>
        <select
          :value="quality"
          :disabled="disabled"
          @change="onQuality(($event.target as HTMLInputElement).value as MeshQuality)"
        >
          <option value="draft">draft</option>
          <option value="standard">standard</option>
          <option value="fine">fine</option>
        </select>
      </div>
      <p class="dim hint">
        Select an entity in the tree or viewport to edit its geometry.
      </p>
    </template>

    <template v-else>
      <div class="field">
        <label>Name</label>
        <input
          :value="entity.name ?? ''"
          :disabled="disabled"
          type="text"
          @change="onEntityName(($event.target as HTMLInputElement).value)"
        />
      </div>
      <div class="field mono id-line">
        <label>id</label>
        <span>{{ entity.id }}</span>
      </div>

      <!-- Wall -->
      <template v-if="wall">
        <div class="grid3">
          <div v-for="(axis, ai) in (['X', 'Y', 'Z'] as const)" :key="`s${axis}`" class="field">
            <label>start {{ axis }}</label>
            <input
              :value="wall.start[ai]"
              :disabled="disabled"
              type="number"
              step="0.05"
              @change="onWallVec('start', ai, ($event.target as HTMLInputElement).value)"
            />
          </div>
        </div>
        <div class="grid3">
          <div v-for="(axis, ai) in (['X', 'Y', 'Z'] as const)" :key="`e${axis}`" class="field">
            <label>end {{ axis }}</label>
            <input
              :value="wall.end[ai]"
              :disabled="disabled"
              type="number"
              step="0.05"
              @change="onWallVec('end', ai, ($event.target as HTMLInputElement).value)"
            />
          </div>
        </div>
        <div class="grid2">
          <div class="field">
            <label>height (m)</label>
            <input
              :value="wall.height"
              :disabled="disabled"
              type="number"
              step="0.05"
              min="0"
              @change="onWallField('height', ($event.target as HTMLInputElement).value)"
            />
          </div>
          <div class="field">
            <label>thickness (m)</label>
            <input
              :value="wall.thickness"
              :disabled="disabled"
              type="number"
              step="0.05"
              min="0"
              @change="onWallField('thickness', ($event.target as HTMLInputElement).value)"
            />
          </div>
        </div>

        <div class="faces">
          <div class="label">Faces</div>
          <div v-for="face in (['inner', 'outer', 'side'] as const)" :key="face" class="field">
            <label>{{ face }}</label>
            <select
              :value="wall.faces[face]"
              :disabled="disabled"
              @change="onWallFace(face, ($event.target as HTMLInputElement).value)"
            >
              <option v-for="mat in materials" :key="mat.id" :value="mat.id">
                {{ mat.id }}
              </option>
            </select>
          </div>
        </div>

        <div class="faces">
          <div class="label">
            Openings ({{ (wall.openings ?? []).length }})
          </div>
          <div class="add-row">
            <button :disabled="disabled" @click="addOpening('door')">+ Door</button>
            <button :disabled="disabled" @click="addOpening('window')">+ Window</button>
          </div>
          <div
            v-for="(op, oi) in wall.openings ?? []"
            :key="oi"
            class="opening"
          >
            <div class="opening-head">
              <select
                :value="op.type"
                :disabled="disabled"
                @change="
                  onOpening(oi, {
                    type: ($event.target as HTMLInputElement).value as WallOpeningType,
                  })
                "
              >
                <option value="door">door</option>
                <option value="window">window</option>
              </select>
              <button :disabled="disabled" title="Remove opening" @click="removeOpening(oi)">
                ×
              </button>
            </div>
            <div class="grid2">
              <div class="field">
                <label>offset</label>
                <input
                  :value="op.offset"
                  :disabled="disabled"
                  type="number"
                  step="0.05"
                  min="0"
                  @change="
                    onOpening(oi, {
                      offset: num(($event.target as HTMLInputElement).value, op.offset),
                    })
                  "
                />
              </div>
              <div class="field">
                <label>width</label>
                <input
                  :value="op.width"
                  :disabled="disabled"
                  type="number"
                  step="0.05"
                  min="0"
                  @change="
                    onOpening(oi, {
                      width: num(($event.target as HTMLInputElement).value, op.width),
                    })
                  "
                />
              </div>
              <div class="field">
                <label>height</label>
                <input
                  :value="op.height"
                  :disabled="disabled"
                  type="number"
                  step="0.05"
                  min="0"
                  @change="
                    onOpening(oi, {
                      height: num(($event.target as HTMLInputElement).value, op.height),
                    })
                  "
                />
              </div>
              <div class="field">
                <label>sill</label>
                <input
                  :value="op.sill ?? 0"
                  :disabled="disabled"
                  type="number"
                  step="0.05"
                  min="0"
                  @change="
                    onOpening(oi, {
                      sill: num(($event.target as HTMLInputElement).value, op.sill ?? 0),
                    })
                  "
                />
              </div>
            </div>
          </div>
        </div>
      </template>

      <!-- Solid box -->
      <template v-else-if="box">
        <div class="grid3">
          <div v-for="(axis, ai) in (['X', 'Y', 'Z'] as const)" :key="`o${axis}`" class="field">
            <label>origin {{ axis }}</label>
            <input
              :value="box.origin[ai]"
              :disabled="disabled"
              type="number"
              step="0.05"
              @change="onBoxVec('origin', ai, ($event.target as HTMLInputElement).value)"
            />
          </div>
        </div>
        <div class="grid3">
          <div v-for="(axis, ai) in (['X', 'Y', 'Z'] as const)" :key="`z${axis}`" class="field">
            <label>size {{ axis }}</label>
            <input
              :value="box.size[ai]"
              :disabled="disabled"
              type="number"
              step="0.05"
              min="0"
              @change="onBoxVec('size', ai, ($event.target as HTMLInputElement).value)"
            />
          </div>
        </div>
        <div class="field">
          <label>materialId</label>
          <select
            :value="box.materialId"
            :disabled="disabled"
            @change="onBoxMaterial(($event.target as HTMLInputElement).value)"
          >
            <option v-for="mat in materials" :key="mat.id" :value="mat.id">
              {{ mat.id }}
            </option>
          </select>
        </div>
      </template>

      <!-- Solid extrude -->
      <template v-else-if="extrude">
        <div class="field">
          <label>height (m)</label>
          <input
            :value="extrude.height"
            :disabled="disabled"
            type="number"
            step="0.05"
            min="0"
            @change="onExtrudeHeight(($event.target as HTMLInputElement).value)"
          />
        </div>
        <div class="field">
          <label>materialId</label>
          <select
            :value="extrude.materialId"
            :disabled="disabled"
            @change="onBoxMaterial(($event.target as HTMLInputElement).value)"
          >
            <option v-for="mat in materials" :key="mat.id" :value="mat.id">
              {{ mat.id }}
            </option>
          </select>
        </div>
        <p class="dim hint">
          polygon: {{ extrude.polygon.length }} pts (XY, extruded along +Z) —
          edit via scene JSON in v1.
        </p>
      </template>

      <!-- Void -->
      <template v-else-if="voidEnt">
        <div class="grid3">
          <div v-for="(axis, ai) in (['X', 'Y', 'Z'] as const)" :key="`vo${axis}`" class="field">
            <label>origin {{ axis }}</label>
            <input
              :value="voidEnt.origin[ai]"
              :disabled="disabled"
              type="number"
              step="0.05"
              @change="onVoidVec('origin', ai, ($event.target as HTMLInputElement).value)"
            />
          </div>
        </div>
        <div class="grid3">
          <div v-for="(axis, ai) in (['X', 'Y', 'Z'] as const)" :key="`vs${axis}`" class="field">
            <label>size {{ axis }}</label>
            <input
              :value="voidEnt.size[ai]"
              :disabled="disabled"
              type="number"
              step="0.05"
              min="0"
              @change="onVoidVec('size', ai, ($event.target as HTMLInputElement).value)"
            />
          </div>
        </div>
        <p class="dim hint">Void subtracts from overlapping wall faces at mesh time.</p>
      </template>

      <!-- Source / receiver: read-only in v1 (placement lives in Scenario flow). -->
      <template v-else>
        <p class="dim hint">
          {{ entity.type }} — placement entities are compiled by «В расчёт»;
          geometry editing is wall/box/void.
        </p>
      </template>
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

.field {
  display: flex;
  flex-direction: column;
  gap: 2px;
  margin-bottom: 6px;
  min-width: 0;
}

.field label {
  color: var(--text-dim);
  font-size: 10px;
  text-transform: uppercase;
  letter-spacing: 0.04em;
}

.field input,
.field select {
  width: 100%;
  min-width: 0;
}

.id-line {
  flex-direction: row;
  align-items: baseline;
  gap: 8px;
  word-break: break-all;
}

.grid2 {
  display: grid;
  grid-template-columns: 1fr 1fr;
  gap: 6px;
}

.grid3 {
  display: grid;
  grid-template-columns: 1fr 1fr 1fr;
  gap: 6px;
}

.faces {
  margin-top: 8px;
}

.label {
  color: var(--text-dim);
  font-size: 11px;
  text-transform: uppercase;
  letter-spacing: 0.04em;
  margin-bottom: 4px;
}

.add-row {
  display: flex;
  gap: 6px;
  margin-bottom: 6px;
}

.opening {
  border: 1px solid var(--border);
  border-radius: 4px;
  padding: 6px;
  margin-bottom: 6px;
}

.opening-head {
  display: flex;
  gap: 6px;
  margin-bottom: 4px;
}

.opening-head select {
  flex: 1;
}

.hint {
  margin: 6px 0 0;
  font-size: 11px;
}
</style>
