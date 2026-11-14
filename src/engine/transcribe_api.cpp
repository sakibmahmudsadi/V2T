#include "engine/transcribe_api.h"

#include <cstring>

#if defined(_WIN32)
#include <windows.h>
#endif

namespace pt {
namespace transcribe {
namespace {

// Directory part of a path, without the trailing separator.
std::string parent_dir(const std::string& path) {
  const size_t pos = path.find_last_of("\\/");
  if (pos == std::string::npos) return std::string(".");
  if (pos == 0) return std::string(1, path[0]);
  return path.substr(0, pos);
}

#if defined(_WIN32)
template <typename Fn>
bool bind(HMODULE lib, const char* name, Fn& slot, std::string& err) {
  slot = reinterpret_cast<Fn>(GetProcAddress(lib, name));
  if (slot == nullptr) {
    err = std::string("engine DLL is missing the export '") + name + "'";
    return false;
  }
  return true;
}
#else
template <typename Fn>
bool bind(void* lib, const char* name, Fn& slot, std::string& err) {
  (void)lib;
  err = std::string("engine loading is only implemented for Windows (missing '") + name + "')";
  return false;
}
#endif

}  // namespace

bool backend_from_name(const std::string& name, int& out_backend) {
  if (name == "cpu") {
    out_backend = kBackendCpu;
    return true;
  }
  if (name == "vulkan") {
    out_backend = kBackendVulkan;
    return true;
  }
  if (name == "auto") {
    out_backend = kBackendAuto;
    return true;
  }
  if (name == "cuda") {
    out_backend = kBackendCuda;
    return true;
  }
  if (name == "rocm") {
    out_backend = kBackendRocm;
    return true;
  }
  return false;
}

Engine::~Engine() {
  close_session();
#if defined(_WIN32)
  if (have_original_cwd_) {
    SetCurrentDirectoryA(original_cwd_.c_str());
    have_original_cwd_ = false;
  }
  if (lib_ != nullptr) {
    FreeLibrary(static_cast<HMODULE>(lib_));
    lib_ = nullptr;
  }
#endif
}

bool Engine::load(const std::string& dll_path, int backend, std::string& err) {
  if (lib_ != nullptr) return true;
  requested_backend_ = backend;

#if defined(_WIN32)
  // The engine reads TRANSCRIBE_BACKENDS to narrow which backends it may
  // register (registering a GPU backend runs driver code, and a broken
  // driver can take the process down before any model loads). This has to
  // happen before the DLL is loaded: the mask is fixed when the engine
  // initialises its backends.
  const char* allowed = nullptr;
  switch (backend) {
    case kBackendCpu: allowed = "cpu"; break;
    case kBackendVulkan: allowed = "cpu,vulkan"; break;
    case kBackendCuda: allowed = "cpu,cuda"; break;
    case kBackendRocm: allowed = "cpu,rocm"; break;
    default: allowed = nullptr; break;
  }
  if (allowed != nullptr && GetEnvironmentVariableA("TRANSCRIBE_BACKENDS", nullptr, 0) == 0) {
    SetEnvironmentVariableA("TRANSCRIBE_BACKENDS", allowed);
  }
  // Note: the engine reads this variable through its own CRT instance, which
  // caches the environment when the DLL loads. A value set here therefore
  // only takes effect if the variable already existed when the *process*
  // started — the controller sets it in the worker's environment when it
  // spawns the worker (see AGENTS.md, "Backend mask").
#endif

#if defined(_WIN32)
  // The engine loads its compute backends (ggml-cpu-*.dll, ggml-vulkan.dll)
  // by name at runtime, so the engine folder has to be on the process DLL
  // search path — not just the directory of transcribe.dll.
  const std::string engine_dir = parent_dir(dll_path);
  engine_dll_path_ = dll_path;
  SetDllDirectoryA(engine_dir.c_str());

  // ggml finds its backends by *scanning* a directory for ggml-<name>-*.dll.
  // The directories it scans are the executable's folder and the current
  // working directory. The engine payload lives in its own folder, so point
  // the working directory there for as long as the engine is loaded (the
  // worker only ever uses absolute paths).
  char original_cwd[MAX_PATH];
  const DWORD n = GetCurrentDirectoryA(MAX_PATH, original_cwd);
  if (n > 0 && n < MAX_PATH) {
    original_cwd_ = std::string(original_cwd, n);
    have_original_cwd_ = true;
  }
  SetCurrentDirectoryA(engine_dir.c_str());

  // LOAD_WITH_ALTERED_SEARCH_PATH makes the engine folder the search path for
  // the engine's own dependencies (ggml.dll and friends), so the payload can
  // live in its own directory instead of next to the executable.
  HMODULE lib = LoadLibraryExA(dll_path.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
  if (lib == nullptr) {
    const DWORD code = GetLastError();
    err = "cannot load engine DLL '" + dll_path + "' (Windows error " +
          std::to_string(code) + ")";
    return false;
  }
  lib_ = lib;

  const bool ok =
      bind(static_cast<HMODULE>(lib_), "transcribe_version", fn_version_, err) &&
      bind(static_cast<HMODULE>(lib_), "transcribe_status_string", fn_status_string_, err) &&
      bind(static_cast<HMODULE>(lib_), "transcribe_abi_struct_size", fn_abi_struct_size_, err) &&
      bind(static_cast<HMODULE>(lib_), "transcribe_model_load_params_init", fn_model_load_params_init_, err) &&
      bind(static_cast<HMODULE>(lib_), "transcribe_session_params_init", fn_session_params_init_, err) &&
      bind(static_cast<HMODULE>(lib_), "transcribe_open", fn_open_, err) &&
      bind(static_cast<HMODULE>(lib_), "transcribe_run", fn_run_, err) &&
      bind(static_cast<HMODULE>(lib_), "transcribe_full_text", fn_full_text_, err) &&
      bind(static_cast<HMODULE>(lib_), "transcribe_session_free", fn_session_free_, err) &&
      bind(static_cast<HMODULE>(lib_), "transcribe_log_set", fn_log_set_, err) &&
      bind(static_cast<HMODULE>(lib_), "transcribe_init_backends_default", fn_init_backends_default_, err) &&
      bind(static_cast<HMODULE>(lib_), "transcribe_init_backends", fn_init_backends_, err) &&
      bind(static_cast<HMODULE>(lib_), "transcribe_allowed_backends", fn_allowed_backends_, err) &&
      bind(static_cast<HMODULE>(lib_), "transcribe_stream_begin", fn_stream_begin_, err) &&
      bind(static_cast<HMODULE>(lib_), "transcribe_stream_feed", fn_stream_feed_, err) &&
      bind(static_cast<HMODULE>(lib_), "transcribe_stream_finalize", fn_stream_finalize_, err) &&
      bind(static_cast<HMODULE>(lib_), "transcribe_stream_get_text", fn_stream_get_text_, err) &&
      bind(static_cast<HMODULE>(lib_), "transcribe_stream_reset", fn_stream_reset_, err);  if (!ok) {
    FreeLibrary(lib);
    lib_ = nullptr;
    return false;
  }

  const std::string found = version();
  if (found != kExpectedVersion) {
    err = "engine ABI version mismatch: expected " + std::string(kExpectedVersion) +
          ", found " + found;
    FreeLibrary(lib);
    lib_ = nullptr;
    return false;
  }

  const size_t load_params_size = fn_abi_struct_size_(kAbiModelLoadParams);
  const size_t session_params_size = fn_abi_struct_size_(kAbiSessionParams);
  if (load_params_size != sizeof(ModelLoadParams) ||
      session_params_size != sizeof(SessionParams)) {
    err = "engine ABI struct size mismatch: model_load_params " +
          std::to_string(load_params_size) + " (expected " +
          std::to_string(sizeof(ModelLoadParams)) + "), session_params " +
          std::to_string(session_params_size) + " (expected " +
          std::to_string(sizeof(SessionParams)) + ")";
    FreeLibrary(lib);
    lib_ = nullptr;
    return false;
  }
  return true;
#else
  (void)dll_path;
  err = "engine loading is only implemented for Windows";
  return false;
#endif
}

bool Engine::init_backends(int backend, std::string& err) {
  if (lib_ == nullptr) {
    err = "engine DLL is not loaded";
    return false;
  }
  if (backends_initialised_) return true;

  // The default helper resolves the directory of the loaded transcribe.dll, so
  // the backend modules are found next to the engine and nowhere else.
  Status status = fn_init_backends_default_();
  if (status != kOk) {
    // Fall back to an explicit directory (the engine's own folder).
    const std::string engine_dir = parent_dir(engine_dll_path_);
    status = fn_init_backends_(engine_dir.c_str());
  }
  if (status != kOk) {
    err = "cannot initialise compute backends: " + status_string(status);
    return false;
  }

  backends_initialised_ = true;
  if (fn_allowed_backends_ != nullptr) {
    allowed_backends_mask_ = fn_allowed_backends_();
  }
  return true;
}

bool Engine::open_session(const std::string& model_path, int backend, std::string& err) {
  if (lib_ == nullptr) {
    err = "engine DLL is not loaded";
    return false;
  }
  if (session_ != nullptr) {
    err = "a session is already open";
    return false;
  }

  ModelLoadParams load_params;
  std::memset(&load_params, 0, sizeof(load_params));
  fn_model_load_params_init_(&load_params);
  load_params.backend = backend;
  load_params.device = nullptr;

  SessionParams session_params;
  std::memset(&session_params, 0, sizeof(session_params));
  fn_session_params_init_(&session_params);
  session_params.n_threads = 0;  // library picks a sensible default

  void* session = nullptr;
  Status status =
      fn_open_(model_path.c_str(), &load_params, &session_params, &session);

  // A model file can be momentarily unavailable (a sync client, a scanner, a
  // network drive). One retry turns a transient failure into a delay instead
  // of a lost dictation.
  if (status != kOk && session == nullptr) {
#if defined(_WIN32)
    Sleep(300);
#endif
    status = fn_open_(model_path.c_str(), &load_params, &session_params, &session);
  }

  if (status != kOk || session == nullptr) {
    err = "cannot open model '" + model_path + "': " + status_string(status);
    return false;
  }
  session_ = session;
  return true;
}

void Engine::close_session() {
  if (session_ != nullptr && fn_session_free_ != nullptr) {
    fn_session_free_(session_);
  }
  session_ = nullptr;
}

bool Engine::run(const float* pcm, int n_samples, std::string& err) {
  if (session_ == nullptr) {
    err = "no session is open";
    return false;
  }
  if (pcm == nullptr || n_samples <= 0) {
    err = "no audio to transcribe";
    return false;
  }

  const Status status = fn_run_(session_, pcm, n_samples, nullptr);
  if (status != kOk) {
    err = "transcription failed: " + status_string(status);
    return false;
  }
  return true;
}

std::string Engine::full_text() const {
  if (session_ == nullptr || fn_full_text_ == nullptr) return std::string();
  const char* text = fn_full_text_(session_);
  return text != nullptr ? std::string(text) : std::string();
}

bool Engine::stream_begin(std::string& err) {
  if (session_ == nullptr) {
    err = "no session is open";
    return false;
  }
  // NULL params: the library's defaults (transcribe task, automatic commit
  // policy, the family's own streaming strategy) — the same setup Handy uses.
  const Status status = fn_stream_begin_(session_, nullptr, nullptr);
  if (status != kOk) {
    err = "cannot start the live stream: " + status_string(status);
    return false;
  }
  return true;
}

bool Engine::stream_feed(const float* pcm, int n_samples, std::string& err) {
  if (session_ == nullptr) {
    err = "no session is open";
    return false;
  }
  if (pcm == nullptr || n_samples <= 0) {
    err = "no audio to feed";
    return false;
  }
  const Status status = fn_stream_feed_(session_, pcm, n_samples, nullptr);
  if (status != kOk) {
    err = "streaming feed failed: " + status_string(status);
    return false;
  }
  return true;
}

bool Engine::stream_finalize(std::string& err) {
  if (session_ == nullptr) {
    err = "no session is open";
    return false;
  }
  const Status status = fn_stream_finalize_(session_, nullptr);
  if (status != kOk) {
    err = "streaming finalize failed: " + status_string(status);
    return false;
  }
  return true;
}

std::string Engine::stream_text() const {
  if (session_ == nullptr || fn_stream_get_text_ == nullptr) return std::string();

  StreamText text;
  std::memset(&text, 0, sizeof(text));
  // The library writes only what fits its own layout; the shipped engine
  // reports 32 bytes here, the published header describes 64.
  text.struct_size = fn_abi_struct_size_(kAbiStreamText);
  if (text.struct_size == 0 || text.struct_size > sizeof(text)) {
    text.struct_size = sizeof(text);
  }

  const Status status = fn_stream_get_text_(session_, &text);
  if (status != kOk || text.full_text == nullptr) return std::string();

  const size_t bytes = static_cast<size_t>(text.full_text_bytes);
  if (bytes == 0 || bytes > (16u << 20)) return std::string();
  return std::string(text.full_text, bytes);
}

void Engine::stream_reset() {
  if (session_ != nullptr && fn_stream_reset_ != nullptr) {
    fn_stream_reset_(session_);
  }
}

std::string Engine::version() const {
  if (fn_version_ == nullptr) return std::string();
  const char* v = fn_version_();
  return v != nullptr ? std::string(v) : std::string();
}

std::string Engine::status_string(Status status) const {
  if (fn_status_string_ == nullptr) return "status " + std::to_string(status);
  const char* s = fn_status_string_(status);
  return s != nullptr ? std::string(s) : "status " + std::to_string(status);
}

size_t Engine::abi_struct_size(int which) const {
  if (fn_abi_struct_size_ == nullptr) return 0;
  return fn_abi_struct_size_(which);
}

bool Engine::set_log_sink(LogCallback callback, void* userdata, std::string& err) {
  if (fn_log_set_ == nullptr) {
    err = "engine DLL does not export transcribe_log_set";
    return false;
  }
  fn_log_set_(reinterpret_cast<void (*)(int, const char*, void*)>(callback), userdata);
  return true;
}

}  // namespace transcribe
}  // namespace pt
