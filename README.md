# ParakeetType

Offline voice typing for Windows, driven by the Parakeet Unified EN 0.6B model you already have.

Press **Right Ctrl + Right Alt**, speak, press again — the text appears at your cursor. No network,
no updater, no model downloads. The transcription runs on your own machine, through the same
inference engine Handy uses, so the words come out exactly the same.

**Status: Milestone 1 complete** — the inference path works, fully offline, and matches Handy's
own output word for word on a golden corpus. The hotkey, the mic button and the always-on
controller come in Milestones 2–4.

---

## What works today

| Capability | Evidence |
|---|---|
| Offline transcription of a WAV through `transcribe.dll` | `build/ParakeetTypeWorker.exe --model … --file …` |
| Same words as Handy | `python tools/corpus_check.py` → **0.00 % word error rate** over 5 recordings |
| No network capability at all | `python tools/audit.py` → the binary imports only `kernel32.dll` + UCRT |
| Malformed input fails cleanly | `python tools/edge_cases.py` → 13/13 handled, no crashes |
| Engine ABI is validated before use | `--selftest` → version `0.4.0`, struct sizes verified |

---

## Build

Requirements: CMake ≥ 3.20, Ninja, and a C++17 compiler. The machine this was developed on uses
the MinGW-w64 toolchain in `C:\mingw64\bin` (g++ 14.2); MSVC works too.

```bash
bash tools/setup_engine.sh                 # copy the engine DLLs into engine/
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
build/ParakeetTypeWorker.exe --selftest
```

`tools/setup_engine.sh` copies `transcribe.dll`, `ggml*.dll` and the MSVC runtime out of the
Handy install (`W:/TOOLS/portable app/Handy`, override with `HANDY_DIR=…`). `onnxruntime.dll` is
deliberately not copied: the ggml Silero VAD is the one we use.

## Use

```bash
MODEL="C:/Users/SADI/.cache/huggingface/hub/models--handy-computer--parakeet-unified-en-0.6b-gguf/snapshots/7e948f21b7bdbac698d3318db9d350f1096f3b6c/parakeet-unified-en-0.6b-Q8_0.gguf"

# transcribe a file
build/ParakeetTypeWorker.exe --model "$MODEL" --file recording.wav

# machine-readable
build/ParakeetTypeWorker.exe --model "$MODEL" --file recording.wav --json

# options
#   --backend cpu|vulkan|auto   compute backend (default cpu)
#   --batch                     one-shot batch decode instead of the live stream
#   --repeat N                  transcribe N times in one session
#   --no-filler                 keep filler words
#   --verbose                   show the engine's own diagnostics
```

## Measured on this machine

| Measurement | Value |
|---|---|
| Model load + backend init | ~1.0–1.4 s (mmap, so the weights are file-backed) |
| Decode of 8.3 s of speech (live stream) | ~0.5–2.3 s depending on frame overhead |
| Decode of 15.7 s of speech | ~5.9 s → about 2.7× faster than real time |
| Peak working set with the model loaded | ~1.09 GB (the 731 MB weights + ggml compute buffers) |
| Import table | `kernel32.dll` + UCRT only — **no networking DLL** |
| Binary size | ~660 KB (static CRT, no runtime dependencies) |

The model load time is why the app records while the model loads: the audio buffer only has to
cover those ~1–1.5 s.

## Layout

```
src/core/            portable logic — WAV decoding, text rules, logger (no OS headers)
src/engine/          the transcribe.dll C ABI binding
src/platform/win32/  Windows helpers
src/worker/          the model host (this milestone's CLI)
engine/              runtime DLLs (git-ignored)
tools/               setup, corpus check, edge cases, offline audit
docs/                the design spec and the implementation plan
AGENTS.md            the working agreement for agents on this repo
```

## How the transcription matches Handy exactly

Handy decodes this model with the **live streaming** path; a one-shot batch run produces slightly
different text (it truncates and drops punctuation). We use the same path — `transcribe_stream_begin`
→ `transcribe_stream_feed` in 30 ms frames → `transcribe_stream_finalize` — and fall back to a
batch run only when a stream yields nothing, which is what Handy does too.

## Next milestones

2. WASAPI capture, Right Ctrl+Right Alt hotkey, `SendInput` typing
3. Controller/worker split: the always-on process stays a few MB, the model unloads 60 s after a take
4. Live streaming in the app, and the single mic button on the right edge of the screen
5. Config file, autostart, packaging
