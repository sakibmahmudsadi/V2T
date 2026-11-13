// WAV file reader: any common WAV in, mono float32 @ 16 kHz out.
//
// The engine only accepts 16 kHz mono float32 and does not resample, so the
// conversion happens here, once.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace pt {

// The sample rate the engine expects.
constexpr int kEngineSampleRate = 16000;

struct Audio {
  std::vector<float> samples;  // mono, in [-1, 1], at kEngineSampleRate
  int source_sample_rate = 0;
  int source_channels = 0;
  double seconds() const {
    return source_sample_rate > 0
               ? static_cast<double>(samples.size()) / kEngineSampleRate
               : 0.0;
  }
};

// Reads a RIFF/WAVE file and converts it to mono float32 at 16 kHz.
// Returns false and fills `err` with a human-readable reason on failure.
bool load_wav(const std::string& path, Audio& out, std::string& err);

// Same conversion, from an in-memory RIFF/WAVE buffer (used by the tests).
bool decode_wav(const uint8_t* data, size_t size, Audio& out, std::string& err);

}  // namespace pt
