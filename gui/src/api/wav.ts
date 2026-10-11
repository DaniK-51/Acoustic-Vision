/**
 * Client-side reader for the ir.wav bytes av-api serves
 * (libs/io encodeWav): IEEE float32, format tag 3, interleaved frames.
 * No C++ change needed — the chart consumes the same artifact as Download.
 */

export interface IrWave {
  sampleRate: number;
  channels: number;
  /** Frame count per channel (samples.length = frames * channels). */
  frames: number;
  /** Interleaved float32 frames: [f0c0, f0c1, …, f1c0, …]. */
  samples: Float32Array;
}

function fourCc(view: DataView, offset: number): string {
  return String.fromCharCode(
    view.getUint8(offset),
    view.getUint8(offset + 1),
    view.getUint8(offset + 2),
    view.getUint8(offset + 3),
  );
}

/** Parse a RIFF/WAVE float32 buffer. Throws on an unsupported container. */
export function parseWavFloat32(buffer: ArrayBuffer): IrWave {
  if (buffer.byteLength < 12) {
    throw new Error('ir.wav: too small for a RIFF header');
  }
  const view = new DataView(buffer);
  if (fourCc(view, 0) !== 'RIFF' || fourCc(view, 8) !== 'WAVE') {
    throw new Error('ir.wav: not a RIFF/WAVE file');
  }

  let sampleRate = 0;
  let channels = 0;
  let format = 0;
  let samples: Float32Array | null = null;

  // Chunk walk from the first subchunk; skips fact/unknown chunks.
  let offset = 12;
  while (offset + 8 <= buffer.byteLength) {
    const id = fourCc(view, offset);
    const size = view.getUint32(offset + 4, true);
    const body = offset + 8;
    if (body + size > buffer.byteLength) {
      break;
    }
    if (id === 'fmt ' && size >= 16) {
      format = view.getUint16(body, true);
      channels = view.getUint16(body + 2, true);
      sampleRate = view.getUint32(body + 4, true);
    } else if (id === 'data') {
      // RIFF chunk bodies are not guaranteed 4-byte aligned (av-api's writer
      // puts data at offset 58), so copy into a fresh buffer before the
      // Float32Array view — views require a multiple-of-4 byteOffset.
      const byteLen = size - (size % 4);
      const copy = new Uint8Array(byteLen);
      copy.set(new Uint8Array(buffer, body, byteLen));
      samples = new Float32Array(copy.buffer);
    }
    offset = body + size + (size % 2); // chunks are word-aligned
  }

  if (!samples || channels < 1 || sampleRate <= 0) {
    throw new Error('ir.wav: missing fmt/data chunk');
  }
  if (format !== 3) {
    throw new Error(`ir.wav: unsupported format tag ${format} (need 3 = float32)`);
  }
  if (samples.length % channels !== 0) {
    throw new Error('ir.wav: sample count not divisible by channel count');
  }
  return {
    sampleRate,
    channels,
    frames: samples.length / channels,
    samples,
  };
}
