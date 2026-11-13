# Milestone 1 — Engine binding + offline transcription CLI

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Prove that ParakeetType's inference path works fully offline by binding `transcribe.dll` from the Handy install and transcribing WAV files from the command line, matching Handy's own output on the golden corpus.

**Architecture:** A `src/engine/transcribe_api` module owns the entire C ABI surface (`LoadLibrary` + `GetProcAddress`, no import lib). A `src/worker` binary is a thin CLI over it: it loads a GGUF model, opens a session, runs one WAV, prints the transcript. `src/core` holds portable, OS-free code (WAV decoding, text rules) so the same code can be reused by the controller in Milestone 2–3 and ported to Linux later.

**Tech Stack:** C++17, CMake + Ninja, MinGW-w64 g++ 14.2 (already at `C:\mingw64\bin`), `transcribe.dll` (transcribe-cpp v0.4.0) via a hand-written C ABI binding, Python 3 for the corpus comparison tool.

---

## File structure

| Path | Responsibility |
|---|---|
| `src/core/wav.h/.cpp` | Read a WAV file → mono float32 @ 16 kHz (handles 16-bit/32-bit-float, mono/stereo, resamples) |
| `src/core/text.h/.cpp` | Filler-word removal, whitespace tidy-up, trailing space — pure functions, no OS calls |
| `src/core/log.h/.cpp` | Minimal leveled logger to stderr and an optional log file |
| `src/engine/transcribe_api.h/.cpp` | The whole `transcribe.dll` C ABI: loader, struct mirrors, status→string |
| `src/worker/main.cpp` | CLI: `--model`, `--file`, `--backend`, `--no-filler`, `--json`, `--selftest` |
| `tools/corpus_check.py` | Runs the worker over the golden corpus, diffs against `history.db`, reports WER |
| `tools/setup_engine.sh` | Copies the engine DLLs out of the Handy install into `engine/` |
| `CMakeLists.txt` | Builds `ParakeetTypeWorker.exe` (controller arrives in Milestone 2) |
| `AGENTS.md` | Working agreement for agents on this repo |

## ABI facts this plan relies on (verified against the published 0.4.0 header)

- `transcribe_open(path, load_params, session_params, out_session)` — loads model + session in one
  call; `load_params`/`session_params` may be `NULL` for defaults. Session owns the model;
  `transcribe_session_free` releases both.
- `struct transcribe_model_load_params { uint64_t struct_size; transcribe_backend_request backend; transcribe_device_t device; }`
  → 8 + 4 (+4 pad) + 8 = **24 bytes** (matches `transcribe_abi_struct_size(TRANSCRIBE_ABI_MODEL_LOAD_PARAMS)`).
- `struct transcribe_session_params { uint64_t struct_size; int n_threads; transcribe_kv_type kv_type; int32_t n_ctx; }` → **24 bytes**.
- `transcribe_run(session, pcm, n_samples, params)` — `params` may be `NULL` for defaults; PCM must be
  mono float32 @ 16 kHz; the library does **not** resample.
- `transcribe_full_text(session)` → `const char *` owned by the session.
- `transcribe_version()` → `"0.4.0"`; `transcribe_status_string(status)` → human-readable status.
- `transcribe_abi_struct_size(which)` → authoritative size for ABI validation at startup.
- Backends: `TRANSCRIBE_BACKEND_AUTO=0, CPU=1, METAL=2, VULKAN=3, CPU_ACCEL=4, CUDA=5, ROCM=6`.

---

### Task 1: Repo skeleton, AGENTS.md, engine payload

**Files:**
- Create: `AGENTS.md`, `CMakeLists.txt`, `.gitignore` (already), `tools/setup_engine.sh`

