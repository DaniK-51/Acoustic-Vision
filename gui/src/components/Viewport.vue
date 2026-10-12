<script setup lang="ts">
import { computed, onBeforeUnmount, onMounted, ref, watch } from 'vue';
import * as THREE from 'three';
import { OrbitControls } from 'three/addons/controls/OrbitControls.js';
import { TransformControls } from 'three/addons/controls/TransformControls.js';
import type { JobFieldSummary, JobRays, Scenario } from '../api/types';
import { fmtBand, materialById, materialColor, surfaceBindings } from '../materials';

// Interactive viewport: room box + source/receiver markers (rect rooms in
// particular), click select, drag on the camera screen plane (depth toward /
// away from the camera is preserved), TransformControls gizmo for axis-aligned
// X/Y/Z moves, numeric X/Y/Z inputs as the fallback. Moves are emitted to the
// shell, which debounces them into WS scene.patch — the viewport never talks
// to the API.
//
// Field heat-map: SpatialField values (linear energy, x-fastest index) drawn
// as a Points cloud. Levels shown as relative dB — 10*log10(v/max(values)),
// so 0 dB = grid maximum (legend states this).
//
// Ray paths: result.rays.paths[] polylines as one LineSegments cloud, vertex
// color by amplitude (blue = weak → red = strong). Capped at kMaxRayPaths so
// a large capturePathLimit cannot tank the frame rate.
//
// Materials (A1/A2): rect room faces are translucent quads colored by their
// material id (palette in materials.ts) with a legend overlay; mesh rooms get
// the legend only (no mesh geometry is loaded client-side). Clicking a face
// selects it and shows name + materialId + α(f) table.

export interface EntityRef {
  entityType: 'source' | 'receiver' | 'face';
  id: string;
}

