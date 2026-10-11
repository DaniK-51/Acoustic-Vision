<script setup lang="ts">
import { computed, onBeforeUnmount, onMounted, ref, watch } from 'vue';
import * as THREE from 'three';
import { OrbitControls } from 'three/addons/controls/OrbitControls.js';
import type { Scenario } from '../api/types';

// Interactive viewport: room box + source/receiver markers (rect rooms in
// particular), click select, drag on the horizontal plane (X/Z, Y kept),
// numeric X/Z inputs as the fallback. Moves are emitted to the shell, which
// debounces them into WS scene.patch — the viewport never talks to the API.

export interface EntityRef {
  entityType: 'source' | 'receiver';
  id: string;
}

const props = defineProps<{
  scenario: Scenario | null;
  selection: EntityRef | null;
}>();

const emit = defineEmits<{
  select: [EntityRef | null];
  move: [EntityRef, [number, number, number]];
}>();

const container = ref<HTMLDivElement | null>(null);

let renderer: THREE.WebGLRenderer | null = null;
let scene: THREE.Scene | null = null;
let camera: THREE.PerspectiveCamera | null = null;
let controls: OrbitControls | null = null;
let roomBox: THREE.LineSegments | null = null;
let resizeObserver: ResizeObserver | null = null;
let raf = 0;

const markerGeoSrc = new THREE.SphereGeometry(0.09, 16, 16);
const markerGeoRcv = new THREE.BoxGeometry(0.14, 0.14, 0.14);
const markers = new Map<string, THREE.Mesh>();
const matSrc = new THREE.MeshBasicMaterial({ color: 0xe6b84d });
const matRcv = new THREE.MeshBasicMaterial({ color: 0x3ecf8e });
const matSelected = new THREE.MeshBasicMaterial({ color: 0x4f8cff });

const raycaster = new THREE.Raycaster();
const pointer = new THREE.Vector2();
const dragPlane = new THREE.Plane(new THREE.Vector3(0, 1, 0), 0);
const planeHit = new THREE.Vector3();

// Drag state (null = orbit controls own the pointer).
let dragKey = '';
let dragY = 0;
let pointerDownAt = { x: 0, y: 0 };
let pointerMoved = false;

function keyOf(ref: EntityRef): string {
  return `${ref.entityType}:${ref.id}`;
}

function selectionKey(): string {
  return props.selection ? keyOf(props.selection) : '';
}

function setPointer(event: PointerEvent): void {
  const el = renderer?.domElement;
  if (!el) {
    return;
  }
  const rect = el.getBoundingClientRect();
  pointer.x = ((event.clientX - rect.left) / rect.width) * 2 - 1;
  pointer.y = -((event.clientY - rect.top) / rect.height) * 2 + 1;
}

function hitMarker(): THREE.Mesh | null {
  if (!scene || !camera) {
    return null;
  }
  raycaster.setFromCamera(pointer, camera);
  const hits = raycaster.intersectObjects([...markers.values()], false);
  return hits.length > 0 ? hits[0].object : null;
}

function applyRoom(): void {
  if (!roomBox) {
    return;
  }
  const room = props.scenario?.room;
  const size =
    room?.type === 'rect' && room.size
      ? { w: room.size.w, h: room.size.h, d: room.size.d }
      : { w: 1, h: 1, d: 1 };
  roomBox.scale.set(size.w, size.h, size.d);
  roomBox.position.set(size.w / 2, size.h / 2, size.d / 2);
}

function syncMarkers(): void {
  if (!scene) {
    return;
  }
  const wanted = new Map<string, { pos: [number, number, number]; src: boolean }>();
  const sc = props.scenario;
  if (sc) {
    for (const src of sc.sources ?? []) {
      wanted.set(`source:${src.id}`, { pos: src.position, src: true });
    }
    for (const rcv of sc.receivers ?? []) {
      wanted.set(`receiver:${rcv.id}`, { pos: rcv.position, src: false });
    }
  }
  for (const [key, mesh] of [...markers]) {
    if (!wanted.has(key)) {
      scene.remove(mesh);
      markers.delete(key);
    }
  }
  const sel = selectionKey();
  for (const [key, entry] of wanted) {
    let mesh = markers.get(key);
    if (!mesh) {
      mesh = new THREE.Mesh(entry.src ? markerGeoSrc : markerGeoRcv,
                            entry.src ? matSrc : matRcv);
      mesh.userData.avKey = key;
      markers.set(key, mesh);
      scene.add(mesh);
    }
    // While dragging, the mesh follows the pointer; props lag one debounce.
    if (key !== dragKey) {
      mesh.position.set(entry.pos[0], entry.pos[1], entry.pos[2]);
    }
    mesh.material = key === sel ? matSelected : (entry.src ? matSrc : matRcv);
    mesh.scale.setScalar(key === sel ? 1.35 : 1);
  }
}

