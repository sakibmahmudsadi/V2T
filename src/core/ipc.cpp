#include "core/ipc.h"

#include <cstring>

#if defined(_WIN32)
#include <windows.h>
#endif

namespace pt {
namespace ipc {
namespace {

#if defined(_WIN32)
bool write_all(void* handle, const void* data, size_t size) {
  const uint8_t* bytes = static_cast<const uint8_t*>(data);
  size_t written = 0;
  while (written < size) {
    DWORD chunk = 0;
    const DWORD want =
        static_cast<DWORD>((size - written) > 0x7FFFFFFF ? 0x7FFFFFFF : (size - written));
    if (!WriteFile(static_cast<HANDLE>(handle), bytes + written, want, &chunk, nullptr)) {
      return false;
    }
    if (chunk == 0) return false;
    written += chunk;
  }
  return true;
}

bool read_all(void* handle, void* data, size_t size) {
  uint8_t* bytes = static_cast<uint8_t*>(data);
  size_t read_total = 0;
  while (read_total < size) {
    DWORD chunk = 0;
    const DWORD want =
        static_cast<DWORD>((size - read_total) > 0x7FFFFFFF ? 0x7FFFFFFF : (size - read_total));
    if (!ReadFile(static_cast<HANDLE>(handle), bytes + read_total, want, &chunk, nullptr)) {
      return false;
    }
    if (chunk == 0) return false;  // EOF
    read_total += chunk;
  }
  return true;
}
#else
bool write_all(void*, const void*, size_t) { return false; }
bool read_all(void*, void*, size_t) { return false; }
#endif

}  // namespace

bool write_frame(void* handle, uint32_t kind, const void* data, size_t size) {
#if !defined(_WIN32)
  (void)handle;
  (void)kind;
  (void)data;
  (void)size;
  return false;
#else
  if (!valid(handle)) return false;
  if (size > kMaxFrameBytes) return false;

  uint8_t header[8];
  const uint32_t length = static_cast<uint32_t>(size);
  header[0] = static_cast<uint8_t>(length & 0xFF);
  header[1] = static_cast<uint8_t>((length >> 8) & 0xFF);
  header[2] = static_cast<uint8_t>((length >> 16) & 0xFF);
  header[3] = static_cast<uint8_t>((length >> 24) & 0xFF);
  header[4] = static_cast<uint8_t>(kind & 0xFF);
  header[5] = static_cast<uint8_t>((kind >> 8) & 0xFF);
  header[6] = static_cast<uint8_t>((kind >> 16) & 0xFF);
  header[7] = static_cast<uint8_t>((kind >> 24) & 0xFF);

  if (!write_all(handle, header, sizeof(header))) return false;
  if (size > 0 && !write_all(handle, data, size)) return false;
  return true;
#endif
}

bool read_frame(void* handle, uint32_t& kind, std::vector<uint8_t>& payload) {
#if !defined(_WIN32)
  (void)handle;
  (void)kind;
  (void)payload;
  return false;
#else
  if (!valid(handle)) return false;

  uint8_t header[8];
  if (!read_all(handle, header, sizeof(header))) return false;

  const uint32_t length = static_cast<uint32_t>(header[0]) |
                          (static_cast<uint32_t>(header[1]) << 8) |
                          (static_cast<uint32_t>(header[2]) << 16) |
                          (static_cast<uint32_t>(header[3]) << 24);
  kind = static_cast<uint32_t>(header[4]) | (static_cast<uint32_t>(header[5]) << 8) |
         (static_cast<uint32_t>(header[6]) << 16) | (static_cast<uint32_t>(header[7]) << 24);

  if (length > kMaxFrameBytes) return false;
  payload.assign(length, 0);
  if (length > 0 && !read_all(handle, payload.data(), length)) return false;
  return true;
#endif
}

bool read_control(void* handle, std::string& json_text) {
  uint32_t kind = 0;
  std::vector<uint8_t> payload;
  if (!read_frame(handle, kind, payload)) return false;
  if (kind != kFrameControl) return false;
  json_text.assign(payload.begin(), payload.end());
  return true;
}

}  // namespace ipc
}  // namespace pt
