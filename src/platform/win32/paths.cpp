#include "platform/win32/paths.h"

#include <windows.h>

namespace pt {
namespace win32 {
namespace {

std::string dir_name(const std::string& path) {
  const size_t pos = path.find_last_of("\\/");
  if (pos == std::string::npos) return std::string();
  return path.substr(0, pos);
}

}  // namespace

std::string exe_path() {
  char buffer[MAX_PATH];
  const DWORD n = GetModuleFileNameA(nullptr, buffer, MAX_PATH);
  if (n == 0 || n >= MAX_PATH) return std::string();
  return std::string(buffer, n);
}

std::string exe_dir() {
  return dir_name(exe_path());
}

std::string join_path(const std::string& a, const std::string& b) {
  if (a.empty()) return b;
  if (b.empty()) return a;
  if (a.back() == '\\' || a.back() == '/') return a + b;
  return a + "\\" + b;
}

std::string normalize_path(const std::string& path) {
  if (path.empty()) return path;
  char buffer[MAX_PATH];
  const DWORD n = GetFullPathNameA(path.c_str(), MAX_PATH, buffer, nullptr);
  if (n == 0 || n >= MAX_PATH) return path;
  return std::string(buffer, n);
}

}  // namespace win32
}  // namespace pt
