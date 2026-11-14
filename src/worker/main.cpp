// ParakeetType worker — the model host.
//
// Two modes:
//   CLI     ParakeetTypeWorker --model <gguf> --file <wav> [--backend cpu|vulkan|auto]
//                              [--no-filler] [--json]
//   Server  ParakeetTypeWorker --serve --pipe <name>   (the controller's worker)
//
// In server mode the worker owns the model and decodes the audio the
// controller feeds it, live, over a named pipe. The engine binding and the
// text rules are the same in both modes.
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "core/ipc.h"
#include "core/json.h"
#include "core/log.h"
#include "core/text.h"
#include "core/wav.h"
#include "engine/transcribe_api.h"
#include "platform/win32/paths.h"

#if defined(_WIN32)
#include <windows.h>
#endif

namespace {

namespace json = pt::json;
namespace ipc = pt::ipc;

using Clock = std::chrono::steady_clock;

double ms_since(Clock::time_point start) {
  return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

void usage() {
  std::fprintf(stderr,
               "ParakeetType worker\n"
               "  --model <gguf>      model file (Parakeet GGUF)\n"
               "  --file <wav>        16-bit or 32-bit float WAV file\n"
               "  --backend <name>    cpu | vulkan | auto (default cpu)\n"
               "  --engine <dll>      engine DLL (default <exe dir>/engine/transcribe.dll)\n"
               "  --serve             run as the controller's worker\n"
               "  --pipe <name>       named pipe to serve on (with --serve)\n"
               "  --no-filler         do not strip filler words\n"
               "  --json              machine-readable output\n"
               "  --selftest          run the built-in checks and exit\n");
}

// Builds a WAV buffer in memory: 16-bit PCM mono at the given rate.
std::vector<uint8_t> make_wav(int sample_rate, const std::vector<float>& samples) {
  std::vector<uint8_t> out;
  const uint32_t data_bytes = static_cast<uint32_t>(samples.size() * 2);
  const uint32_t riff_size = 36 + data_bytes;

  auto put32 = [&out](uint32_t v) {
    out.push_back(static_cast<uint8_t>(v & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
  };
  auto put16 = [&out](uint16_t v) {
    out.push_back(static_cast<uint8_t>(v & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
  };
  auto put_tag = [&out](const char* tag) {
    for (int i = 0; i < 4; ++i) out.push_back(static_cast<uint8_t>(tag[i]));
  };

  put_tag("RIFF");
  put32(riff_size);
  put_tag("WAVE");
  put_tag("fmt ");
  put32(16);
  put16(1);  // PCM
  put16(1);  // mono
  put32(static_cast<uint32_t>(sample_rate));
  put32(static_cast<uint32_t>(sample_rate * 2));
  put16(2);  // block align
  put16(16);  // bits per sample
  put_tag("data");
  put32(data_bytes);
  for (float s : samples) {
    float clamped = s;
    if (clamped > 1.0f) clamped = 1.0f;
    if (clamped < -1.0f) clamped = -1.0f;
    const int16_t v = static_cast<int16_t>(std::lround(clamped * 32767.0f));
    put16(static_cast<uint16_t>(v));
  }
  return out;
}

int fail(const std::string& message) {
  std::fprintf(stderr, "error: %s\n", message.c_str());
  return 1;
}

// Prints the engine's and ggml's diagnostics. By default only warnings and
// errors reach the console; --verbose shows everything.
void engine_log_sink(int level, const char* message, void* userdata) {
  const bool verbose = userdata != nullptr;
  // Without --verbose only warnings (2) and errors (3) reach the console.
  if (!verbose && level != 2 && level != 3) return;

  const char* name = "?";
  switch (level) {
    case 1: name = "INFO"; break;
    case 2: name = "WARN"; break;
    case 3: name = "ERROR"; break;
    case 4: name = "DEBUG"; break;
    case 5: name = "CONT"; break;
    default: break;
  }
  std::fprintf(stderr, "[engine %s] %s\n", name, message != nullptr ? message : "");
}

bool file_exists(const std::string& path) {
  std::FILE* f = std::fopen(path.c_str(), "rb");
  if (f == nullptr) return false;
  std::fclose(f);
  return true;
}

// Finds the engine DLL: next to the exe in engine/, or one level up (the
// development layout has build/ and engine/ side by side).
// Finds the engine DLL, in order of preference:
//   <exe dir>/engine/transcribe.dll   (shipped layout)
//   <exe dir>/../engine/transcribe.dll (development layout: build/ next to engine/)
//   <exe dir>/transcribe.dll          (flat layout)
std::string default_engine_path() {
  const std::string dir = pt::win32::exe_dir();
  const std::string candidates[] = {
      pt::win32::join_path(dir, "engine\\transcribe.dll"),
      pt::win32::join_path(dir, "..\\engine\\transcribe.dll"),
      pt::win32::join_path(dir, "transcribe.dll"),
  };
  for (const std::string& candidate : candidates) {
    if (file_exists(candidate)) return pt::win32::normalize_path(candidate);
  }
  return pt::win32::normalize_path(candidates[0]);
}

int selftest(const std::string& engine_dll) {
  int failures = 0;
  auto check = [&failures](bool condition, const char* what) {
    if (condition) {
      std::fprintf(stderr, "  ok   %s\n", what);
    } else {
      std::fprintf(stderr, "  FAIL %s\n", what);
      ++failures;
    }
  };

  std::fprintf(stderr, "selftest: core\n");

  // --- WAV decoding ------------------------------------------------------
  {
    std::vector<float> samples(1000, 0.5f);
    const auto wav = make_wav(16000, samples);
    pt::Audio audio;
    std::string err;
    const bool ok = pt::decode_wav(wav.data(), wav.size(), audio, err);
    check(ok && audio.samples.size() == 1000, "16 kHz mono 16-bit WAV decodes to 1000 samples");
    if (ok && !audio.samples.empty()) {
      check(std::fabs(audio.samples[0] - 0.5f) < 1e-4f, "sample value survives the round trip");
    }
  }
  {
    std::vector<float> samples(800, 0.25f);
    const auto wav = make_wav(8000, samples);
    pt::Audio audio;
    std::string err;
    const bool ok = pt::decode_wav(wav.data(), wav.size(), audio, err);
    check(ok && audio.samples.size() == 1600, "8 kHz WAV is resampled up to 16 kHz");
  }
  {
    pt::Audio audio;
    std::string err;
    const uint8_t junk[16] = {0};
    check(!pt::decode_wav(junk, sizeof(junk), audio, err), "garbage is rejected");
    check(!err.empty(), "rejection carries a reason");
  }

  // --- Text rules --------------------------------------------------------
  {
    const auto fillers = pt::default_filler_words();
    const std::string cleaned =
        pt::tidy_whitespace(pt::remove_filler_words("Um, hello uh world. Hmm.", fillers));
    check(cleaned == "hello world.", "filler words are removed with their punctuation");
    const std::string kept = pt::tidy_whitespace(
        pt::remove_filler_words("the umbrella is here", fillers));
    check(kept == "the umbrella is here", "a filler inside a word is not removed");
    check(pt::append_trailing_space("hi") == "hi ", "trailing space is appended");
    check(pt::append_trailing_space("hi ") == "hi ", "trailing space is not doubled");
  }

  // --- Engine ABI --------------------------------------------------------
  std::fprintf(stderr, "selftest: engine\n");
  {
    pt::transcribe::Engine engine;
    std::string err;
    const bool loaded = engine.load(engine_dll, pt::transcribe::kBackendCpu, err);
    check(loaded, "engine DLL loads");
    if (!loaded) {
      std::fprintf(stderr, "       %s\n", err.c_str());
    } else {
      check(engine.version() == pt::transcribe::kExpectedVersion, "engine ABI version is 0.4.0");
      check(engine.abi_struct_size(pt::transcribe::kAbiModelLoadParams) ==
                sizeof(pt::transcribe::ModelLoadParams),
            "model load params struct size matches");
      check(engine.abi_struct_size(pt::transcribe::kAbiSessionParams) ==
                sizeof(pt::transcribe::SessionParams),
            "session params struct size matches");
    }
  }

  if (failures == 0) {
    std::fprintf(stderr, "selftest: OK\n");
    return 0;
  }
  std::fprintf(stderr, "selftest: %d failure(s)\n", failures);
  return 1;
}

}  // namespace

// ---------------------------------------------------------------------------
// Server mode: the controller's worker.
// ---------------------------------------------------------------------------

#if defined(_WIN32)

std::wstring widen(const std::string& value) {
  if (value.empty()) return std::wstring();
  const int needed = MultiByteToWideChar(CP_UTF8, 0, value.c_str(),
                                          static_cast<int>(value.size()), nullptr, 0);
  std::wstring result(static_cast<size_t>(needed), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), &result[0],
                      needed);
  return result;
}

// Connects to the pipe the controller created. The controller may still be
// starting up, so retry for a few seconds.
HANDLE connect_to_pipe(const std::string& pipe_name, std::string& err) {
  const std::wstring path = L"\\\\.\\pipe\\" + widen(pipe_name);
  for (int attempt = 0; attempt < 100; ++attempt) {
    HANDLE pipe = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                               OPEN_EXISTING, 0, nullptr);
    if (pipe != INVALID_HANDLE_VALUE) return pipe;
    Sleep(50);
  }
  err = "cannot connect to the controller pipe '" + pipe_name + "'";
  return INVALID_HANDLE_VALUE;
}

bool send_json(HANDLE pipe, const json::ValuePtr& value) {
  const std::string text = json::stringify(*value);
  return ipc::write_frame(pipe, ipc::kFrameControl, text.data(), text.size());
}

json::ValuePtr reply_ok() {
  json::ValuePtr value = json::make_object();
  json::set(value, "ok", json::make_bool(true));
  return value;
}

json::ValuePtr reply_error(const std::string& message) {
  json::ValuePtr value = json::make_object();
  json::set(value, "ok", json::make_bool(false));
  json::set(value, "error", json::make_string(message));
  return value;
}

int run_server(const std::string& pipe_name, const std::string& model_path,
               const std::string& backend_name, const std::string& engine_dll) {
  std::string err;
  HANDLE pipe = connect_to_pipe(pipe_name, err);
  if (pipe == INVALID_HANDLE_VALUE) {
    std::fprintf(stderr, "error: %s\n", err.c_str());
    return 1;
  }

  int backend = pt::transcribe::kBackendCpu;
  if (!backend_name.empty() && !pt::transcribe::backend_from_name(backend_name, backend)) {
    std::fprintf(stderr, "error: unknown backend '%s'\n", backend_name.c_str());
    CloseHandle(pipe);
    return 1;
  }

  pt::transcribe::Engine engine;
  if (!engine.load(engine_dll, backend, err)) {
    send_json(pipe, reply_error(err));
    std::fprintf(stderr, "error: %s\n", err.c_str());
    CloseHandle(pipe);
    return 1;
  }
  if (!engine.init_backends(backend, err)) {
    send_json(pipe, reply_error(err));
    CloseHandle(pipe);
    return 1;
  }

  bool model_loaded = false;
  bool streaming = false;
  std::vector<uint8_t> last_audio;

  while (true) {
    uint32_t kind = 0;
    std::vector<uint8_t> payload;
    if (!ipc::read_frame(pipe, kind, payload)) break;

    if (kind == ipc::kFrameAudio) {
      // The last audio frame is kept so a "batch" command can decode it as a
      // one-shot run (the fallback when a stream yields nothing).
      last_audio = payload;
      if (!streaming || payload.empty()) continue;
      const size_t count = payload.size() / sizeof(float);
      std::string feed_error;
      if (!engine.stream_feed(reinterpret_cast<const float*>(payload.data()),
                              static_cast<int>(count), feed_error)) {
        // No reply here: the controller is not reading at this point, and a
        // blocking write would deadlock the pipe.
        std::fprintf(stderr, "worker: feed failed: %s\n", feed_error.c_str());
      }
      continue;
    }

    if (kind != ipc::kFrameControl) continue;

    std::string parse_error;
    const json::ValuePtr message = json::parse(
        std::string(payload.begin(), payload.end()), parse_error);
    if (message == nullptr || !message->is_object()) {
      send_json(pipe, reply_error("malformed control message"));
      continue;
    }

    const json::Value* command = message->find("cmd");
    const std::string name = command != nullptr ? command->as_string() : std::string();

    if (name == "load") {
      const json::Value* model = message->find("model");
      const std::string path = model != nullptr ? model->as_string() : model_path;
      const Clock::time_point start = Clock::now();
      std::string open_error;
      if (!engine.open_session(path, backend, open_error)) {
        send_json(pipe, reply_error(open_error));
        continue;
      }
      model_loaded = true;
      json::ValuePtr reply = reply_ok();
      json::set(reply, "load_ms", json::make_number(ms_since(start)));
      send_json(pipe, reply);
      continue;
    }

    if (name == "begin") {
      if (!model_loaded) {
        send_json(pipe, reply_error("no model is loaded"));
        continue;
      }
      std::string begin_error;
      if (!engine.stream_begin(begin_error)) {
        send_json(pipe, reply_error(begin_error));
        continue;
      }
      streaming = true;
      send_json(pipe, reply_ok());
      continue;
    }

    if (name == "finalize") {
      if (!streaming) {
        send_json(pipe, reply_error("no stream is active"));
        continue;
      }
      std::string finalize_error;
      std::string text;
      if (engine.stream_finalize(finalize_error)) {
        text = engine.stream_text();
      }
      streaming = false;
      if (text.empty()) {
        // A stream can miss what a run over the whole recording finds; the
        // controller sends the buffer for a batch retry in that case.
        send_json(pipe, reply_error(text.empty() ? "no text" : finalize_error));
        continue;
      }
      json::ValuePtr reply = reply_ok();
      json::set(reply, "text", json::make_string(text));
      send_json(pipe, reply);
      continue;
    }

    if (name == "batch") {
      // Fallback path: the controller sends the whole buffer as one audio
      // frame and then asks for a one-shot decode of it.
      if (!model_loaded || last_audio.empty()) {
        send_json(pipe, reply_error("no audio to decode"));
        continue;
      }
      const size_t count = last_audio.size() / sizeof(float);
      std::string run_error;
      if (!engine.run(reinterpret_cast<const float*>(last_audio.data()),
                      static_cast<int>(count), run_error)) {
        send_json(pipe, reply_error(run_error));
        continue;
      }
      json::ValuePtr reply = reply_ok();
      json::set(reply, "text", json::make_string(engine.full_text()));
      send_json(pipe, reply);
      continue;
    }

    if (name == "reset") {
      // The take was cancelled: drop the stream and start clean.
      engine.stream_reset();
      streaming = false;
      send_json(pipe, reply_ok());
      continue;
    }

    if (name == "shutdown") {
      send_json(pipe, reply_ok());
      break;
    }

    send_json(pipe, reply_error("unknown command '" + name + "'"));
  }

  engine.close_session();
  CloseHandle(pipe);
  return 0;
}

#endif  // _WIN32

int main(int argc, char** argv) {
  std::string model_path;
  std::string file_path;
  std::string engine_dll;
  std::string backend_name = "cpu";
  bool no_filler = false;
  bool json = false;
  bool run_selftest = false;
  bool verbose = false;
  bool batch = false;
  int repeat = 1;
  bool serve = false;
  std::string pipe_name;

  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    auto next = [&](std::string& target) -> bool {
      if (i + 1 >= argc) return false;
      target = argv[++i];
      return true;
    };
    bool ok = true;
    if (arg == "--model") {
      ok = next(model_path);
    } else if (arg == "--file") {
      ok = next(file_path);
    } else if (arg == "--engine") {
      ok = next(engine_dll);
    } else if (arg == "--backend") {
      ok = next(backend_name);
    } else if (arg == "--no-filler") {
      no_filler = true;
    } else if (arg == "--json") {
      json = true;
    } else if (arg == "--selftest") {
      run_selftest = true;
    } else if (arg == "--verbose") {
      verbose = true;
    } else if (arg == "--batch") {
      batch = true;
    } else if (arg == "--serve") {
      serve = true;
    } else if (arg == "--pipe") {
      ok = next(pipe_name);
    } else if (arg == "--repeat") {
      std::string value;
      if (!next(value)) {
        std::fprintf(stderr, "missing value for --repeat\n");
        return 2;
      }
      repeat = std::atoi(value.c_str());
      if (repeat < 1) repeat = 1;
    } else if (arg == "--help" || arg == "-h") {
      usage();
      return 0;
    } else {
      std::fprintf(stderr, "unknown argument: %s\n", arg.c_str());
      usage();
      return 2;
    }
    if (!ok) {
      std::fprintf(stderr, "missing value for %s\n", arg.c_str());
      return 2;
    }
  }

  if (engine_dll.empty()) {
    engine_dll = default_engine_path();
  }

  if (run_selftest) return selftest(engine_dll);

#if defined(_WIN32)
  if (serve) {
    if (pipe_name.empty()) {
      std::fprintf(stderr, "error: --serve needs --pipe <name>\n");
      return 2;
    }
    return run_server(pipe_name, model_path, backend_name, engine_dll);
  }
#else
  if (serve) {
    std::fprintf(stderr, "error: --serve is only implemented for Windows\n");
    return 2;
  }
#endif

  if (model_path.empty() || file_path.empty()) {
    usage();
    return 2;
  }

  int backend = pt::transcribe::kBackendCpu;
  if (!pt::transcribe::backend_from_name(backend_name, backend)) {
    return fail("unknown backend '" + backend_name + "'");
  }

  pt::Audio audio;
  std::string err;
  if (!pt::load_wav(file_path, audio, err)) return fail(err);
  if (audio.samples.empty()) return fail("no audio samples in " + file_path);
  // The encoder needs a few hundred milliseconds to produce a frame; below
  // that the engine can only fail, so say so plainly.
  if (audio.samples.size() < static_cast<size_t>(pt::kEngineSampleRate / 4)) {
    return fail("audio is too short to transcribe (needs at least 0.25 s)");
  }

  pt::transcribe::Engine engine;
  const Clock::time_point t_load = Clock::now();
  if (!engine.load(engine_dll, backend, err)) return fail(err);
  const std::string engine_version = engine.version();
  {
    // The library wants its sink installed once, before any model is loaded.
    std::string log_err;
    if (!engine.set_log_sink(&engine_log_sink, verbose ? reinterpret_cast<void*>(1) : nullptr,
                             log_err)) {
      std::fprintf(stderr, "warning: %s\n", log_err.c_str());
    }
  }
  if (!engine.init_backends(backend, err)) return fail(err);
  if (!engine.open_session(model_path, backend, err)) return fail(err);
  const double load_ms = ms_since(t_load);

  const Clock::time_point t_run = Clock::now();
  std::string raw;
  std::string mode;
  double last_run_ms = 0.0;

  // One pass over the audio. `--repeat` runs it several times in the same
  // session, which is how the controller will behave: the model stays loaded
  // between takes.
  for (int iteration = 0; iteration < repeat; ++iteration) {
    const Clock::time_point t_iteration = Clock::now();
    raw.clear();
    mode.clear();

    if (batch) {
      if (!engine.run(audio.samples.data(), static_cast<int>(audio.samples.size()), err)) {
        engine.close_session();
        return fail(err);
      }
      raw = engine.full_text();
      mode = "batch";
    } else {
      // Live streaming decode: the same path Handy uses for this model, fed
      // in 30 ms frames.
      constexpr int kFrameSamples = 480;  // 30 ms at 16 kHz
      engine.stream_reset();
      bool ok = engine.stream_begin(err);
      if (ok) {
        const std::vector<float>& samples = audio.samples;
        for (size_t offset = 0; offset < samples.size(); offset += kFrameSamples) {
          const size_t count = std::min<size_t>(kFrameSamples, samples.size() - offset);
          if (!engine.stream_feed(samples.data() + offset, static_cast<int>(count), err)) {
            ok = false;
            break;
          }
        }
      }
      if (ok) ok = engine.stream_finalize(err);
      if (ok) raw = engine.stream_text();
      mode = "stream";

      if (!ok || raw.empty()) {
        // A stream can miss what a run over the whole recording finds, so
        // fall back to a single batch run (Handy does the same).
        if (!ok) {
          std::fprintf(stderr, "note: streaming failed (%s); falling back to a batch run\n",
                       err.c_str());
        } else {
          std::fprintf(stderr, "note: streaming produced no text; falling back to a batch run\n");
        }
        engine.stream_reset();
        if (!engine.run(audio.samples.data(), static_cast<int>(audio.samples.size()), err)) {
          engine.close_session();
          return fail(err);
        }
        raw = engine.full_text();
        mode = "batch (fallback)";
      }
    }
    last_run_ms = ms_since(t_iteration);
  }
  const double run_ms = ms_since(t_run);

  std::string text = raw;
  if (!no_filler) {
    text = pt::remove_filler_words(text, pt::default_filler_words());
  }
  text = pt::tidy_whitespace(text);
  const std::string typed = pt::append_trailing_space(text);

  engine.close_session();

  if (json) {
    std::printf("{\n");
    std::printf("  \"raw\": \"%s\",\n", raw.c_str());
    std::printf("  \"text\": \"%s\",\n", typed.c_str());
    std::printf("  \"load_ms\": %.1f,\n", load_ms);
    std::printf("  \"run_ms\": %.1f,\n", run_ms);
    std::printf("  \"last_run_ms\": %.1f,\n", last_run_ms);
    std::printf("  \"repeat\": %d,\n", repeat);
    std::printf("  \"audio_seconds\": %.2f,\n", audio.seconds());
    std::printf("  \"backend\": \"%s\",\n", backend_name.c_str());
    std::printf("  \"mode\": \"%s\",\n", mode.c_str());
    std::printf("  \"engine_version\": \"%s\"\n", engine_version.c_str());
    std::printf("}\n");
  } else {
    std::printf("%s\n", text.c_str());
  }
  return 0;
}
