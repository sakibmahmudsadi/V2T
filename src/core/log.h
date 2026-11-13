// Leveled logger: stderr always, optional file as well.
//
// Deliberately tiny and dependency-free: the controller process must stay
// small, so the logger allocates nothing until a log file is requested.
#pragma once

#include <string>

namespace pt {

enum class LogLevel { Trace = 0, Debug = 1, Info = 2, Warn = 3, Error = 4, Off = 5 };

// Returns false when the file cannot be opened (logging then stays on stderr).
bool log_set_file(const std::string& path);
void log_set_level(LogLevel level);
LogLevel log_level();

void log_write(LogLevel level, const char* file, int line, const std::string& message);

}  // namespace pt

#define PT_LOG(level, msg) ::pt::log_write((level), __FILE__, __LINE__, (msg))
#define PT_TRACE(msg) PT_LOG(::pt::LogLevel::Trace, (msg))
#define PT_DEBUG(msg) PT_LOG(::pt::LogLevel::Debug, (msg))
#define PT_INFO(msg) PT_LOG(::pt::LogLevel::Info, (msg))
#define PT_WARN(msg) PT_LOG(::pt::LogLevel::Warn, (msg))
#define PT_ERROR(msg) PT_LOG(::pt::LogLevel::Error, (msg))
