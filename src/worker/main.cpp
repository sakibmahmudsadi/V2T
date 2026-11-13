// ParakeetType worker — the model host.
//
// Milestone 1 shape: a command line tool that loads the local Parakeet model
// through transcribe.dll and transcribes a WAV file, fully offline.
//
//   ParakeetTypeWorker --model <gguf> --file <wav> [--backend cpu|vulkan|auto]
//                        [--no-filler] [--json]
//   ParakeetTypeWorker --selftest
//
// Later milestones turn this into the long-lived worker process that the
// controller talks to over a pipe; the engine binding and the text rules are
// already the final ones.
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "core/log.h"
#include "core/text.h"
#include "core/wav.h"
#include "engine/transcribe_api.h"
#include "platform/win32/paths.h"

namespace {

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

// Prints everything the engine and ggml have to say. Installed before any
// model is loaded, as the library requires.
void engine_log_sink(int level, const char* message, void* userdata) {
  (void)userdata;
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
    const bool loaded = engine.load(engine_dll, err);
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

  pt::transcribe::Engine engine;
  const Clock::time_point t_load = Clock::now();
  if (!engine.load(engine_dll, err)) return fail(err);
  const std::string engine_version = engine.version();
  if (verbose) {
    // The library wants its sink installed once, before any model is loaded.
    std::string log_err;
    if (!engine.set_log_sink(&engine_log_sink, nullptr, log_err)) {
      std::fprintf(stderr, "warning: %s\n", log_err.c_str());
    }
  }
  if (!engine.init_backends(backend, err)) return fail(err);
  if (!engine.open_session(model_path, backend, err)) return fail(err);
  const double load_ms = ms_since(t_load);

  const Clock::time_point t_run = Clock::now();
  std::string raw;
  std::string mode;

  if (batch) {
    if (!engine.run(audio.samples.data(), static_cast<int>(audio.samples.size()), err)) {
      engine.close_session();
      return fail(err);
    }
    raw = engine.full_text();
    mode = "batch";
  } else {
    // Live streaming decode: the same path Handy uses for this model, fed in
    // 30 ms frames.
    constexpr int kFrameSamples = 480;  // 30 ms at 16 kHz
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
      // A stream can miss what a run over the whole recording finds, so fall
      // back to a single batch run over the same audio (Handy does the same).
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
