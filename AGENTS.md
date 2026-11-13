# AGENTS.md — working agreement for ParakeetType

ParakeetType is an **offline voice-typing tool for Windows**. It transcribes speech with the
user's existing local model — NVIDIA Parakeet Unified EN 0.6B (GGUF) — and types the result into
whatever window has focus. It is deliberately tiny, always-listening for one hotkey, and it
**never touches the network**.

Spec: `docs/superpowers/specs/2026-11-14-parakeettype-offline-dictation-design.md`
Plan: `docs/superpowers/plans/2026-11-14-milestone1-engine-binding.md`

---

## 1. Non-negotiable rules

1. **Never add network capability.** No sockets, no HTTP client, no updater, no telemetry, no
   "check for a newer model". The finished binaries must import no networking DLL — this is
   verified after each build (Task 6 of the plan) and is the product's core promise.
2. **Never re-implement inference.** The engine is `transcribe.dll` (transcribe-cpp v0.4.0),
   already on disk. Do not vendor, port, or "improve" the model code. Bind it and call it.
3. **Never change the model.** The user's GGUF (`parakeet-unified-en-0.6b-Q8_0.gguf`) is the
   model they chose and trust. Do not substitute, re-quantise, or download another one.
4. **Keep the controller small.** The always-on process must stay at a few MB. Anything that
   needs the model, or that allocates megabytes, belongs in the worker process.
5. **One visible thing.** The app shows nothing until the hotkey is pressed; then one mic button
   docks to the right edge of the screen. No tray icon, no overlay panel, no dialogs.
6. **Windows first, portable by construction.** Optimize for Windows, but keep platform calls
   behind the `src/platform/<os>/` interfaces so a Linux port is new backends, not a rewrite.

## 2. Layout

```
src/core/          portable logic — no OS headers, no platform assumptions
  wav.*            WAV file → mono float32 @ 16 kHz
  text.*           filler-word removal, whitespace tidy, trailing space
  log.*            leveled logger
src/engine/        the transcribe.dll C ABI (the only place that knows about the engine)
  transcribe_api.*
src/platform/win32/ Win32 backends (hotkey, audio, typing, mic button) — Milestone 2+
src/worker/        model host process (CLI in Milestone 1)
src/controller/    always-on process — Milestone 2+
engine/            runtime DLLs copied from the Handy install (git-ignored)
tools/             helper scripts (engine setup, corpus regression)
docs/              spec + plan
```

**Portability rule:** `src/core/` must compile with no `<windows.h>` anywhere. `src/worker/` is
already portable (it only calls the engine). Only `src/platform/win32/` may include Windows
headers.

## 3. The engine contract

- Engine DLL: `engine/transcribe.dll` (**transcribe-cpp v0.4.0**), plus `ggml*.dll`,
  `msvcp140*.dll`, `vcruntime140*.dll` in the same folder. `onnxruntime.dll` is deliberately
  absent — the Silero VAD runs on ggml.
- Bind via `LoadLibrary` + `GetProcAddress`. **No import lib, no link-time dependency.**
- **Validate at startup:** `transcribe_version()` must equal `"0.4.0"`, and
  `transcribe_abi_struct_size(TRANSCRIBE_ABI_MODEL_LOAD_PARAMS)` / `...(SESSION_PARAMS)` must equal
  `sizeof()` of our mirrored structs (24 and 24). Fail loudly and early — never continue against
  a mismatched ABI.
- The model is loaded with `mmap`; its ~731 MB of weights are file-backed. RSS shows them, but the
  pages are shared and reclaimable.
- PCM contract: **mono float32 @ 16 000 Hz**. The library does not resample — we do.

## 4. Build, test, run

```bash
# one-time: pull the engine DLLs out of the Handy install
bash tools/setup_engine.sh

# configure + build (MinGW-w64 g++ is at C:/mingw64/bin)
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build

# self-test (wav + text + ABI checks)
build/ParakeetTypeWorker.exe --selftest

# transcribe a file offline
build/ParakeetTypeWorker.exe \
  --model "C:/Users/SADI/.cache/huggingface/hub/models--handy-computer--parakeet-unified-en-0.6b-gguf/snapshots/7e948f21b7bdbac698d3318db9d350f1096f3b6c/parakeet-unified-en-0.6b-Q8_0.gguf" \
  --file "C:/Users/SADI/AppData/Roaming/com.pais.handy/recordings/handy-1794595283.wav"

# golden-corpus regression against Handy's own transcripts
python tools/corpus_check.py
```

`tools/corpus_check.py` compares our output with the transcripts Handy stored in
`%APPDATA%/com.pais.handy/history.db` for the same recordings. It must stay at **≤ 5 % corpus
word error rate**; a regression above that is a bug, not a rounding difference.

## 5. Code conventions

- C++17, no third-party libraries, no exceptions across the FFI boundary, no RTTI dependency.
- Every file has one job. If a file grows past ~400 lines, split it.
- Errors are reported as `bool ok` + `std::string& err` (never exceptions) in the core and the
  engine binding.
- Comments explain **why**, not what. Match the style of the files already in the tree.
- No `Sleep` polling loops where a condition variable or message loop would do — the controller
  must not burn a core while idle.

## 6. Commit convention

```
feat(scope): what changed
fix(scope): what was broken
chore(scope): housekeeping
test(scope): tests only
docs(scope): documentation only
```

Commit after every task in the plan. Never commit `engine/`, `build/`, or `logs/`.

## 7. Current state

- **Milestone 0 (done):** Handy's updater disabled (`HANDY_DISABLE_UPDATER=1`,
  `update_checks_enabled: false`, `Handy Offline.cmd`).
- **Milestone 1 (this plan):** engine binding + offline CLI transcription + golden corpus.
- **Milestone 2:** WASAPI capture, Right Ctrl+Right Alt hotkey, `SendInput` typing.
- **Milestone 3:** controller/worker split with a 60 s idle unload.
- **Milestone 4:** live streaming + the single mic button.
- **Milestone 5:** config file, autostart, packaging, README.

## 8. Before calling any work done

Show the command output that proves it: build output, `--selftest` output, corpus-check table, and
the import audit for the binaries. A claim without output is not evidence.
