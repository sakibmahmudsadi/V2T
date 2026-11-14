// Application configuration.
//
// Read from config.json next to the executable. Every value has a default, so
// a missing file or an unknown key is never fatal.
#pragma once

#include <string>
#include <vector>

namespace pt {

struct Config {
  // Absolute path of the Parakeet GGUF model.
  std::string model_path;

  // Folder holding transcribe.dll and the ggml modules (default: <exe>/engine).
  std::string engine_dir;

  // Compute backend: "cpu" (default) or "vulkan".
  std::string backend = "cpu";

  // Hotkey that toggles dictation. Right Ctrl + Right Alt by default.
  std::vector<std::string> hotkey = {"rctrl", "ralt"};
  std::string cancel_key = "esc";

  // The worker exits this many seconds after the last transcription.
  int model_unload_seconds = 60;

  // Hard cap on the captured audio, in seconds.
  int max_buffer_seconds = 120;

  // Strip filler words from the transcript.
  bool filler_removal = true;
  std::vector<std::string> filler_words;

  // Append a space after the typed text.
  bool append_trailing_space = true;

  // Send Enter after the text.
  bool auto_submit = false;

  // Which screen edge the mic button docks to: "right" or "left".
  std::string mic_button_edge = "right";

  // Log file (relative to the executable folder).
  std::string log_path = "logs/v2t.log";
  std::string log_level = "info";

  // Path of the config file this was loaded from.
  std::string source_path;
};

// Loads the configuration. Missing file -> defaults; malformed file -> defaults
// plus a warning in `err` (never a hard failure).
bool load_config(const std::string& path, Config& config, std::string& err);

// Writes a config file with the current values (used to create the first one).
bool save_config(const std::string& path, const Config& config, std::string& err);

}  // namespace pt