function selectedPos(): [number, number, number] | null {
  const sel = props.selection;
  const sc = props.scenario;
  if (!sel || !sc) {
    return null;
  }
  const list = sel.entityType === 'source' ? sc.sources : sc.receivers;
  const entity = (list ?? []).find((e) => e.id === sel.id);
  return entity ? entity.position : null;
}

const posX = computed(() => selectedPos()?.[0] ?? 0);
const posZ = computed(() => selectedPos()?.[2] ?? 0);

function emitMove(x: number, z: number): void {
  const sel = props.selection;
  const pos = selectedPos();
  if (!sel || !pos) {
    return;
  }
  emit('move', sel, [x, pos[1], z]);
}

function onNumInput(axis: 0 | 2, event: Event): void {
  const raw = (event.target as HTMLInputElement).value;
  const value = Number(raw);
  if (!Number.isFinite(value)) {
    return;
  }
  const pos = selectedPos();
  if (!pos) {
    return;
  }
  const next: [number, number, number] = [pos[0], pos[1], pos[2]];
  next[axis] = value;
  emitMove(next[0], next[2]);
}

function onPointerDown(event: PointerEvent): void {
  if (event.button !== 0) {
    return;
  }
  setPointer(event);
  pointerDownAt = { x: event.clientX, y: event.clientY };
  pointerMoved = false;
  const mesh = hitMarker();
  if (!mesh) {
    return;
  }
  const key = String(mesh.userData.avKey ?? '');
  const sep = key.indexOf(':');
  if (sep <= 0) {
    return;
  }
  const ref: EntityRef = {
    entityType: key.slice(0, sep) as EntityRef['entityType'],
    id: key.slice(sep + 1),
  };
  emit('select', ref);
  dragKey = key;
  dragY = mesh.position.y;
  if (controls) {
    controls.enabled = false;
  }
  renderer?.domElement.setPointerCapture(event.pointerId);
}

function onPointerMove(event: PointerEvent): void {
  if (
    Math.abs(event.clientX - pointerDownAt.x) > 3 ||
    Math.abs(event.clientY - pointerDownAt.y) > 3
  ) {
    pointerMoved = true;
  }
  if (!dragKey || !camera) {
    return;
  }
  setPointer(event);
  raycaster.setFromCamera(pointer, camera);
  dragPlane.constant = -dragY;
  if (!raycaster.ray.intersectPlane(dragPlane, planeHit)) {
    return;
  }
  const mesh = markers.get(dragKey);
  if (mesh) {
    mesh.position.set(planeHit.x, dragY, planeHit.z);
  }
  const sep = dragKey.indexOf(':');
  emit(
    'move',
    {
      entityType: dragKey.slice(0, sep) as EntityRef['entityType'],
      id: dragKey.slice(sep + 1),
    },
    [planeHit.x, dragY, planeHit.z],
  );
}

function onPointerUp(event: PointerEvent): void {
  if (dragKey) {
    renderer?.domElement.releasePointerCapture(event.pointerId);
    dragKey = '';
    if (controls) {
      controls.enabled = true;
    }
    return;
  }
  // Click on empty space clears the selection (orbit drags are ignored).
  if (!pointerMoved) {
    setPointer(event);
    if (!hitMarker()) {
      emit('select', null);
    }
  }
}

function onResize(): void {
  const el = container.value;
  if (!el || !renderer || !camera) {
    return;
  }
  const width = Math.max(el.clientWidth, 1);
  const height = Math.max(el.clientHeight, 1);
  renderer.setSize(width, height, false);
  camera.aspect = width / height;
  camera.updateProjectionMatrix();
}

