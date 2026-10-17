// Room-editor Scene document helpers (V0.2). Factories produce valid
// schemas/scene.schema.json shapes so create/put never trip validateScene.
import type {
  EditorEntity,
  EditorGeometryEntity,
  EditorMaterial,
  EditorMeshSettings,
  EditorScene,
  SolidBoxEntity,
  SolidExtrudeEntity,
  Vec3,
  VoidEntity,
  WallEntity,
} from './api/types';

export const kDefaultBandsHz = [125, 250, 500, 1000, 2000, 4000];

/** Starter materials so a new scene is immediately usable. */
export function defaultEditorMaterials(): EditorMaterial[] {
  return [
    {
      id: 'mat-brick',
      name: 'Painted brick',
      bandsHz: [...kDefaultBandsHz],
      absorption: [0.1, 0.1, 0.12, 0.15, 0.15, 0.2],
      scattering: 0.1,
    },
    {
      id: 'mat-plaster',
      name: 'Plasterboard',
      bandsHz: [...kDefaultBandsHz],
      absorption: [0.1, 0.1, 0.15, 0.2, 0.2, 0.2],
      scattering: 0.1,
    },
    {
      id: 'mat-carpet',
      name: 'Carpet on concrete',
      bandsHz: [...kDefaultBandsHz],
      absorption: [0.1, 0.2, 0.3, 0.4, 0.4, 0.4],
      scattering: 0.1,
    },
  ];
}

export function defaultMeshSettings(): EditorMeshSettings {
  return {
    surface: { algorithm: 'auto', quality: 'draft' },
    volume: { algorithm: 'hex-box', targetSize: 0.15, enabled: false },
  };
}

/** Empty local document (not yet POSTed — id is assigned by the API). */
export function createEmptyEditorScene(): EditorScene {
  return {
    schemaVersion: 1,
    units: 'm',
    materials: defaultEditorMaterials(),
    entities: [],
    sources: [],
    receivers: [],
    meshSettings: defaultMeshSettings(),
  };
}

/** Closed 6×2.8×4 m shoebox (4 walls + floor/ceiling slabs) — rect-branch ready. */
export function createDefaultEditorScene(): EditorScene {
  const w = 6.0;
  const d = 4.0;
  const h = 2.8;
  const t = 0.2;
  return {
    schemaVersion: 1,
    units: 'm',
    materials: defaultEditorMaterials(),
    entities: [
      {
        type: 'wall',
        id: 'wall-south',
        name: 'South',
        start: [0, 0, 0],
        end: [w, 0, 0],
        height: h,
        thickness: t,
        faces: { inner: 'mat-brick', outer: 'mat-brick', side: 'mat-plaster' },
        openings: [],
      },
      {
        type: 'wall',
        id: 'wall-east',
        name: 'East',
        start: [w, 0, 0],
        end: [w, d, 0],
        height: h,
        thickness: t,
        faces: { inner: 'mat-brick', outer: 'mat-brick', side: 'mat-plaster' },
        openings: [],
      },
      {
        type: 'wall',
        id: 'wall-north',
        name: 'North',
        start: [w, d, 0],
        end: [0, d, 0],
        height: h,
        thickness: t,
        faces: { inner: 'mat-brick', outer: 'mat-brick', side: 'mat-plaster' },
        openings: [],
      },
      {
        type: 'wall',
        id: 'wall-west',
        name: 'West',
        start: [0, d, 0],
        end: [0, 0, 0],
        height: h,
        thickness: t,
        faces: { inner: 'mat-brick', outer: 'mat-brick', side: 'mat-plaster' },
        openings: [],
      },
      {
        type: 'solid_box',
        id: 'slab-floor',
        name: 'Floor',
        origin: [0, 0, -0.1],
        size: [w, d, 0.1],
        materialId: 'mat-carpet',
      },
      {
        type: 'solid_box',
        id: 'slab-ceiling',
        name: 'Ceiling',
        origin: [0, 0, h],
        size: [w, d, 0.1],
        materialId: 'mat-plaster',
      },
    ],
    sources: [],
    receivers: [],
    meshSettings: defaultMeshSettings(),
  };
}

