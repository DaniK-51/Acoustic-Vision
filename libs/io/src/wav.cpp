#include <av/io/io.hpp>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <vector>

namespace av {
namespace {

void appendU16(std::vector<std::uint8_t>& out, std::uint16_t value) {
  out.push_back(static_cast<std::uint8_t>(value & 0xFFu));
  out.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFFu));
}

void appendU32(std::vector<std::uint8_t>& out, std::uint32_t value) {
  out.push_back(static_cast<std::uint8_t>(value & 0xFFu));
  out.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFFu));
  out.push_back(static_cast<std::uint8_t>((value >> 16) & 0xFFu));
  out.push_back(static_cast<std::uint8_t>((value >> 24) & 0xFFu));
}

void appendFourCc(std::vector<std::uint8_t>& out, const char* tag) {
  out.push_back(static_cast<std::uint8_t>(tag[0]));
  out.push_back(static_cast<std::uint8_t>(tag[1]));
  out.push_back(static_cast<std::uint8_t>(tag[2]));
  out.push_back(static_cast<std::uint8_t>(tag[3]));
}

void appendF32(std::vector<std::uint8_t>& out, float value) {
  static_assert(sizeof(float) == 4, "IEEE float32 WAV requires 32-bit float");
  std::uint32_t bits = 0;
  std::memcpy(&bits, &value, sizeof(bits));
  appendU32(out, bits);
}

}  // namespace

std::vector<std::uint8_t> encodeWav(const IrBuffer& ir) {
  if (ir.channels == 0) {
    throw std::runtime_error("encodeWav: IrBuffer has no channels");
  }
  if (!(ir.sampleRate > 0.0)) {
    throw std::runtime_error("encodeWav: sampleRate must be positive");
  }
  if (ir.samples.size() % ir.channels != 0) {
    throw std::runtime_error(
        "encodeWav: samples size not divisible by channel count");
  }
  const std::size_t sampleCount = ir.samples.size() / ir.channels;
  // RIFF sizes are uint32; samples are 4 bytes each plus the fixed header.
  if (ir.samples.size() >
      (static_cast<std::size_t>((std::numeric_limits<std::uint32_t>::max)()) -
       58u) /
          4u) {
    throw std::runtime_error("encodeWav: IR too large for a RIFF container");
  }

  const std::uint32_t sampleRateU32 =
      static_cast<std::uint32_t>(std::llround(ir.sampleRate));
  const std::uint16_t channelsU16 = static_cast<std::uint16_t>(ir.channels);
  const std::uint16_t blockAlign =
      static_cast<std::uint16_t>(channelsU16 * 4u);
  const std::uint32_t byteRate = sampleRateU32 * blockAlign;
  const std::uint32_t dataBytes =
      static_cast<std::uint32_t>(ir.samples.size() * 4u);

  // fmt is 18 bytes (WAVEFORMATEX with cbSize) and a fact chunk is present:
  // format tag 3 is non-PCM, so the extension fields are required by the spec.
  std::vector<std::uint8_t> bytes;
  bytes.reserve(58u + ir.samples.size() * 4u);

  appendFourCc(bytes, "RIFF");
  appendU32(bytes, 50u + dataBytes);  // file size minus the "RIFF" + size fields
  appendFourCc(bytes, "WAVE");

  appendFourCc(bytes, "fmt ");
  appendU32(bytes, 18u);
  appendU16(bytes, 3u);  // WAVE_FORMAT_IEEE_FLOAT
  appendU16(bytes, channelsU16);
  appendU32(bytes, sampleRateU32);
  appendU32(bytes, byteRate);
  appendU16(bytes, blockAlign);
  appendU16(bytes, 32u);  // bits per sample
  appendU16(bytes, 0u);   // cbSize

  appendFourCc(bytes, "fact");
  appendU32(bytes, 4u);
  appendU32(bytes, static_cast<std::uint32_t>(sampleCount));

  appendFourCc(bytes, "data");
  appendU32(bytes, dataBytes);
  // Planar [channel][sample] -> interleaved frames.
  for (std::size_t i = 0; i < sampleCount; ++i) {
    for (std::size_t channel = 0; channel < ir.channels; ++channel) {
      appendF32(bytes, ir.samples[channel * sampleCount + i]);
    }
  }
  return bytes;
}

void writeWav(const std::filesystem::path& path, const IrBuffer& ir) {
  const std::vector<std::uint8_t> bytes = encodeWav(ir);
  std::ofstream output(path, std::ios::binary);
  if (!output) {
    throw std::runtime_error("cannot write WAV file: " + path.string());
  }
  output.write(reinterpret_cast<const char*>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
  if (!output) {
    throw std::runtime_error("failed writing WAV file: " + path.string());
  }
}

}  // namespace av
