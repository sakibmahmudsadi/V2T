#include "core/wav.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace pt {
namespace {

constexpr uint16_t kFormatPcm = 1;
constexpr uint16_t kFormatIeeeFloat = 3;
constexpr uint16_t kFormatExtensible = 0xFFFE;

uint16_t read_u16(const uint8_t* p) {
  return static_cast<uint16_t>(p[0] | (p[1] << 8));
}

uint32_t read_u32(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
         (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

// One sample of one channel, normalised to [-1, 1].
double sample_value(const uint8_t* p, uint16_t bits, uint16_t format) {
  if (format == kFormatIeeeFloat && bits == 32) {
    float f = 0.0f;
    std::memcpy(&f, p, sizeof(f));
    return static_cast<double>(f);
  }
  if (format == kFormatIeeeFloat && bits == 64) {
    double d = 0.0;
    std::memcpy(&d, p, sizeof(d));
    return d;
  }
  switch (bits) {
    case 8:
      return (static_cast<double>(p[0]) - 128.0) / 128.0;
    case 16: {
      int16_t v = static_cast<int16_t>(read_u16(p));
      return static_cast<double>(v) / 32768.0;
    }
    case 24: {
      int32_t v = static_cast<int32_t>(p[0] | (p[1] << 8) | (p[2] << 16));
      if (v & 0x800000) v |= ~0xFFFFFF;  // sign extend
      return static_cast<double>(v) / 8388608.0;
    }
    case 32: {
      int32_t v = static_cast<int32_t>(read_u32(p));
      return static_cast<double>(v) / 2147483648.0;
    }
    default:
      return 0.0;
  }
}

void resample_to_engine_rate(const std::vector<double>& src, int src_rate,
                             std::vector<float>& out) {
  out.clear();
  if (src.empty() || src_rate <= 0) return;

  if (src_rate == kEngineSampleRate) {
    out.reserve(src.size());
    for (double v : src) out.push_back(static_cast<float>(v));
    return;
  }

  const size_t out_count =
      static_cast<size_t>(std::llround(static_cast<double>(src.size()) *
                                      kEngineSampleRate / src_rate));
  out.reserve(out_count);
  const double step = static_cast<double>(src_rate) / kEngineSampleRate;
  for (size_t i = 0; i < out_count; ++i) {
    const double pos = static_cast<double>(i) * step;
    size_t i0 = static_cast<size_t>(pos);
    if (i0 >= src.size()) i0 = src.size() - 1;
    size_t i1 = i0 + 1;
    if (i1 >= src.size()) i1 = i0;
    const double frac = pos - static_cast<double>(i0);
    const double v = src[i0] + (src[i1] - src[i0]) * frac;
    out.push_back(static_cast<float>(v));
  }
}

}  // namespace

bool decode_wav(const uint8_t* data, size_t size, Audio& out, std::string& err) {
  out = Audio();

  if (data == nullptr || size < 4) {
    err = "file is too small to be a WAV";
    return false;
  }
  if (std::memcmp(data, "RIFF", 4) != 0) {
    err = "not a RIFF/WAVE file";
    return false;
  }
  if (size < 44) {
    err = "file is too small to be a WAV";
    return false;
  }
  if (std::memcmp(data + 8, "WAVE", 4) != 0) {
    err = "not a RIFF/WAVE file";
    return false;
  }

  uint16_t format = 0;
  uint16_t channels = 0;
  uint16_t bits = 0;
  uint32_t sample_rate = 0;
  bool have_fmt = false;

  const uint8_t* data_chunk = nullptr;
  size_t data_size = 0;

  size_t pos = 12;
  while (pos + 8 <= size) {
    const char* id = reinterpret_cast<const char*>(data + pos);
    const uint32_t chunk_size = read_u32(data + pos + 4);
    const size_t body = pos + 8;
    if (body + chunk_size > size) {
      // Tolerate a truncated final chunk by clamping to what is there.
      const size_t available = size - body;
      if (std::memcmp(id, "data", 4) == 0) {
        data_chunk = data + body;
        data_size = available;
      }
      break;
    }

    if (std::memcmp(id, "fmt ", 4) == 0 && chunk_size >= 16) {
      format = read_u16(data + body);
      channels = read_u16(data + body + 2);
      sample_rate = read_u32(data + body + 4);
      bits = read_u16(data + body + 14);
      if (format == kFormatExtensible && chunk_size >= 26) {
        // The real format lives in the first two bytes of the sub-format GUID.
        format = read_u16(data + body + 24);
      }
      have_fmt = true;
    } else if (std::memcmp(id, "data", 4) == 0) {
      data_chunk = data + body;
      data_size = chunk_size;
    }

    pos = body + chunk_size + (chunk_size & 1u);  // chunks are word aligned
  }

  if (!have_fmt) {
    err = "WAV file has no 'fmt ' chunk";
    return false;
  }
  if (data_chunk == nullptr) {
    err = "WAV file has no 'data' chunk";
    return false;
  }
  if (channels == 0 || sample_rate == 0) {
    err = "WAV file declares zero channels or zero sample rate";
    return false;
  }
  if (format != kFormatPcm && format != kFormatIeeeFloat) {
    err = "unsupported WAV sample format (only PCM and IEEE float are supported)";
    return false;
  }
  if (bits != 8 && bits != 16 && bits != 24 && bits != 32) {
    err = "unsupported WAV bit depth: " + std::to_string(bits);
    return false;
  }
  if (format == kFormatIeeeFloat && bits != 32 && bits != 64) {
    err = "unsupported float WAV bit depth: " + std::to_string(bits);
    return false;
  }

  const size_t bytes_per_sample = bits / 8u;
  const size_t frame_bytes = bytes_per_sample * channels;
  if (frame_bytes == 0) {
    err = "WAV frame size is zero";
    return false;
  }

  const size_t frames = data_size / frame_bytes;
  if (frames == 0) {
    err = "WAV file contains no audio frames";
    return false;
  }

  std::vector<double> mono;
  mono.reserve(frames);
  for (size_t f = 0; f < frames; ++f) {
    const uint8_t* frame = data_chunk + f * frame_bytes;
    double sum = 0.0;
    for (size_t c = 0; c < channels; ++c) {
      sum += sample_value(frame + c * bytes_per_sample, bits, format);
    }
    mono.push_back(sum / static_cast<double>(channels));
  }

  out.source_sample_rate = static_cast<int>(sample_rate);
  out.source_channels = static_cast<int>(channels);
  resample_to_engine_rate(mono, static_cast<int>(sample_rate), out.samples);
  return true;
}

bool load_wav(const std::string& path, Audio& out, std::string& err) {
  std::FILE* f = std::fopen(path.c_str(), "rb");
  if (f == nullptr) {
    err = "cannot open file: " + path;
    return false;
  }

  std::vector<uint8_t> bytes;
  uint8_t buffer[65536];
  size_t n = 0;
  while ((n = std::fread(buffer, 1, sizeof(buffer), f)) > 0) {
    bytes.insert(bytes.end(), buffer, buffer + n);
  }
  std::fclose(f);

  if (bytes.empty()) {
    err = "file is empty: " + path;
    return false;
  }
  return decode_wav(bytes.data(), bytes.size(), out, err);
}

}  // namespace pt
