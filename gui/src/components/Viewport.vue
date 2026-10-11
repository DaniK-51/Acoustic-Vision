<script setup lang="ts">
import { onBeforeUnmount, onMounted, ref, watch } from 'vue';
import * as THREE from 'three';
import { OrbitControls } from 'three/addons/controls/OrbitControls.js';
import type { Scenario } from '../api/types';

// Read-only viewport stub (GUI1): grid + unit box scaled to a rect room when
// known. No editing gizmos — those land in GUI2/GUI3 slices.

const props = defineProps<{ scenario: Scenario | null }>();

const container = ref<HTMLDivElement | null>(null);

let renderer: THREE.WebGLRenderer | null = null;
let scene: THREE.Scene | null = null;
let camera: THREE.PerspectiveCamera | null = null;
let controls: OrbitControls | null = null;
let roomBox: THREE.LineSegments | null = null;
let resizeObserver: ResizeObserver | null = null;
let raf = 0;

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

  const markerGeo = new THREE.SphereGeometry(0.06, 12, 12);
  const srcMat = new THREE.MeshBasicMaterial({ color: 0xe6b84d });
  const rcvMat = new THREE.MeshBasicMaterial({ color: 0x3ecf8e });

  const placeMarkers = (): void => {
    if (!scene) {
      return;
    }
    for (const child of [...scene.children]) {
      if (child.userData.avMarker) {
        scene.remove(child);
      }
    }
    const sc = props.scenario;
    if (!sc) {
      return;
    }
    for (const src of sc.sources ?? []) {
      const m = new THREE.Mesh(markerGeo, srcMat);
      m.position.set(src.position[0], src.position[1], src.position[2]);
      m.userData.avMarker = 'source';
      scene.add(m);
    }
    for (const rcvDef of sc.receivers ?? []) {
      const m = new THREE.Mesh(markerGeo, rcvMat);
      m.position.set(rcvDef.position[0], rcvDef.position[1], rcvDef.position[2]);
      m.userData.avMarker = 'receiver';
      scene.add(m);
    }
  };
  placeMarkers();

  watch(
    () => props.scenario,
    () => {
      applyRoom();
      placeMarkers();
    },
    { deep: true },
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
  if (scene) {
    scene.traverse((obj) => {
      const mesh = obj as THREE.Mesh;
      if (mesh.geometry) {
        mesh.geometry.dispose();
      }
      const material = mesh.material as THREE.Material | THREE.Material[] | undefined;
      if (Array.isArray(material)) {
        material.forEach((m) => m.dispose());
      } else {
        material?.dispose();
      }
    });
  }
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
      Viewport stub — orbit only, no editing
      <span v-if="scenario?.room?.type === 'rect'"> · room box from scenario</span>
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
}

.overlay {
  position: absolute;
  left: 10px;
  bottom: 8px;
  font-size: 11px;
  pointer-events: none;
}
</style>
