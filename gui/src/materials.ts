// Material ↔ surface mapping shared by the viewport (face colors, legend),
// MaterialsPanel (α table) and SceneOutlinePanel (A5 bindings). No Three.js
// here — colors are plain CSS hex so panels can paint swatches directly.
import type { Scenario, ScenarioMaterial } from './api/types';

/** Fixed palette indexed by position in scenario.materials (stable per scene). */
export const kMaterialPalette = [
  '#e2574c',
  '#4f8cff',
  '#3ecf8e',
  '#e6b84d',
  '#b06ce8',
  '#2ec7b8',
  '#e87ab0',
  '#8bc34a',
] as const;

/** Rect face keys in display order (panel rows, outline, legend). */
export const kRectFaces = ['floor', 'ceiling', 'x+', 'x-', 'z+', 'z-'] as const;

export type SurfaceBinding = {
  /** Rect face name or mesh materialOverrides key. */
  surface: string;
  materialId: string;
};

/** Surface → material bindings of the loaded room (rect faces or mesh overrides). */
export function surfaceBindings(sc: Scenario | null): SurfaceBinding[] {
  if (!sc) {
    return [];
  }
  const room = sc.room;
  if (room.type === 'rect' && room.faces) {
    const out: SurfaceBinding[] = [];
    for (const face of kRectFaces) {
      const materialId = room.faces[face];
      if (materialId) {
        out.push({ surface: face, materialId });
      }
    }
    return out;
  }
  if (room.type === 'mesh' && room.mesh) {
    const overrides = (room.mesh as { materialOverrides?: Record<string, string> })
      .materialOverrides;
    if (overrides) {
      return Object.entries(overrides).map(([surface, materialId]) => ({
        surface,
        materialId,
      }));
    }
  }
  return [];
}

export function materialById(
  sc: Scenario | null,
  materialId: string,
): ScenarioMaterial | undefined {
  return sc?.materials.find((m) => m.id === materialId);
}

/** Palette color for a material id (first slot for unknown ids). */
export function materialColor(sc: Scenario | null, materialId: string): string {
  const idx = sc?.materials.findIndex((m) => m.id === materialId) ?? -1;
  return kMaterialPalette[idx >= 0 ? idx % kMaterialPalette.length : 0];
}

/** 125 → 125, 1000 → 1k (band headers in tables and legends). */
export function fmtBand(hz: number): string {
  return hz >= 1000 ? `${hz / 1000}k` : String(hz);
}
