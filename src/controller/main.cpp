// V2T controller — the always-on process.
//
// Right Ctrl + Right Alt starts recording; press it again (or click the mic
// button) to stop, and the finished text is typed at the cursor.
//
// This process never loads the model. It watches the hotkey, captures the
// microphone into a buffer, shows the one mic button, and hands the audio to
// the worker process, which owns the model. When the model has been idle for
// `model_unload_seconds`, the worker exits and its memory goes back to the OS.
//
// State machine:
//
//   IDLE --hotkey--> RECORDING --hotkey--> DECODING --> TYPING --> IDLE
//              \--Esc--> CANCELLED ---------------------------------> IDLE
//
// The audio captured while the model is still loading is replayed into the
// live stream as soon as the model is ready, so nothing is lost and the user
// never waits for a load.
#include <windows.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "core/config.h"
#include "core/ipc.h"
#include "core/json.h"
#include "core/log.h"
#include "core/text.h"
#include "platform/win32/audio.h"
#include "platform/win32/hotkey.h"
#include "platform/win32/mic_button.h"
#include "platform/win32/paths.h"
#include "platform/win32/typing.h"
#include "platform/win32/worker_client.h"

namespace {

namespace json = pt::json;
namespace ipc = pt::ipc;

constexpr UINT WM_APP_TOGGLE = WM_APP + 1;
constexpr UINT WM_APP_STOP = WM_APP + 2;
constexpr UINT WM_APP_QUIT = WM_APP + 3;
constexpr UINT WM_APP_RESULT = WM_APP + 4;
constexpr UINT WM_APP_CANCEL = WM_APP + 5;

bool file_exists(const std::string& path) {
  return GetFileAttributesA(path.c_str()) != INVALID_FILE_ATTRIBUTES;
}

enum class AppState { Idle, Recording, Decoding };

// What the worker thread hands back to the UI thread.
struct Result {
  bool ok = true;
  std::string text;     // the transcript (when ok)
  std::string error;    // the reason (when not ok)
};

// Everything the audio thread, the worker thread and the UI thread share.
struct Shared {  std::mutex mutex;
  std::condition_variable cv;

  std::vector<float> buffer;  // captured PCM, mono 16 kHz
  size_t sent = 0;            // how much of it the worker already has

  bool capturing = false;
  bool overflow = false;      // the buffer hit its cap
  bool start_requested = false;
  bool stop_requested = false;
  bool cancel_requested = false;
  bool quit_requested = false;