export function isGeometryEntity(
  entity: EditorEntity,
): entity is EditorGeometryEntity {
  return (
    entity.type === 'wall' ||
    entity.type === 'solid_box' ||
    entity.type === 'solid_extrude' ||
    entity.type === 'void'
  );
}

export function isWall(entity: EditorEntity): entity is WallEntity {
  return entity.type === 'wall';
}

export function isSolidBox(entity: EditorEntity): entity is SolidBoxEntity {
  return entity.type === 'solid_box';
}

export function isSolidExtrude(
  entity: EditorEntity,
): entity is SolidExtrudeEntity {
  return entity.type === 'solid_extrude';
}

export function isVoid(entity: EditorEntity): entity is VoidEntity {
  return entity.type === 'void';
}

export function entityLabel(entity: EditorEntity): string {
  return entity.name?.trim() || entity.id;
}

function uniqueId(base: string, taken: ReadonlySet<string>): string {
  if (!taken.has(base)) {
    return base;
  }
  let n = 2;
  while (taken.has(`${base}-${n}`)) {
    n++;
  }
  return `${base}-${n}`;
}

function idsOf(scene: EditorScene): Set<string> {
  return new Set(scene.entities.map((e) => e.id));
}

export function makeWall(scene: EditorScene): WallEntity {
  const id = uniqueId('wall', idsOf(scene));
  return {
    type: 'wall',
    id,
    name: id,
    start: [0, 0, 0],
    end: [3, 0, 0],
    height: 2.8,
    thickness: 0.2,
    faces: {
      inner: scene.materials[0]?.id ?? 'mat-brick',
      outer: scene.materials[0]?.id ?? 'mat-brick',
      side: scene.materials[1]?.id ?? scene.materials[0]?.id ?? 'mat-plaster',
    },
    openings: [],
  };
}

export function makeSolidBox(scene: EditorScene): SolidBoxEntity {
  const id = uniqueId('box', idsOf(scene));
  return {
    type: 'solid_box',
    id,
    name: id,
    origin: [0, 0, 0],
    size: [1, 1, 1],
    materialId: scene.materials[0]?.id ?? 'mat-brick',
  };
}

export function makeVoid(scene: EditorScene): VoidEntity {
  const id = uniqueId('void', idsOf(scene));
  return {
    type: 'void',
    id,
    name: id,
    origin: [0, 0, 0],
    size: [0.9, 0.2, 2.1],
  };
}

export function makeSolidExtrude(scene: EditorScene): SolidExtrudeEntity {
  const id = uniqueId('extrude', idsOf(scene));
  return {
    type: 'solid_extrude',
    id,
    name: id,
    // Simple L-shape so the entity is visibly non-box.
    polygon: [
      [0, 0],
      [2, 0],
      [2, 1],
      [1, 1],
      [1, 2],
      [0, 2],
    ],
    height: 2.5,
    materialId: scene.materials[0]?.id ?? 'mat-brick',
  };
}

/** Shallow clone with a replaced entity (by id); other entities are shared. */
export function replaceEntity(
  scene: EditorScene,
  next: EditorEntity,
): EditorScene {
  return {
    ...scene,
    entities: scene.entities.map((e) => (e.id === next.id ? next : e)),
  };
}

export function removeEntity(scene: EditorScene, id: string): EditorScene {
  return {
    ...scene,
    entities: scene.entities.filter((e) => e.id !== id),
  };
}

export function addEntity(
  scene: EditorScene,
  entity: EditorEntity,
): EditorScene {
  return {
    ...scene,
    entities: [...scene.entities, entity],
  };
}

export function findEntity(
  scene: EditorScene | null,
  id: string | null,
): EditorEntity | null {
  if (!scene || !id) {
    return null;
  }
  return scene.entities.find((e) => e.id === id) ?? null;
}

/** Editor Z-up → three.js Y-up: (x, y, z) → (x, z, y). */
export function toThreeCoords(v: Vec3): [number, number, number] {
  return [v[0], v[2], v[1]];
}
