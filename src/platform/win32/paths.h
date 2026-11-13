// Windows-only path helpers.
#pragma once

#include <string>

namespace pt {
namespace win32 {

// Full path of the running executable.
std::string exe_path();

// Directory containing the running executable (no trailing separator).
std::string exe_dir();

// Joins two path fragments with a single separator.
std::string join_path(const std::string& a, const std::string& b);

// Resolves a path to an absolute, normalised form (no "." or ".." segments).
std::string normalize_path(const std::string& path);

}  // namespace win32
}  // namespace pt