  std::chrono::steady_clock::time_point last_activity = std::chrono::steady_clock::now();
  std::atomic<AppState> state{AppState::Idle};
};

Shared g_shared;
HWND g_window = nullptr;
pt::Config g_config;
pt::win32::HotkeyListener g_hotkey;
pt::win32::AudioCapture g_audio;
pt::win32::MicButton g_mic;
pt::win32::WorkerClient g_worker;
std::thread g_worker_thread;
std::string g_pipe_name;
bool g_worker_active = false;  // the worker process holds the model

// ---------------------------------------------------------------------------
// Audio capture -> buffer
// ---------------------------------------------------------------------------

// The input level, 0..1, used to tell "recording" from "audio is arriving".
std::atomic<float> g_input_level{0.0f};

void on_audio(const float* samples, size_t count) {
  // Root mean square of the chunk: cheap, and enough to show that the
  // microphone is picking you up.
  double sum = 0.0;
  for (size_t i = 0; i < count; ++i) {
    sum += static_cast<double>(samples[i]) * samples[i];
  }
  const float rms = count > 0 ? static_cast<float>(std::sqrt(sum / count)) : 0.0f;
  g_input_level = rms;

  std::lock_guard<std::mutex> lock(g_shared.mutex);
  const size_t cap = static_cast<size_t>(g_config.max_buffer_seconds) * 16000;
  if (g_shared.buffer.size() + count > cap) {
    g_shared.overflow = true;
    return;
  }
  g_shared.buffer.insert(g_shared.buffer.end(), samples, samples + count);
  g_shared.cv.notify_all();
}

// ---------------------------------------------------------------------------
// Worker thread: owns the model process and the pipe
// ---------------------------------------------------------------------------

std::string worker_path() {
  return pt::win32::join_path(pt::win32::exe_dir(), "V2TWorker.exe");
}

// The engine DLL the worker loads. The engine folder sits next to the
// executable (shipped layout) or one level up (development layout).
std::string engine_dll_path() {
  std::string dir = g_config.engine_dir;
  if (dir.empty()) dir = pt::win32::join_path(pt::win32::exe_dir(), "engine");
  return pt::win32::join_path(dir, "transcribe.dll");
}

bool send_command(const std::string& command, std::string& reply) {
  const json::ValuePtr message = json::make_object();
  json::set(message, "cmd", json::make_string(command));
  std::string error;
  if (!g_worker.send_control(json::stringify(*message), error)) {
    PT_ERROR("cannot talk to the worker: " + error);
    return false;
  }
  if (!g_worker.read_control(reply, error)) {
    PT_ERROR("the worker stopped responding: " + error);
    return false;
  }
  return true;
}

std::string reply_text(const std::string& reply) {
  std::string error;
  const json::ValuePtr value = json::parse(reply, error);
  if (value == nullptr || !value->is_object()) return std::string();
  const json::Value* text = value->find("text");
  return text != nullptr ? text->as_string() : std::string();
}

// True when the reply says the command failed; the reason is in `error`.
bool reply_failed(const std::string& reply, std::string& error) {
  std::string parse_error;
  const json::ValuePtr value = json::parse(reply, parse_error);
  if (value == nullptr || !value->is_object()) {
    error = parse_error.empty() ? "malformed reply from the worker" : parse_error;
    return true;
  }
  const json::Value* ok = value->find("ok");
  if (ok != nullptr && ok->is_bool() && ok->as_bool()) return false;
  const json::Value* message = value->find("error");
  error = message != nullptr ? message->as_string() : "the worker reported a failure";
  return true;
}

// Sends everything the worker has not seen yet.
//
// The copy happens under the lock, but the pipe write does NOT: holding the
// lock across a blocking write deadlocks the microphone callback (which needs
// the same lock to append samples).
bool flush_audio() {
  std::vector<float> chunk;
  {
    std::lock_guard<std::mutex> lock(g_shared.mutex);
    if (g_shared.sent >= g_shared.buffer.size()) return true;
    chunk.assign(g_shared.buffer.begin() + static_cast<long>(g_shared.sent),
                  g_shared.buffer.end());
    g_shared.sent = g_shared.buffer.size();
  }

  std::string error;
  if (!g_worker.send_audio(chunk.data(), chunk.size(), error)) {
    PT_ERROR("cannot send audio to the worker: " + error);
    return false;
  }
  return true;
}

void post_result(bool ok, const std::string& text, const std::string& error) {
  auto* result = new Result();
  result->ok = ok;
  result->text = text;
  result->error = error;
  PostMessage(g_window, WM_APP_RESULT, 0, reinterpret_cast<LPARAM>(result));
}

void worker_thread_main() {
  while (true) {
    {
      std::unique_lock<std::mutex> lock(g_shared.mutex);
      g_shared.cv.wait_for(lock, std::chrono::milliseconds(500), [] {
        return g_shared.start_requested || g_shared.stop_requested ||
               g_shared.quit_requested ||
               (g_shared.capturing && g_shared.sent < g_shared.buffer.size());
      });

      if (g_shared.quit_requested) break;

      // Idle timer: the worker keeps the model between takes, and exits once
      // nothing has happened for model_unload_seconds.
      if (!g_shared.start_requested && !g_shared.stop_requested && g_worker_active &&
          !g_shared.capturing) {
        // Never unload the model while the user is still talking: that would
        // destroy the take in progress.
        const auto idle = std::chrono::steady_clock::now() - g_shared.last_activity;
        if (idle >= std::chrono::seconds(g_config.model_unload_seconds)) {
          lock.unlock();
          std::string reply;
          send_command("shutdown", reply);
          g_worker.stop();
          g_worker_active = false;
          PT_INFO("the model was idle; the worker exited");
          continue;
        }
      }
    }

    if (g_shared.start_requested) {
      std::unique_lock<std::mutex> lock(g_shared.mutex);
      g_shared.start_requested = false;

      // The worker keeps the model between takes (it exits when the model
      // goes idle). Reusing it means the second take starts instantly —
      // no process spawn, no model load, no gigabyte of re-allocation.
      if (!g_worker.is_running()) {
        std::string error;
        if (!g_worker.start(worker_path(), g_pipe_name, g_config.backend, engine_dll_path(),
                            error)) {
          PT_ERROR("cannot start the worker: " + error);
          lock.unlock();
          post_result(false, std::string(), error);
          continue;
        }
        g_worker_active = true;
        lock.unlock();

        // Load the model. The audio keeps accumulating in the buffer meanwhile.
        const json::ValuePtr load = json::make_object();
        json::set(load, "cmd", json::make_string("load"));
        json::set(load, "model", json::make_string(g_config.model_path));
        std::string reply;
        if (!g_worker.send_control(json::stringify(*load), error) ||
            !g_worker.read_control(reply, error)) {
          PT_ERROR("the worker did not load the model: " + error);
          post_result(false, std::string(), error);
          continue;
        }
        std::string reason;
        if (reply_failed(reply, reason)) {
          PT_ERROR("the worker could not load the model: " + reason);
          post_result(false, std::string(), reason);
          continue;
        }
        PT_INFO("model ready");
      } else {
        lock.unlock();
        PT_INFO("reusing the worker (the model is already loaded)");
      }

      std::string begin_reply;
      std::string reason;
      if (!send_command("begin", begin_reply) || reply_failed(begin_reply, reason)) {
        post_result(false, std::string(), "the worker refused to start the stream");
        continue;
      }

      // Catch-up: everything captured while the model was loading goes in now.
      flush_audio();
      continue;
    }

    if (g_shared.stop_requested) {
      bool cancelled = false;
      {
        std::lock_guard<std::mutex> lock(g_shared.mutex);
        g_shared.stop_requested = false;
        cancelled = g_shared.cancel_requested;
        g_shared.cancel_requested = false;
      }

      if (cancelled) {
        // The take is discarded: drop the audio and reset the stream.
        {
          std::lock_guard<std::mutex> lock(g_shared.mutex);
          g_shared.buffer.clear();
          g_shared.sent = 0;
        }
        std::string reply;
        if (g_worker_active) send_command("reset", reply);
        post_result(true, std::string(), std::string());
        continue;
      }

      std::string reply;
      std::string text;
      if (g_worker_active) {
        flush_audio();
        if (send_command("finalize", reply)) {
          std::string reason;
          if (!reply_failed(reply, reason)) text = reply_text(reply);
        }
        if (text.empty()) {
          // The stream found nothing: fall back to a one-shot decode of the
          // same audio (Handy does the same). The worker already has the
          // audio, so nothing is sent twice.
          PT_INFO("the live stream produced no text; falling back to a batch decode");
          if (send_command("batch", reply)) {
            std::string reason;
            if (!reply_failed(reply, reason)) text = reply_text(reply);
          }
        }
      }

      {
        std::lock_guard<std::mutex> lock(g_shared.mutex);
        g_shared.last_activity = std::chrono::steady_clock::now();
      }
      post_result(true, text, std::string());
      continue;
    }

    // New audio to send.
    flush_audio();
  }

  g_worker.stop();
  g_worker_active = false;
}

// ---------------------------------------------------------------------------
// UI thread
// ---------------------------------------------------------------------------

void apply_filler_rules(const std::string& raw, std::string& text) {
  text = raw;
  if (g_config.filler_removal) {
    text = pt::remove_filler_words(text, g_config.filler_words);
  }
  text = pt::tidy_whitespace(text);
  if (g_config.append_trailing_space) text = pt::append_trailing_space(text);
}

void start_dictation() {
  {
    std::lock_guard<std::mutex> lock(g_shared.mutex);
    g_shared.buffer.clear();
    g_shared.sent = 0;
    g_shared.overflow = false;
    g_shared.capturing = true;
    g_shared.start_requested = true;
  }
  g_shared.cv.notify_all();

  std::string error;
  if (!g_audio.start(&on_audio, error)) {
    PT_ERROR("cannot start the microphone: " + error);
    g_shared.state = AppState::Idle;
    g_mic.show(g_config.mic_button_edge, error);
    g_mic.set_state(pt::win32::MicButtonState::Attention, error);
    return;
  }

  {
    std::lock_guard<std::mutex> lock(g_shared.mutex);
    g_shared.last_activity = std::chrono::steady_clock::now();
  }
  g_shared.state = AppState::Recording;
  std::string show_error;
  if (!g_mic.show(g_config.mic_button_edge, show_error)) {
    PT_ERROR("cannot show the mic button: " + show_error);
  }
  g_mic.set_state(pt::win32::MicButtonState::Idle, "recording");
  PT_INFO("recording started");
}

void stop_dictation(bool cancelled) {
  if (g_shared.state != AppState::Recording) return;

  g_audio.stop();
  g_input_level = 0.0f;
  {
    std::lock_guard<std::mutex> lock(g_shared.mutex);
    g_shared.capturing = false;
    g_shared.stop_requested = true;
    g_shared.cancel_requested = cancelled;
  }
  g_shared.cv.notify_all();

  g_shared.state = AppState::Decoding;
  g_mic.set_state(pt::win32::MicButtonState::Working, cancelled ? "cancelled" : "transcribing");
}

void type_result(const std::string& text) {
  std::string final_text;
  apply_filler_rules(text, final_text);

  std::string error;
  if (!pt::win32::type_text(final_text, error)) {
    PT_ERROR("cannot type the text: " + error);
    g_mic.set_state(pt::win32::MicButtonState::Attention, error);
    return;
  }
  if (g_config.auto_submit) {
    pt::win32::press_enter(error);
  }
  PT_INFO("typed: " + final_text);
}

LRESULT CALLBACK window_proc(HWND hwnd, UINT message, WPARAM w_param, LPARAM l_param) {
  switch (message) {
    case WM_APP_TOGGLE:
      if (g_shared.state == AppState::Idle) {
        start_dictation();
      } else if (g_shared.state == AppState::Recording) {
        stop_dictation(false);
      }
      return 0;

    case WM_APP_STOP:
      stop_dictation(false);
      return 0;

    case WM_APP_CANCEL:
      stop_dictation(true);
      return 0;

    case WM_APP_QUIT:
      PostQuitMessage(0);
      return 0;

    case WM_APP_RESULT: {
      std::unique_ptr<Result> result(reinterpret_cast<Result*>(l_param));
      if (result == nullptr) return 0;

      g_shared.state = AppState::Idle;
      g_audio.stop();

      if (!result->ok) {
        // Something went wrong (no microphone, no model, ...): say so on the
        // button and stay idle.
        PT_ERROR(result->error);
        g_mic.show(g_config.mic_button_edge, result->error);
        g_mic.set_state(pt::win32::MicButtonState::Attention, result->error);
        return 0;
      }

      if (result->text.empty()) {
        g_mic.hide();
        PT_INFO("nothing was transcribed");
        return 0;
      }

      g_mic.hide();
      type_result(result->text);
      return 0;
    }

    case WM_TIMER:
      if (g_shared.state == AppState::Recording) {
        // Green while the microphone is picking you up, grey when it is not.
        const bool loud = g_input_level.load() > 0.008f;
        g_mic.set_state(loud ? pt::win32::MicButtonState::Active
                             : pt::win32::MicButtonState::Idle,
                        loud ? "listening" : "recording");
      }
      return 0;

    case WM_DESTROY:
      PostQuitMessage(0);
      return 0;

    default:
      break;
  }
  return DefWindowProcW(hwnd, message, w_param, l_param);
}

}  // namespace

