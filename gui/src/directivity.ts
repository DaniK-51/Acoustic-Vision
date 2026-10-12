// Polar-directivity helpers for the GUI: analytic presets (omni/dipole/cardioid
// when curves are absent) and linear frequency interpolation between the two
// nearest measured curves (plan-v0.1.1 B2/B4). Pure functions — no DOM.

import type { Directivity, PolarCurve } from './api/types';

/** Bands used when synthesizing an analytic preset (matches the catalog). */
const DEFAULT_FREQS = [125, 250, 500, 1000, 2000, 4000];

/** Angle step for synthetic preset curves, degrees. */
const SYNTHETIC_ANGLE_STEP = 15;

const GAIN_FLOOR_DB = -40;

/**
 * Analytic relative gain (dB, 0 = on-axis) for an angle in degrees.
 * 0° = front/on-axis; +90° = the right side of the pattern.
 */
export function analyticGainDb(type: string, angleDeg: number): number {
  const rad = (angleDeg * Math.PI) / 180;
  let linear = 0;
  if (type === 'omni') {
    return 0;
  }
  if (type === 'dipole') {
    // Figure-of-eight: |cos θ|, nulls at ±90°.
    linear = Math.abs(Math.cos(rad));
  } else if (type === 'cardioid') {
    // Cardioid: (1 + cos θ) / 2, null at 180°.
    linear = (1 + Math.cos(rad)) / 2;
  } else {
    return 0;
  }
  return linear > 1e-4 ? 20 * Math.log10(linear) : GAIN_FLOOR_DB;
}

/** Preset curve set for an analytic type — flat across the default bands. */
export function syntheticPolar(type: string): PolarCurve {
  const anglesDeg: number[] = [];
  for (let a = -180; a <= 180; a += SYNTHETIC_ANGLE_STEP) {
    anglesDeg.push(a);
  }
  const gainDb = DEFAULT_FREQS.map(() =>
    anglesDeg.map((angle) => analyticGainDb(type, angle)),
  );
  return { freqsHz: [...DEFAULT_FREQS], anglesDeg, gainDb };
}

/**
 * Curve for one plane: measured data when present, analytic preset for
 * omni/dipole/cardioid when absent, null for custom without curves.
 */
export function resolvePlaneCurve(
  directivity: Directivity | null | undefined,
  plane: 'horizontal' | 'vertical',
): PolarCurve | null {
  if (!directivity) {
    return syntheticPolar('omni');
  }
  const measured = plane === 'horizontal' ? directivity.horizontal : directivity.vertical;
  if (measured && measured.freqsHz.length > 0) {
    return measured;
  }
  if (directivity.type === 'omni' || directivity.type === 'dipole' || directivity.type === 'cardioid') {
    return syntheticPolar(directivity.type);
  }
  return null;
}

/**
 * Gain row (dB per angle) at freqHz: exact match when a curve exists at f,
 * otherwise linear interpolation between the two nearest curves (B4), clamped
 * outside the measured range.
 */
export function gainAtFreq(curve: PolarCurve, freqHz: number): number[] {
  const freqs = curve.freqsHz;
  const rows = curve.gainDb;
  if (freqs.length === 0 || rows.length === 0) {
    return [];
  }
  if (freqs.length === 1 || freqHz <= freqs[0]) {
    return [...rows[0]];
  }
  const last = freqs.length - 1;
  if (freqHz >= freqs[last]) {
    return [...rows[last]];
  }
  let hi = 1;
  while (hi < freqs.length && freqs[hi] < freqHz) {
    hi++;
  }
  const lo = hi - 1;
  if (freqs[hi] === freqHz) {
    return [...rows[hi]];
  }
  const t = (freqHz - freqs[lo]) / (freqs[hi] - freqs[lo]);
  return rows[lo].map((gain, i) => gain + t * (rows[hi][i] - gain));
}