const props = defineProps<{
  scenario: Scenario | null;
  selection: EntityRef | null;
  /** Full field grid from GET /jobs/{jobId}/field (null = no heat-map). */
  field: JobFieldSummary | null;
  /** Captured ray polylines from GET /jobs/{jobId}/result (null = no rays). */
  rays: JobRays | null;
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
const cameraDir = new THREE.Vector3();

// --- field heat-map ---
// Points cloud over the SpatialField grid. Linear energy values are shown as
// relative dB: 10*log10(v / max(values)) — 0 dB is the grid peak, documented
// in the legend. Cells below kFieldFloorDb are skipped so quiet space stays
// transparent instead of a solid fog of floor-colored points.
const showField = ref(true);
const legend = ref<{ minDb: number; maxDb: number; cells: number } | null>(null);
let fieldPoints: THREE.Points | null = null;

const kFieldFloorDb = -30;

function clearField(): void {
  if (fieldPoints && scene) {
    scene.remove(fieldPoints);
    fieldPoints.geometry.dispose();
    (fieldPoints.material as THREE.Material).dispose();
    fieldPoints = null;
  }
  legend.value = null;
}

/** Blue → red ramp over t in [0,1] (low → high level). */
function levelColor(t: number): THREE.Color {
  return new THREE.Color().setHSL((1 - t) * 0.66, 0.85, 0.5);
}

function rebuildField(): void {
  clearField();
  const field = props.field;
  if (!field || !scene) {
    return;
  }
  const { nx, ny, nz, values } = field;
  if (!values || nx <= 0 || ny <= 0 || nz <= 0) {
    return;
  }
  const cell = field.cellSize && field.cellSize > 0 ? field.cellSize : 1;
  const origin = field.origin ?? [0, 0, 0];
  let peak = 0;
  for (const v of values) {
    if (Number.isFinite(v) && v > peak) {
      peak = v;
    }
  }
  if (peak <= 0) {
    return;
  }

  const positions: number[] = [];
  const colors: number[] = [];
  let minDb = 0;
  let maxDb = 0;
  let first = true;
  // values index: x + nx*(y + ny*z) — x fastest (SpatialField contract).
  for (let z = 0; z < nz; z++) {
    for (let y = 0; y < ny; y++) {
      for (let x = 0; x < nx; x++) {
        const v = values[x + nx * (y + ny * z)];
        if (!Number.isFinite(v) || v <= 0) {
          continue;
        }
        const db = 10 * Math.log10(v / peak);
        if (db < kFieldFloorDb) {
          continue;
        }
        positions.push(
          origin[0] + (x + 0.5) * cell,
          origin[1] + (y + 0.5) * cell,
          origin[2] + (z + 0.5) * cell,
        );
        // Floor maps to 0 (blue), peak to 1 (red).
        const t = Math.min(1, Math.max(0, (db - kFieldFloorDb) / (0 - kFieldFloorDb)));
        const color = levelColor(t);
        colors.push(color.r, color.g, color.b);
        if (first) {
          minDb = db;
          maxDb = db;
          first = false;
        } else {
          if (db < minDb) {
            minDb = db;
          }
          if (db > maxDb) {
            maxDb = db;
          }
        }
      }
    }
  }
  if (positions.length === 0) {
    return;
  }

  const geometry = new THREE.BufferGeometry();
  geometry.setAttribute('position', new THREE.Float32BufferAttribute(positions, 3));
  geometry.setAttribute('color', new THREE.Float32BufferAttribute(colors, 3));
  const material = new THREE.PointsMaterial({
    size: Math.max(cell * 0.75, 0.02),
    vertexColors: true,
    transparent: true,
    opacity: 0.75,
    depthWrite: false,
    sizeAttenuation: true,
  });
  fieldPoints = new THREE.Points(geometry, material);
  fieldPoints.visible = showField.value;
  scene.add(fieldPoints);
  legend.value = { minDb, maxDb, cells: positions.length / 3 };
}

function toggleField(): void {
  showField.value = !showField.value;
  if (fieldPoints) {
    fieldPoints.visible = showField.value;
  }
}

// --- ray paths ---
// Polylines drawn as one LineSegments buffer (pair-wise segments). Vertex
// color follows amplitude (levelColor ramp over a normalized amplitude), so
// strong bounces read hot and weak ones recede. kMaxRayPaths matches the
// engine-side capturePathLimit default (64) and bounds the buffer size.
const showRays = ref(true);
const rayLegend = ref<{ shown: number; total: number } | null>(null);
let rayLines: THREE.LineSegments | null = null;

const kMaxRayPaths = 64;

function clearRays(): void {
  if (rayLines && scene) {
    scene.remove(rayLines);
    rayLines.geometry.dispose();
    (rayLines.material as THREE.Material).dispose();
    rayLines = null;
  }
  rayLegend.value = null;
}

function rebuildRays(): void {
  clearRays();
  const rays = props.rays;
  if (!rays || !scene) {
    return;
  }
  const paths = rays.paths ?? [];
  if (paths.length === 0) {
    return;
  }
  const shownPaths = paths.slice(0, kMaxRayPaths);

  // amplitude = sqrt(sourceEnergy * prod(1-alpha)) — absolute energy scale,
  // not (0,1]; color by level relative to the strongest captured path so the
  // ramp stays readable across engines/power settings.
  let maxAmp = 0;
  for (const path of shownPaths) {
    if (Number.isFinite(path.amplitude) && path.amplitude > maxAmp) {
      maxAmp = path.amplitude;
    }
  }

  const positions: number[] = [];
  const colors: number[] = [];
  for (const path of shownPaths) {
    const pts = path.points ?? [];
    const t = maxAmp > 0 ? Math.min(1, Math.max(0, path.amplitude / maxAmp)) : 0;
    const color = levelColor(t);
    for (let i = 0; i + 1 < pts.length; i++) {
      const a = pts[i];
      const b = pts[i + 1];
      if (!a || !b || a.length < 3 || b.length < 3) {
        continue;
      }
      positions.push(a[0], a[1], a[2], b[0], b[1], b[2]);
      colors.push(color.r, color.g, color.b, color.r, color.g, color.b);
    }
  }
  if (positions.length === 0) {
    return;
  }

  const geometry = new THREE.BufferGeometry();
  geometry.setAttribute('position', new THREE.Float32BufferAttribute(positions, 3));
  geometry.setAttribute('color', new THREE.Float32BufferAttribute(colors, 3));
  const material = new THREE.LineBasicMaterial({
    vertexColors: true,
    transparent: true,
    opacity: 0.4,
    depthWrite: false,
  });
  rayLines = new THREE.LineSegments(geometry, material);
  rayLines.visible = showRays.value;
  scene.add(rayLines);
  rayLegend.value = { shown: shownPaths.length, total: paths.length };
}

function toggleRays(): void {
  showRays.value = !showRays.value;
  if (rayLines) {
    rayLines.visible = showRays.value;
  }
}

// --- material faces (A1) + face info (A2) ---
// Rect rooms: one translucent quad per face, colored by its material id via
// the shared palette, so floor/ceiling/walls read apart at a glance. Mesh rooms
// get the legend only (mesh geometry is loaded server-side, not into three.js).
// Face click → selection with entityType 'face'; the overlay shows face name,
// materialId and the α(f) table of the bound material.
const kBoxFaceKeys = ['x+', 'x-', 'ceiling', 'floor', 'z+', 'z-'] as const;

let faceBox: THREE.Mesh | null = null;
const faceMats: THREE.MeshBasicMaterial[] = [];

const materialLegend = computed(() => {
  const sc = props.scenario;
  if (!sc) {
    return [] as Array<{ id: string; name: string; color: string; surfaces: string[] }>;
  }
  const bindings = surfaceBindings(sc);
  return sc.materials.map((mat) => ({
    id: mat.id,
    name: mat.name,
    color: materialColor(sc, mat.id),
    surfaces: bindings.filter((b) => b.materialId === mat.id).map((b) => b.surface),
  }));
});

const faceInfo = computed(() => {
  const sel = props.selection;
  const sc = props.scenario;
  if (!sel || sel.entityType !== 'face' || !sc) {
    return null;
  }
  const materialId =
    surfaceBindings(sc).find((b) => b.surface === sel.id)?.materialId ?? '';
  const material = materialById(sc, materialId);
  return {
    surface: sel.id,
    materialId: materialId || '(none)',
    materialName: material?.name ?? '',
    bandsHz: material?.bandsHz ?? [],
    absorption: material?.absorption ?? [],
  };
});

function clearFaces(): void {
  if (faceBox && scene) {
    scene.remove(faceBox);
    faceBox.geometry.dispose();
    faceBox = null;
  }
  for (const mat of faceMats) {
    mat.dispose();
  }
  faceMats.length = 0;
}

function rebuildFaces(): void {
  clearFaces();
  const sc = props.scenario;
  const room = sc?.room;
  if (!sc || !scene || room?.type !== 'rect' || !room.size) {
    return;
  }
  const { w, h, d } = room.size;
  const geometry = new THREE.BoxGeometry(w, h, d);
  // BoxGeometry group order: +x, -x, +y, -y, +z, -z ↔ kBoxFaceKeys.
  for (const key of kBoxFaceKeys) {
    const materialId = room.faces?.[key] ?? '';
    const color = materialId ? materialColor(sc, materialId) : '#8b95a7';
    faceMats.push(
      new THREE.MeshBasicMaterial({
        color: new THREE.Color(color),
        transparent: true,
        opacity: 0.28,
        side: THREE.DoubleSide,
        depthWrite: false,
      }),
    );
  }
  faceBox = new THREE.Mesh(geometry, faceMats);
  faceBox.position.set(w / 2, h / 2, d / 2);
  scene.add(faceBox);
  syncFaceHighlight();
}

/** Selected face reads hotter (brighter, more opaque) than the rest. */
function syncFaceHighlight(): void {
  const sel = props.selection;
  const selectedFace = sel && sel.entityType === 'face' ? sel.id : '';
  kBoxFaceKeys.forEach((key, i) => {
    const mat = faceMats[i];
    if (!mat) {
      return;
    }
    const base = new THREE.Color(
      materialColor(props.scenario, props.scenario?.room.faces?.[key] ?? ''),
    );
    if (key === selectedFace) {
      mat.color.copy(base).lerp(new THREE.Color(0xffffff), 0.35);
      mat.opacity = 0.55;
    } else {
      mat.color.copy(base);
      mat.opacity = 0.28;
    }
  });
}

function hitFace(): string | null {
  if (!scene || !camera || !faceBox) {
    return null;
  }
  raycaster.setFromCamera(pointer, camera);
  const hits = raycaster.intersectObject(faceBox, false);
  const hit = hits[0];
  if (!hit) {
    return null;
  }
  // BoxGeometry groups carry their material index in the hit face — that is
  // exactly kBoxFaceKeys order. faceIndex/2 is the same mapping (2 triangles
  // per side) and kept as the fallback for unindexed hits.
  const index = hit.face?.materialIndex ?? (hit.faceIndex !== undefined ? Math.floor(hit.faceIndex / 2) : -1);
  return kBoxFaceKeys[index] ?? null;
}

// Drag state (null = orbit controls own the pointer).
let dragKey = '';
let gizmoKey = '';
// True while the active pointer gesture belongs to the gizmo (so pointerup
// must not treat it as an empty-space click and clear the selection).
let gizmoPointer = false;
let pointerDownAt = { x: 0, y: 0 };
let pointerMoved = false;

// R1: optional grid snap for marker drag and gizmo translation.
const snapEnabled = ref(false);
const kSnapStep = 0.25;
let transformControls: TransformControls | null = null;

function snapCoord(value: number): number {
  return Math.round(value / kSnapStep) * kSnapStep;
}

function applySnapSetting(): void {
  if (!transformControls) {
    return;
  }
  transformControls.setTranslationSnap(snapEnabled.value ? kSnapStep : null);
}

function keyOf(ref: EntityRef): string {
  return `${ref.entityType}:${ref.id}`;
}

function selectionKey(): string {
  return props.selection ? keyOf(props.selection) : '';
}

function parseKey(key: string): EntityRef | null {
  const sep = key.indexOf(':');
  if (sep <= 0) {
    return null;
  }
  return {
    entityType: key.slice(0, sep) as EntityRef['entityType'],
    id: key.slice(sep + 1),
  };
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
  rebuildFaces();
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
    // While dragging (plane or gizmo), the mesh follows the pointer;
    // props lag one debounce.
    if (key !== dragKey && key !== gizmoKey) {
      mesh.position.set(entry.pos[0], entry.pos[1], entry.pos[2]);
    }
    mesh.material = key === sel ? matSelected : (entry.src ? matSrc : matRcv);
    mesh.scale.setScalar(key === sel ? 1.35 : 1);
  }
}

