// Binding for the transcribe-cpp C ABI (transcribe.dll v0.4.0).
//
// This is the only place in the project that knows the engine exists.
// The DLL is loaded at runtime with LoadLibrary/GetProcAddress: no import
// library, no link-time dependency, and the ABI version is validated before
// any call that could misbehave.
//
// The struct mirrors below come from the published header
// (crates.io: transcribe-cpp-sys 0.4.0, include/transcribe.h). Their sizes
// are checked against the library's own transcribe_abi_struct_size() at
// load time, so a mismatched DLL is refused instead of misread.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace pt {
namespace transcribe {

using Status = int;

// transcribe_status values (0.4.0 header).
enum : Status {
  kOk = 0,
  kErrInvalidArg = 1,
  kErrNotImplemented = 2,
  kErrFileNotFound = 3,
  kErrGguf = 4,
  kErrUnsupportedArch = 5,
  kErrUnsupportedVariant = 6,
  kErrOom = 7,
  kErrBackend = 8,
  kErrSampleRate = 9,
  kErrUnsupportedLanguage = 10,
  kErrUnsupportedTask = 11,
  kErrUnsupportedTimestamps = 12,
  kErrAborted = 13,
  kErrBadStructSize = 14,
  kErrUnsupportedPnc = 15,
  kErrUnsupportedItn = 16,
  kErrInputTooLong = 17,
  kErrOutputTruncated = 18,
  kErrOutputRepetition = 19,
  kErrUnsupportedRole = 20,
  kErrInputTooShort = 21,
};

// transcribe_backend_request values.
enum Backend {
  kBackendAuto = 0,
  kBackendCpu = 1,
  kBackendMetal = 2,
  kBackendVulkan = 3,
  kBackendCpuAccel = 4,
  kBackendCuda = 5,
  kBackendRocm = 6,
};

// transcribe_abi_struct values we use for validation.
enum AbiStruct {
  kAbiModelLoadParams = 0,
  kAbiSessionParams = 1,
  kAbiRunParams = 2,
  kAbiStreamParams = 3,
  kAbiCapabilities = 4,
  kAbiStreamUpdate = 9,
  kAbiStreamText = 10,
};

struct ModelLoadParams {
  uint64_t struct_size;
  int32_t backend;
  void* device;
};

struct SessionParams {
  uint64_t struct_size;
  int32_t n_threads;
  int32_t kv_type;
  int32_t n_ctx;
};

// transcribe_stream_text, as the shipped engine reports it.
//
// The engine that ships with Handy is not byte-identical to the published
// crates.io release: it reports 32 bytes for this struct where the published
// header describes 64. The struct_size protocol keeps this safe — the library
// only writes the prefix that fits both layouts, and only the leading fields
// (struct_size, full_text, full_text_bytes) are read back.
struct StreamText {
  uint64_t struct_size;
  const char* full_text;
  uint64_t full_text_bytes;
  const char* committed_text;
  uint64_t committed_text_bytes;
  const char* tentative_text;
  uint64_t tentative_text_bytes;
  uint64_t raw_tentative_start_bytes;
};

static_assert(sizeof(ModelLoadParams) == 24, "model load params layout mismatch");
static_assert(sizeof(SessionParams) == 24, "session params layout mismatch");

// The ABI version this binding is written against.
constexpr const char* kExpectedVersion = "0.4.0";

class Engine {
 public:
  Engine() = default;
  ~Engine();

  Engine(const Engine&) = delete;
  Engine& operator=(const Engine&) = delete;

  // Loads the engine DLL and validates its version and struct layouts.
  // `backend` narrows the set of compute backends the engine may register
  // (TRANSCRIBE_BACKEND_MASK_*); it has to be known *before* the DLL is
  // loaded, because the engine fixes the mask when it initialises.
  bool load(const std::string& dll_path, int backend, std::string& err);
  bool is_loaded() const { return lib_ != nullptr; }

  // Loads a model and opens a session against it. The session owns the model.
  bool open_session(const std::string& model_path, int backend, std::string& err);

  // Registers the compute backends. Must be called once before the first
  // model load: the engine does not register backends on its own.
  // `backend` narrows the allowed set (CPU only, CPU+Vulkan, ...).
  bool init_backends(int backend, std::string& err);
  bool has_session() const { return session_ != nullptr; }
  void close_session();

  // Runs one transcription over mono float32 PCM at 16 kHz.
  bool run(const float* pcm, int n_samples, std::string& err);

  // --- Live streaming decode (the path Handy uses for this model) ---------
  //
  // The model decodes the audio as it arrives, so the transcript is ready as
  // soon as the last frame is fed.
  bool stream_begin(std::string& err);
  bool stream_feed(const float* pcm, int n_samples, std::string& err);
  bool stream_finalize(std::string& err);
  std::string stream_text() const;
  void stream_reset();

  // The transcript of the last run (owned by the session).
  std::string full_text() const;

  std::string version() const;
  std::string status_string(Status status) const;

  // Authoritative size of one of the library's public structs, for ABI
  // validation. Returns 0 for an id this library does not know.
  size_t abi_struct_size(int which) const;

  // Routes the library's and ggml's diagnostics into the caller's sink.
  // Call once, before any model is loaded. Pass nullptr to silence them.
  using LogCallback = void (*)(int level, const char* message, void* userdata);
  bool set_log_sink(LogCallback callback, void* userdata, std::string& err);

 private:
  void* lib_ = nullptr;
  void* session_ = nullptr;
  std::string original_cwd_;
  bool have_original_cwd_ = false;

  const char* (*fn_version_)() = nullptr;
  const char* (*fn_status_string_)(Status) = nullptr;
  size_t (*fn_abi_struct_size_)(int) = nullptr;
  void (*fn_model_load_params_init_)(ModelLoadParams*) = nullptr;
  void (*fn_session_params_init_)(SessionParams*) = nullptr;
  Status (*fn_open_)(const char*, const ModelLoadParams*, const SessionParams*, void**) = nullptr;
  Status (*fn_run_)(void*, const float*, int, const void*) = nullptr;
  const char* (*fn_full_text_)(const void*) = nullptr;
  void (*fn_session_free_)(void*) = nullptr;
  void (*fn_log_set_)(void (*)(int, const char*, void*), void*) = nullptr;
  Status (*fn_init_backends_default_)() = nullptr;
  Status (*fn_init_backends_)(const char*) = nullptr;
  uint32_t (*fn_allowed_backends_)() = nullptr;
  Status (*fn_stream_begin_)(void*, const void*, const void*) = nullptr;
  Status (*fn_stream_feed_)(void*, const float*, int, void*) = nullptr;
  Status (*fn_stream_finalize_)(void*, void*) = nullptr;
  Status (*fn_stream_get_text_)(const void*, StreamText*) = nullptr;
  void (*fn_stream_reset_)(void*) = nullptr;
  bool backends_initialised_ = false;
  uint32_t allowed_backends_mask_ = 0;
  std::string engine_dll_path_;
  int requested_backend_ = kBackendCpu;

 public:
  // The backend mask the engine settled on (TRANSCRIBE_BACKEND_MASK_*).
  uint32_t allowed_backends() const { return allowed_backends_mask_; }
};

// Maps a backend name ("cpu", "vulkan", "auto") to its enum value.
bool backend_from_name(const std::string& name, int& out_backend);

}  // namespace transcribe
}  // namespace pt