onMounted(() => {
  const el = container.value;
  if (!el) {
    return;
  }

  scene = new THREE.Scene();
  scene.background = new THREE.Color(0x0e1116);

  camera = new THREE.PerspectiveCamera(50, 1, 0.1, 200);
  camera.position.set(8, 6, 10);

  renderer = new THREE.WebGLRenderer({ antialias: true });
  renderer.setPixelRatio(window.devicePixelRatio);
  el.appendChild(renderer.domElement);
  onResize();

  controls = new OrbitControls(camera, renderer.domElement);
  controls.target.set(2, 1, 2);
  controls.enableDamping = true;
  controls.update();

  const grid = new THREE.GridHelper(24, 24, 0x3a4558, 0x252c38);
  grid.position.y = 0;
  scene.add(grid);

  // Unit box wireframe; scaled to the loaded rect room via applyRoom().
  const unitBox = new THREE.BoxGeometry(1, 1, 1);
  const edges = new THREE.EdgesGeometry(unitBox);
  roomBox = new THREE.LineSegments(
    edges,
    new THREE.LineBasicMaterial({ color: 0x4f8cff }),
  );
  unitBox.dispose();
  scene.add(roomBox);
  applyRoom();
  syncMarkers();

  const canvas = renderer.domElement;
  canvas.addEventListener('pointerdown', onPointerDown);
  canvas.addEventListener('pointermove', onPointerMove);
  canvas.addEventListener('pointerup', onPointerUp);

  watch(
    () => props.scenario,
    () => {
      applyRoom();
      syncMarkers();
    },
    { deep: true },
  );
  watch(
    () => selectionKey(),
    () => syncMarkers(),
  );

  resizeObserver = new ResizeObserver(() => onResize());
  resizeObserver.observe(el);

  const tick = (): void => {
    raf = requestAnimationFrame(tick);
    controls?.update();
    if (renderer && scene && camera) {
      renderer.render(scene, camera);
    }
  };
  tick();
});

onBeforeUnmount(() => {
  cancelAnimationFrame(raf);
  resizeObserver?.disconnect();
  resizeObserver = null;
  controls?.dispose();
  controls = null;
  if (renderer) {
    renderer.domElement.removeEventListener('pointerdown', onPointerDown);
    renderer.domElement.removeEventListener('pointermove', onPointerMove);
    renderer.domElement.removeEventListener('pointerup', onPointerUp);
  }
  if (scene) {
    scene.traverse((obj) => {
      const mesh = obj as THREE.Mesh;
      if (mesh.geometry && mesh.geometry !== markerGeoSrc && mesh.geometry !== markerGeoRcv) {
        mesh.geometry.dispose();
      }
      const material = mesh.material as THREE.Material | THREE.Material[] | undefined;
      if (Array.isArray(material)) {
        material.forEach((m) => {
          if (m !== matSrc && m !== matRcv && m !== matSelected) {
            m.dispose();
          }
        });
      } else if (material && material !== matSrc && material !== matRcv && material !== matSelected) {
        material.dispose();
      }
    });
  }
  markerGeoSrc.dispose();
  markerGeoRcv.dispose();
  matSrc.dispose();
  matRcv.dispose();
  matSelected.dispose();
  markers.clear();
  renderer?.dispose();
  renderer?.domElement.remove();
  renderer = null;
  scene = null;
  camera = null;
  roomBox = null;
});
</script>

<template>
  <div class="viewport">
    <div ref="container" class="canvas-host"></div>
    <div class="overlay dim">
      Click to select · drag on the plane to move (X/Z) · orbit with empty drag
      <span v-if="scenario?.room?.type === 'rect'"> · room box from scenario</span>
    </div>
    <div v-if="selection" class="sel-box">
      <span class="mono">{{ selection.entityType }} {{ selection.id }}</span>
      <label>
        X
        <input
          :value="posX"
          type="number"
          step="0.1"
          @change="onNumInput(0, $event)"
        />
      </label>
      <label>
        Z
        <input
          :value="posZ"
          type="number"
          step="0.1"
          @change="onNumInput(2, $event)"
        />
      </label>
    </div>
  </div>
</template>

<style scoped>
.viewport {
  position: relative;
  flex: 1;
  min-height: 280px;
  background: var(--bg-inset);
  border: 1px solid var(--border);
  border-radius: 6px;
  overflow: hidden;
}

.canvas-host {
  position: absolute;
  inset: 0;
}

.canvas-host :deep(canvas) {
  display: block;
  width: 100%;
  height: 100%;
  touch-action: none;
}

.overlay {
  position: absolute;
  left: 10px;
  bottom: 8px;
  font-size: 11px;
  pointer-events: none;
}

.sel-box {
  position: absolute;
  top: 8px;
  right: 8px;
  display: flex;
  align-items: center;
  gap: 8px;
  padding: 6px 10px;
  background: var(--bg-panel);
  border: 1px solid var(--border);
  border-radius: 6px;
  font-size: 11px;
  pointer-events: auto;
}

.sel-box label {
  display: flex;
  align-items: center;
  gap: 4px;
}

.sel-box input {
  width: 72px;
  padding: 2px 4px;
  background: var(--bg-inset);
  border: 1px solid var(--border);
  border-radius: 4px;
  color: inherit;
  font: inherit;
}
</style>