function selectedPos(): [number, number, number] | null {
  const sel = props.selection;
  const sc = props.scenario;
  if (!sel || !sc || sel.entityType === 'face') {
    return null;
  }
  const list = sel.entityType === 'source' ? sc.sources : sc.receivers;
  const entity = (list ?? []).find((e) => e.id === sel.id);
  return entity ? entity.position : null;
}

const posX = computed(() => selectedPos()?.[0] ?? 0);
const posY = computed(() => selectedPos()?.[1] ?? 0);
const posZ = computed(() => selectedPos()?.[2] ?? 0);

function emitMove(x: number, y: number, z: number): void {
  const sel = props.selection;
  const pos = selectedPos();
  if (!sel || !pos) {
    return;
  }
  emit('move', sel, [x, y, z]);
}

function onNumInput(axis: 0 | 1 | 2, event: Event): void {
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
  emitMove(next[0], next[1], next[2]);
}

/** Keep the gizmo attached to the selected marker (or detach on clear). */
function syncGizmo(): void {
  if (!transformControls) {
    return;
  }
  const key = selectionKey();
  const mesh = key ? markers.get(key) : null;
  if (mesh) {
    transformControls.attach(mesh);
  } else {
    transformControls.detach();
    // detach() leaves a stale hover axis; clear it so marker clicks work.
    transformControls.axis = null;
    gizmoKey = '';
  }
}