- [ ] **Step 1: Write `AGENTS.md`** — project purpose, the offline rule, ABI contract, build/test commands, module boundaries, the Windows-first/Linux-portable rule, and the commit convention.
- [ ] **Step 2: Write `tools/setup_engine.sh`** — copy `transcribe.dll`, `ggml*.dll`, `msvcp140*.dll`, `vcruntime140*.dll` from `W:/TOOLS/portable app/Handy` into `engine/`. Must **not** copy `onnxruntime.dll` (not needed: Silero VAD runs on ggml).
- [ ] **Step 3: Run it** — `bash tools/setup_engine.sh` → expect `engine/` with 13 DLLs; `ls engine/ | wc -l` prints 13.
- [ ] **Step 4: Write `CMakeLists.txt`** — C++17, `project(ParakeetType CXX)`, one static lib `ptcore` (core+engine sources, no OS deps) and one exe `ParakeetTypeWorker` linking `ptcore`. Static CRT + libgcc/libstdc++ so the exe is standalone.
- [ ] **Step 5: Commit**

```bash
git add -A && git commit -m "chore: repo skeleton, AGENTS.md, engine payload script"
```

### Task 2: Portable core — WAV decoding and text rules

**Files:**
- Create: `src/core/wav.h`, `src/core/wav.cpp`, `src/core/text.h`, `src/core/text.cpp`, `src/core/log.h`, `src/core/log.cpp`

- [ ] **Step 1: `src/core/wav.h`** — `struct Audio { std::vector<float> samples; int sample_rate = 0; };`
  `bool load_wav(const std::string& path, Audio& out, std::string& err);`
  Support: 16-bit PCM and 32-bit float, 1–2 channels, any rate → downmix to mono, linear resample to 16 kHz.
- [ ] **Step 2: `src/core/wav.cpp`** — parse RIFF chunks (`fmt `, `data`), reject non-PCM/float or missing chunks with a clear error, downmix, resample.
- [ ] **Step 3: `src/core/text.h`/`.cpp`** —
  `std::string remove_filler_words(const std::string& text, const std::vector<std::string>& fillers);`
  Case-insensitive whole-word removal (also consumes a following `,` or `.`), whitespace tidy-up,
  `append_trailing_space`.
- [ ] **Step 4: `src/core/log.h`/`.cpp`** — `set_level`, `set_file`, `info/warn/error` macros writing to stderr and the log file.
- [ ] **Step 5: Write `src/worker/main.cpp` skeleton** that only parses `--selftest` and exercises wav+text on synthetic data (no engine yet). Build it:
  `cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build`
  → expect `build/ParakeetTypeWorker.exe`.
- [ ] **Step 6: Run `build/ParakeetTypeWorker.exe --selftest`** → expect `selftest: OK`.
- [ ] **Step 7: Commit**

```bash
git add -A && git commit -m "feat(core): wav decoding, text rules, logger, selftest harness"
```

### Task 3: Engine binding (the C ABI)

**Files:**
- Create: `src/engine/transcribe_api.h`, `src/engine/transcribe_api.cpp`

- [ ] **Step 1: `transcribe_api.h`** — mirror the header's structs with `static_assert` on sizes
  (24 / 24), declare the `Status` enum values, the `Engine` class:
  `bool load(const std::string& dll_path, std::string& err);`
  `bool open_session(const std::string& model_path, int backend, std::string& err);`
  `bool run(const float* pcm, int n_samples, std::string& err);`
  `std::string full_text() const;`
  `void close_session();`
  `std::string version() const;`
  `size_t abi_struct_size(int which) const;`
- [ ] **Step 2: `transcribe_api.cpp`** — `LoadLibraryA` + `GetProcAddress` for exactly the functions
  used; **fail loudly if `transcribe_version() != "0.4.0"`**; **fail loudly if
  `transcribe_abi_struct_size(0) != sizeof(ModelLoadParams)` or `(1) != sizeof(SessionParams)`**.
  `open_session` fills `model_load_params` with `backend` and `device = nullptr`, calls
  `transcribe_open`.
- [ ] **Step 3: Extend `--selftest`** to also load `engine/transcribe.dll` and assert
  version == "0.4.0" and both struct sizes match → expect `selftest: OK (engine 0.4.0)`.
- [ ] **Step 4: Build and run** → expect `selftest: OK (engine 0.4.0)`.
- [ ] **Step 5: Commit**

