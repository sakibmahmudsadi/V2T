// Framing for the controller <-> worker pipe.
//
// One channel carries two kinds of message:
//   kind 1 — control, a JSON object as UTF-8 text
//   kind 2 — audio, raw float32 PCM samples
//
// Every frame is: [u32 payload length][u32 kind][payload bytes], little endian.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#endif

namespace pt {
namespace ipc {

enum FrameKind : uint32_t {
  kFrameControl = 1,
  kFrameAudio = 2,
};

// Largest payload we accept, so a corrupt length cannot ask for a huge
// allocation.
constexpr uint32_t kMaxFrameBytes = 64u * 1024u * 1024u;

// True when the handle is a pipe that can be read and written.
inline bool valid(void* handle) { return handle != nullptr && handle != INVALID_HANDLE_VALUE; }

// Writes one frame. Returns false on a write error.
bool write_frame(void* handle, uint32_t kind, const void* data, size_t size);

// Reads one frame. Returns false on EOF or error; `kind` and `payload` are
// filled on success.
bool read_frame(void* handle, uint32_t& kind, std::vector<uint8_t>& payload);

// Convenience: read a control frame and parse it as JSON.
bool read_control(void* handle, std::string& json_text);

}  // namespace ipc
}  // namespace pt