int main(int argc, char** argv) {
  const std::string exe_dir = pt::win32::exe_dir();

  // The config lives next to the executable; the development layout has it
  // one level up (build/ next to the project root). --config overrides it.
  std::string config_path = pt::win32::join_path(exe_dir, "config.json");
  if (!file_exists(config_path)) {
    const std::string parent = pt::win32::join_path(exe_dir, "..\\config.json");
    if (file_exists(parent)) config_path = parent;
  }
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--config" && i + 1 < argc) {
      config_path = argv[++i];
    } else if (arg == "--render-icon") {
      // Writes the mic button to a BMP so the icon can be checked without a
      // screenshot.
      if (i + 1 >= argc) {
        std::fprintf(stderr, "missing value for --render-icon\n");
        return 2;
      }
      const std::string output = argv[++i];
      pt::win32::MicButton button;
      if (!button.render_to_file(output, pt::win32::MicButtonState::Active)) {
        std::fprintf(stderr, "error: cannot write %s\n", output.c_str());
        return 1;
      }
      std::fprintf(stderr, "wrote %s\n", output.c_str());
      return 0;
    } else if (arg == "--help" || arg == "-h") {
      std::fprintf(stderr,
                   "V2T controller\n"
                   "  --config <path>    configuration file (default: config.json next to the exe)\n"
                   "  --render-icon <bmp>  write the mic button to a bitmap and exit\n");
      return 0;
    }
  }

  std::string config_error;
  pt::load_config(config_path, g_config, config_error);
  if (!config_error.empty()) {
    std::fprintf(stderr, "note: %s\n", config_error.c_str());
  }
  if (g_config.model_path.empty()) {
    std::fprintf(stderr,
                 "error: no model_path in %s\n"
                 "       set it to your Parakeet GGUF, for example:\n"
                 "       \"model_path\": \"C:/Users/.../parakeet-unified-en-0.6b-Q8_0.gguf\"\n",
                 config_path.c_str());
    return 2;
  }
  if (g_config.engine_dir.empty()) {
    g_config.engine_dir = pt::win32::join_path(exe_dir, "engine");
  }

  // The log lives in a folder next to the executable.
  const std::string log_path = pt::win32::join_path(exe_dir, g_config.log_path);
  const size_t slash = log_path.find_last_of("\\/");
  if (slash != std::string::npos) {
    CreateDirectoryA(log_path.substr(0, slash).c_str(), nullptr);
  }
  pt::log_set_file(log_path);
  pt::log_set_level(pt::LogLevel::Info);
  PT_INFO("V2T controller starting");

  // A hidden message window: the hotkey hook and the mic button talk to it.
  WNDCLASSEXW window_class;
  ZeroMemory(&window_class, sizeof(window_class));
  window_class.cbSize = sizeof(window_class);
  window_class.lpfnWndProc = &window_proc;
  window_class.hInstance = GetModuleHandleW(nullptr);
  window_class.lpszClassName = L"V2TController";
  RegisterClassExW(&window_class);

  g_window = CreateWindowExW(0, L"V2TController", L"", WS_OVERLAPPEDWINDOW, 0, 0, 1, 1,
                             nullptr, nullptr, window_class.hInstance, nullptr);
  if (g_window == nullptr) {
    std::fprintf(stderr, "error: cannot create the controller window\n");
    return 1;
  }

  // Right Ctrl + Right Alt toggles dictation; Esc cancels the take.
  g_hotkey.set_callbacks(
      [] { PostMessage(g_window, WM_APP_TOGGLE, 0, 0); },
      [] { PostMessage(g_window, WM_APP_CANCEL, 0, 0); });
  std::string hotkey_error;
  if (!g_hotkey.start(g_config.hotkey, g_config.cancel_key, hotkey_error)) {
    std::fprintf(stderr, "error: %s\n", hotkey_error.c_str());
    return 1;
  }

  // The mic button: click stops the recording, right-click quits.
  g_mic.set_callbacks(
      [] { PostMessage(g_window, WM_APP_STOP, 0, 0); },
      [] { PostMessage(g_window, WM_APP_QUIT, 0, 0); });

  // 10 Hz is plenty for the level meter and costs nothing.
  SetTimer(g_window, 1, 100, nullptr);

  g_pipe_name = "v2t-" + std::to_string(GetCurrentProcessId());
  g_worker_thread = std::thread(worker_thread_main);

  MSG message;
  while (GetMessageW(&message, nullptr, 0, 0) > 0) {
    TranslateMessage(&message);
    DispatchMessageW(&message);
  }

  PT_INFO("shutting down");
  g_audio.stop();
  {
    std::lock_guard<std::mutex> lock(g_shared.mutex);
    g_shared.quit_requested = true;
  }
  g_shared.cv.notify_all();
  if (g_worker_thread.joinable()) g_worker_thread.join();
  g_hotkey.stop();
  return 0;
}
