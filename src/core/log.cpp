#include "core/log.h"

#include <cstdio>
#include <ctime>
#include <mutex>

namespace pt {
namespace {

std::mutex& log_mutex() {
  static std::mutex m;
  return m;
}

LogLevel g_level = LogLevel::Info;
FILE* g_file = nullptr;

const char* level_name(LogLevel level) {
  switch (level) {
    case LogLevel::Trace: return "TRACE";
    case LogLevel::Debug: return "DEBUG";
    case LogLevel::Info: return "INFO";
    case LogLevel::Warn: return "WARN";
    case LogLevel::Error: return "ERROR";
    default: return "OFF";
  }
}

}  // namespace

bool log_set_file(const std::string& path) {
  std::lock_guard<std::mutex> lock(log_mutex());
  if (g_file) {
    std::fclose(g_file);
    g_file = nullptr;
  }
  if (path.empty()) return true;
  g_file = std::fopen(path.c_str(), "a");
  return g_file != nullptr;
}

void log_set_level(LogLevel level) {
  std::lock_guard<std::mutex> lock(log_mutex());
  g_level = level;
}

LogLevel log_level() {
  std::lock_guard<std::mutex> lock(log_mutex());
  return g_level;
}

void log_write(LogLevel level, const char* file, int line, const std::string& message) {
  std::lock_guard<std::mutex> lock(log_mutex());
  if (level < g_level || g_level == LogLevel::Off) return;

  const char* base = file ? file : "";
  const char* slash = base;
  for (const char* p = base; *p; ++p) {
    if (*p == '/' || *p == '\\') slash = p + 1;
  }

  std::time_t now = std::time(nullptr);
  std::tm tm_buf{};
  localtime_s(&tm_buf, &now);
  char stamp[32];
  std::strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M:%S", &tm_buf);

  if (g_file) {
    std::fprintf(g_file, "%s [%s] %s:%d %s\n", stamp, level_name(level), slash, line,
                 message.c_str());
    std::fflush(g_file);
  }
  std::fprintf(stderr, "%s [%s] %s:%d %s\n", stamp, level_name(level), slash, line,
               message.c_str());
}

}  // namespace pt
