// A4 preset scenarios: same shoebox, three absorption profiles (soft = dead,
// hard = live, studio = treated) so RT and face colors visibly differ.
import type { Scenario } from './api/types';

export interface ScenarioPreset {
  id: string;
  /** Short button label. */
  label: string;
  /** One-line description shown next to the preset row. */
  blurb: string;
  scenario: Scenario;
}

const bandsHz = [125, 250, 500, 1000, 2000, 4000];

function shoebox(
  name: string,
  materials: Scenario['materials'],
  faces: Record<string, string>,
): Scenario {
  return {
    schemaVersion: 1,
    name,
    room: {
      type: 'rect',
      size: { w: 6.0, h: 2.8, d: 4.5 },
      faces,
    },
    materials,
    sources: [{ id: 'src-1', position: [1.2, 1.5, 1.0], powerDb: 80.0 }],
    receivers: [{ id: 'rcv-1', position: [4.0, 1.5, 3.0] }],
    params: {
      engine: 'image-source',
      maxReflectionOrder: 10,
      sampleRate: 16000,
      durationSec: 1.0,
      bandsHz,
      speedOfSound: 343.0,
      capturePaths: false,
      airAbsorption: false,
      signedIr: false,
    },
  };
}

const soft: Scenario = shoebox(
  'preset-soft',
  [
    {
      id: 'mat-deep-carpet',
      name: 'Deep carpet + underlay',
      bandsHz,
      absorption: [0.25, 0.4, 0.55, 0.65, 0.7, 0.7],
      scattering: 0.2,
    },
    {
      id: 'mat-acoustic-tile',
      name: 'Acoustic ceiling tile',
      bandsHz,
      absorption: [0.35, 0.5, 0.65, 0.7, 0.7, 0.65],
      scattering: 0.2,
    },
    {
      id: 'mat-drape',
      name: 'Heavy fabric drape',
      bandsHz,
      absorption: [0.2, 0.4, 0.6, 0.7, 0.7, 0.65],
      scattering: 0.2,
    },
  ],
  {
    floor: 'mat-deep-carpet',
    ceiling: 'mat-acoustic-tile',
    'x+': 'mat-drape',
    'x-': 'mat-drape',
    'z+': 'mat-drape',
    'z-': 'mat-drape',
  },
);

const hard: Scenario = shoebox(
  'preset-hard',
  [
    {
      id: 'mat-concrete',
      name: 'Polished concrete',
      bandsHz,
      absorption: [0.01, 0.01, 0.02, 0.02, 0.02, 0.02],
      scattering: 0.05,
    },
    {
      id: 'mat-plaster-hard',
      name: 'Painted plaster',
      bandsHz,
      absorption: [0.05, 0.05, 0.06, 0.07, 0.07, 0.07],
      scattering: 0.05,
    },
    {
      id: 'mat-glass',
      name: 'Glass / glazing',
      bandsHz,
      absorption: [0.03, 0.03, 0.03, 0.04, 0.04, 0.03],
      scattering: 0.05,
    },
  ],
  {
    floor: 'mat-concrete',
    ceiling: 'mat-plaster-hard',
    'x+': 'mat-glass',
    'x-': 'mat-glass',
    'z+': 'mat-plaster-hard',
    'z-': 'mat-glass',
  },
);

const studio: Scenario = shoebox(
  'preset-studio',
  [
    {
      id: 'mat-carpet',
      name: 'Carpet on concrete',
      bandsHz,
      absorption: [0.08, 0.15, 0.3, 0.4, 0.4, 0.4],
      scattering: 0.25,
    },
    {
      id: 'mat-broadband',
      name: 'Broadband absorber',
      bandsHz,
      absorption: [0.45, 0.65, 0.8, 0.85, 0.85, 0.8],
      scattering: 0.25,
    },
    {
      id: 'mat-wood-slat',
      name: 'Wood slat / diffuser',
      bandsHz,
      absorption: [0.12, 0.2, 0.3, 0.28, 0.28, 0.32],
      scattering: 0.25,
    },
  ],
  {
    floor: 'mat-carpet',
    ceiling: 'mat-broadband',
    'x+': 'mat-wood-slat',
    'x-': 'mat-wood-slat',
    'z+': 'mat-broadband',
    'z-': 'mat-wood-slat',
  },
);

export const scenarioPresets: ScenarioPreset[] = [
  { id: 'soft', label: 'Soft', blurb: 'high α — dead room', scenario: soft },
  { id: 'hard', label: 'Hard', blurb: 'low α — live room', scenario: hard },
  { id: 'studio', label: 'Studio', blurb: 'treated — absorbers + diffusers', scenario: studio },
];
