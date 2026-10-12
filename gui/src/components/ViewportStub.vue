<script setup lang="ts">
import { onMounted, onUnmounted, ref, watch } from 'vue';
import * as THREE from 'three';
import { state } from '../store';

const host = ref<HTMLDivElement | null>(null);

let renderer: THREE.WebGLRenderer | null = null;
let scene: THREE.Scene | null = null;
let camera: THREE.PerspectiveCamera | null = null;
let roomMesh: THREE.Mesh | null = null;
let frameId = 0;

function applyRoomSize(): void {
  if (!roomMesh || !scene) {
    return;
  }
  const size = state.scenario?.room?.size;
  const w = size?.w ?? 6;
  const h = size?.h ?? 3;
  const d = size?.d ?? 4;
  roomMesh.geometry.dispose();
  roomMesh.geometry = new THREE.BoxGeometry(w, h, d);
  roomMesh.position.set(0, h / 2, 0);
}

function animate(): void {
  frameId = requestAnimationFrame(animate);
  if (renderer && scene && camera) {
    renderer.render(scene, camera);
  }
}

onMounted(() => {
  const el = host.value;
  if (!el) {
    return;
  }

  const width = el.clientWidth || 640;
  const height = el.clientHeight || 480;

  scene = new THREE.Scene();
  scene.background = new THREE.Color(0x12151c);

  camera = new THREE.PerspectiveCamera(50, width / height, 0.1, 200);
  camera.position.set(10, 8, 12);
  camera.lookAt(0, 1.5, 0);

  const grid = new THREE.GridHelper(20, 20, 0x2c3548, 0x222a3a);
  scene.add(grid);

  const ambient = new THREE.AmbientLight(0xffffff, 0.7);
  scene.add(ambient);
  const dir = new THREE.DirectionalLight(0xffffff, 0.6);
  dir.position.set(5, 10, 7);
  scene.add(dir);

  // Placeholder room box — replaced by real editor geometry in later GUI slices.
  const material = new THREE.MeshStandardMaterial({
    color: 0x4da3ff,
    transparent: true,
    opacity: 0.25,
    side: THREE.DoubleSide,
  });
  roomMesh = new THREE.Mesh(new THREE.BoxGeometry(6, 3, 4), material);
  roomMesh.position.set(0, 1.5, 0);
  scene.add(roomMesh);

  const edges = new THREE.LineSegments(
    new THREE.EdgesGeometry(roomMesh.geometry),
    new THREE.LineBasicMaterial({ color: 0x8b97ad }),
  );
  roomMesh.add(edges);

  renderer = new THREE.WebGLRenderer({ antialias: true });
  renderer.setSize(width, height);
  renderer.setPixelRatio(window.devicePixelRatio);
  el.appendChild(renderer.domElement);

  applyRoomSize();
  animate();
});

watch(
  () => state.scenario?.room?.size,
  () => {
    applyRoomSize();
  },
  { deep: true },
);

onUnmounted(() => {
  cancelAnimationFrame(frameId);
  renderer?.dispose();
  renderer = null;
  scene = null;
  camera = null;
  roomMesh = null;
});
</script>

<template>
  <div ref="host" class="viewport">
    <div class="badge">viewport stub — grid + placeholder room</div>
  </div>
</template>

<style scoped>
.viewport {
  position: relative;
  flex: 1;
  min-height: 320px;
  background: var(--panel);
  border: 1px solid var(--border);
  border-radius: 6px;
  overflow: hidden;
}

.viewport :deep(canvas) {
  display: block;
  width: 100%;
  height: 100%;
}

.badge {
  position: absolute;
  top: 8px;
  left: 8px;
  z-index: 1;
  padding: 3px 8px;
  border-radius: 4px;
  background: rgba(18, 21, 28, 0.75);
  color: var(--text-dim);
  font-size: 11px;
}
</style>