function onGizmoDraggingChanged(event: { value?: unknown }): void {
  const dragging = Boolean(event.value);
  if (controls) {
    controls.enabled = !dragging;
  }
  if (dragging) {
    const obj = transformControls?.object as THREE.Mesh | undefined;
    gizmoKey = obj ? String(obj.userData.avKey ?? '') : '';
  } else {
    gizmoKey = '';
  }
}

function onGizmoObjectChange(): void {
  if (!transformControls?.dragging) {
    return;
  }
  const obj = transformControls.object as THREE.Mesh | undefined;
  if (!obj) {
    return;
  }
  const ref = parseKey(String(obj.userData.avKey ?? ''));
  if (!ref) {
    return;
  }
  emit('move', ref, [obj.position.x, obj.position.y, obj.position.z]);
}

function onPointerDown(event: PointerEvent): void {
  if (event.button !== 0) {
    return;
  }
  // Gizmo arrows own the pointer when hovered or already dragging.
  // `axis` is only meaningful while an object is attached (stale otherwise).
  if (transformControls && transformControls.dragging) {
    gizmoPointer = true;
    return;
  }
  if (transformControls?.object && transformControls.axis) {
    gizmoPointer = true;
    return;
  }
  gizmoPointer = false;
  setPointer(event);
  pointerDownAt = { x: event.clientX, y: event.clientY };
  pointerMoved = false;
  const mesh = hitMarker();
  if (!mesh) {
    return;
  }
  const key = String(mesh.userData.avKey ?? '');
  const ref = parseKey(key);
  if (!ref) {
    return;
  }
  emit('select', ref);
  dragKey = key;
  // Camera-facing plane through the marker: screen-plane drag, depth
  // toward / away from the camera stays fixed for the whole gesture.
  if (camera) {
    camera.getWorldDirection(cameraDir);
    dragPlane.setFromNormalAndCoplanarPoint(cameraDir, mesh.position);
  }
  if (controls) {
    controls.enabled = false;
  }
  if (transformControls) {
    transformControls.enabled = false;
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
  if (!raycaster.ray.intersectPlane(dragPlane, planeHit)) {
    return;
  }
  let x = planeHit.x;
  let y = planeHit.y;
  let z = planeHit.z;
  if (snapEnabled.value) {
    x = snapCoord(x);
    y = snapCoord(y);
    z = snapCoord(z);
  }
  const mesh = markers.get(dragKey);
  if (mesh) {
    mesh.position.set(x, y, z);
  }
  const ref = parseKey(dragKey);
  if (ref) {
    emit('move', ref, [x, y, z]);
  }
}

function onPointerUp(event: PointerEvent): void {
  if (gizmoPointer) {
    // Gizmo gesture: leave selection alone (a click on a handle is not a
    // click on empty space).
    gizmoPointer = false;
    return;
  }
  if (dragKey) {
    renderer?.domElement.releasePointerCapture(event.pointerId);
    dragKey = '';
    if (controls) {
      controls.enabled = true;
    }
    if (transformControls) {
      transformControls.enabled = true;
    }
    return;
  }
  // Click without drag: face hit selects that face (A2), otherwise the click
  // is empty space and clears the selection (orbit drags are ignored).
  if (!pointerMoved) {
    setPointer(event);
    if (!hitMarker()) {
      const face = hitFace();
      if (face) {
        emit('select', { entityType: 'face', id: face });
      } else {
        emit('select', null);
      }
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

  // Axis gizmo (R1/C2): translate-only; hands orbit back on drag end.
  transformControls = new TransformControls(camera, renderer.domElement);
  transformControls.setMode('translate');
  transformControls.addEventListener('dragging-changed', onGizmoDraggingChanged);
  transformControls.addEventListener('objectChange', onGizmoObjectChange);
  scene.add(transformControls.getHelper());
  applySnapSetting();
  syncGizmo();

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
  rebuildField();
  rebuildRays();

  const canvas = renderer.domElement;
  canvas.addEventListener('pointerdown', onPointerDown);
  canvas.addEventListener('pointermove', onPointerMove);
  canvas.addEventListener('pointerup', onPointerUp);

  watch(
    () => props.scenario,
    () => {
      applyRoom();
      syncMarkers();
      // Markers may have been recreated (or removed) — re-attach or detach.
      syncGizmo();
    },
    { deep: true },
  );
  watch(
    () => selectionKey(),
    () => {
      syncMarkers();
      syncGizmo();
      syncFaceHighlight();
    },
  );
  watch(snapEnabled, () => applySnapSetting());
  watch(
    () => props.field,
    () => rebuildField(),
  );
  watch(
    () => props.rays,
    () => rebuildRays(),
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
  clearField();
  clearRays();
  clearFaces();
  if (transformControls) {
    transformControls.detach();
    if (scene) {
      scene.remove(transformControls.getHelper());
    }
    transformControls.dispose();
    transformControls = null;
  }
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
      Click marker or face to select · drag on the screen plane (no depth
      change) · gizmo arrows move X/Y/Z · orbit with empty drag
      <span v-if="scenario?.room?.type === 'rect'"> · room box from scenario</span>
    </div>
    <div class="tools-box">
      <label>
        <input v-model="snapEnabled" type="checkbox" />
        Snap {{ kSnapStep }} m
      </label>
    </div>
    <div v-if="field" class="field-box">
      <label>
        <input type="checkbox" :checked="showField" @change="toggleField" />
        Field
      </label>
      <template v-if="showField && legend">
        <div class="legend-bar" aria-hidden="true"></div>
        <span class="mono legend-text">
          {{ legend.minDb.toFixed(1) }} … {{ legend.maxDb.toFixed(1) }} dB rel
          (0 = max)
        </span>
        <span class="dim">{{ legend.cells }} cells</span>
      </template>
      <span v-else-if="showField" class="dim">no cells above floor</span>
    </div>
    <div v-if="rayLegend" class="rays-box">
      <label>
        <input type="checkbox" :checked="showRays" @change="toggleRays" />
        Rays
      </label>
      <template v-if="showRays">
        <span class="mono legend-text">
          {{ rayLegend.shown }}/{{ rayLegend.total }} paths · color = rel. amplitude
        </span>
      </template>
    </div>
    <div v-if="materialLegend.length > 0" class="mats-box">
      <div class="mats-title">Materials</div>
      <div v-for="row in materialLegend" :key="row.id" class="mat-row">
        <span class="swatch" :style="{ background: row.color }"></span>
        <span class="mono">{{ row.id }}</span>
        <span class="dim mat-surfaces">
          {{ row.surfaces.length > 0 ? row.surfaces.join(', ') : '(unused)' }}
        </span>
      </div>
    </div>
    <div v-if="faceInfo" class="face-box">
      <div class="mats-title">Face</div>
      <div class="mono">
        {{ faceInfo.surface }}
        <span class="dim">→</span>
        {{ faceInfo.materialId }}
      </div>
      <div v-if="faceInfo.materialName" class="dim">{{ faceInfo.materialName }}</div>
      <table class="alpha-table">
        <thead>
          <tr>
            <th v-for="band in faceInfo.bandsHz" :key="band">{{ fmtBand(band) }}</th>
          </tr>
        </thead>
        <tbody>
          <tr>
            <td
              v-for="(alpha, i) in faceInfo.absorption"
              :key="faceInfo.bandsHz[i] ?? i"
              class="mono"
            >
              {{ alpha.toFixed(2) }}
            </td>
          </tr>
        </tbody>
      </table>
    </div>
    <div v-if="selection && selection.entityType !== 'face'" class="sel-box">
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
        Y
        <input
          :value="posY"
          type="number"
          step="0.1"
          @change="onNumInput(1, $event)"
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

.field-box {
  position: absolute;
  top: 8px;
  left: 8px;
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

.field-box label {
  display: flex;
  align-items: center;
  gap: 4px;
}

.rays-box {
  position: absolute;
  top: 48px;
  left: 8px;
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

.rays-box label {
  display: flex;
  align-items: center;
  gap: 4px;
}

.legend-bar {
  width: 72px;
  height: 8px;
  border-radius: 2px;
  background: linear-gradient(to right, #2b5cff, #2ec7b8, #e8d44d, #e23b2e);
}

.legend-text {
  font-size: 10px;
}

.tools-box {
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

.tools-box label {
  display: flex;
  align-items: center;
  gap: 4px;
}

.sel-box {
  position: absolute;
  top: 48px;
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

.mats-box {
  position: absolute;
  bottom: 28px;
  right: 8px;
  max-width: 240px;
  padding: 6px 10px;
  background: var(--bg-panel);
  border: 1px solid var(--border);
  border-radius: 6px;
  font-size: 11px;
  pointer-events: auto;
}

.mats-title {
  color: var(--text-dim);
  font-size: 10px;
  text-transform: uppercase;
  letter-spacing: 0.04em;
  margin-bottom: 4px;
}

.mat-row {
  display: flex;
  align-items: baseline;
  gap: 6px;
  line-height: 1.5;
}

.mat-row .mono {
  font-size: 10px;
}

.mat-surfaces {
  font-size: 10px;
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}

.swatch {
  display: inline-block;
  width: 10px;
  height: 10px;
  border-radius: 2px;
  flex: none;
}

.face-box {
  position: absolute;
  bottom: 28px;
  left: 8px;
  max-width: 280px;
  padding: 6px 10px;
  background: var(--bg-panel);
  border: 1px solid var(--border);
  border-radius: 6px;
  font-size: 11px;
  pointer-events: auto;
}

.alpha-table {
  margin-top: 4px;
  width: auto;
  font-size: 10px;
}

.alpha-table th,
.alpha-table td {
  padding: 2px 6px;
  text-align: right;
  border-bottom: none;
}
</style>