```bash
git add -A && git commit -m "feat(engine): transcribe.dll C ABI binding with version/struct validation"
```

### Task 4: Transcribe a WAV from the CLI

**Files:**
- Modify: `src/worker/main.cpp`

- [ ] **Step 1: Add CLI** — `--model <gguf> --file <wav> [--backend cpu|vulkan] [--no-filler] [--json] [--print-raw]`.
  Defaults: backend `cpu`, filler removal **on** with the locked list
  (`uh uhm umm uhh uhhh ehh ehm ahm hmm hm mmm um ah eh`).
- [ ] **Step 2: Implement the flow** — load WAV → `open_session` → `run` → `full_text` →
  filler removal → whitespace tidy → trailing space → print (plain or JSON with
  `raw`, `text`, `load_ms`, `run_ms`, `audio_seconds`).
- [ ] **Step 3: Run it on a golden WAV** —
  `build/ParakeetTypeWorker.exe --model "<gguf>" --file "C:/Users/SADI/AppData/Roaming/com.pais.handy/recordings/handy-1794595283.wav"`
  → expect `Also if possible make the controller smaller` (Handy's own transcript).
- [ ] **Step 4: Commit**

```bash
git add -A && git commit -m "feat(worker): transcribe a WAV file offline from the CLI"
```

### Task 5: Golden-corpus regression check

**Files:**
- Create: `tools/corpus_check.py`

- [ ] **Step 1: Write `tools/corpus_check.py`** — read `%APPDATA%/com.pais.handy/history.db`
  (`transcription_history.file_name`, `.transcription_text`), run the worker on each matching WAV in
  `recordings/`, compute word error rate (Levenshtein over words) and print a per-file table
  (expected vs actual, WER %) plus a corpus WER; exit non-zero if corpus WER > 5 %.
- [ ] **Step 2: Run it** → expect every file transcribed and corpus WER ≤ 5 %.
- [ ] **Step 3: Commit**

```bash
git add -A && git commit -m "test: golden-corpus regression against Handy's history.db"
```

### Task 6: Bug hunt and hardening

**Files:**
- Modify: `src/worker/main.cpp`, `src/core/wav.cpp`, `src/core/text.cpp` as needed

- [ ] **Step 1: Edge cases** — empty WAV, 1-sample WAV, 8 kHz WAV (resample), stereo WAV, 32-bit
  float WAV, non-WAV file, missing file, missing model file, wrong `--model` path, zero-length
  audio, a WAV longer than the model's window (60 s) → each must produce a clear error and a
  non-zero exit code, never a crash.
- [ ] **Step 2: Guard the engine** — null `session`, null `full_text` pointer, `TRANSCRIBE_ERR_*`
  mapped to readable messages via `transcribe_status_string`.
- [ ] **Step 3: Memory** — run the worker on a 60 s file and check RSS stays bounded (no leak of the
  session); run it twice in one process to make sure `close_session` frees the model.
- [ ] **Step 4: Offline proof** — `python -c "import pefile; ..."` on `build/ParakeetTypeWorker.exe`:
  assert the import list contains no `ws2_32.dll`, `winhttp.dll`, `wininet.dll`, `urlmon.dll`.
- [ ] **Step 5: Re-run the corpus check** → still ≤ 5 % WER.
- [ ] **Step 6: Commit**

```bash
git add -A && git commit -m "fix: edge-case handling, engine guards, offline import audit"
```

### Task 7: Hand-off for user testing

- [ ] **Step 1: Write `README.md`** — what it is, how to run the worker on a WAV, where the model
  and engine live, and what Milestone 2/3 will add.
- [ ] **Step 2: Record measurements** in the README: model load time, run time for a 10 s clip,
  peak RSS.
- [ ] **Step 3: Commit** and hand the user the exact command to try.

```bash
git add -A && git commit -m "docs: README with usage and measurements"
```

---

## Out of scope for this plan

Hotkey, microphone capture, mic button, controller/worker split, live streaming, filler list
tuning, packaging. Those are Milestones 2–5 in
`docs/superpowers/specs/2026-11-14-parakeettype-offline-dictation-design.md`.
