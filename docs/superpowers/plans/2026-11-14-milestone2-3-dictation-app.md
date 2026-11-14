# Milestones 2 + 3 — the dictation app (controller + worker)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Right Ctrl + Right Alt starts recording, the model decodes the speech as you talk, the
finished text is typed at the cursor, and the model unloads 60 s after the last take — all offline,
with the always-on process staying at a few MB.

**Architecture:** Two processes. The **controller** (`V2T.exe`) is the always-on part:
keyboard hook, mic capture, the one mic button, keystroke injection — no model, no big buffers.
The **worker** (`V2TWorker.exe`) holds the model and does the decoding. They talk over a
named pipe with length-prefixed frames. The controller starts recording *immediately* on the hotkey
and loads the model in the background; the audio that was captured while the model loaded is
replayed into the live stream as fast as the model consumes it, so nothing is lost and the user
never waits.

**Tech Stack:** C++17, Win32 (low-level keyboard hook, waveIn capture, SendInput), the existing
`transcribe.dll` binding, a hand-written minimal JSON parser (config + control messages).

---

## Design decisions (settled in the spec, confirmed by measurement)

| Question | Answer | Why |
|---|---|---|
| Chunk the audio into 5–10 s pieces? | **No** | Memory is audio-length-independent (1113 MB at 8 s vs 1116 MB at 33 s). Chunking costs accuracy and speed. |
| Which decode path? | **Live stream**, batch only as fallback | The stream runs 2.6–3.9× faster than real time, so the text is done the instant you stop talking. |
| One process or two? | **Two** | The controller stays ~5 MB; the 1.09 GB model lives in the worker, which exits 60 s after the last take. |
| Speaker separation? | **No** | Out of scope, agreed. |
| IPC | Named pipe, framed: `[u32 len][u32 kind][payload]`, kind 1 = JSON control, kind 2 = raw float32 PCM | No base64, no dependencies, one channel for both. |
| Audio capture | `waveIn` (winmm), 16 kHz mono 16-bit | The Windows mapper resamples and downmixes; no COM, no resampler. |
| Backend mask | The controller sets `TRANSCRIBE_BACKENDS` in the worker's environment | The engine reads it at DLL load; setting it later is too late (see AGENTS.md). |

---

## Tasks

### Task 1: JSON + config

- [x] `src/core/json.h/.cpp` — minimal JSON: parse to a `Value` (null/bool/number/string/array/object),
  and build objects. Enough for `config.json` and the control protocol.
- [x] `src/core/config.h/.cpp` — `Config` struct with defaults (model path, hotkey, backend,
  `model_unload_seconds: 60`, `max_buffer_seconds: 120`, filler list, trailing space, mic edge),
  loaded from `config.json` next to the executable; unknown keys ignored.
- [x] Test: `--selftest` parses a config with comments/odd spacing and round-trips a value.

### Task 2: IPC framing

- [x] `src/core/ipc.h/.cpp` — `write_frame(handle, kind, bytes)`, `read_frame(handle, kind, bytes)`,
  exact-length reads, no buffering surprises.
- [x] Test: a pipe round-trip in the selftest (create a pipe pair, write a frame, read it back).

### Task 3: Platform backends

- [x] `src/platform/win32/hotkey.h/.cpp` — `WH_KEYBOARD_LL` hook that reports Right Ctrl + Right Alt
  press/release (left/right aware) and Esc.
- [x] `src/platform/win32/audio.h/.cpp` — waveIn capture at 16 kHz mono 16-bit; converts to float32
  and pushes into a sink callback; start/stop.
- [x] `src/platform/win32/typing.h/.cpp` — SendInput with `KEYEVENTF_UNICODE`, chunked with a small
  delay so long text does not overflow the input queue.
- [x] `src/platform/win32/mic_button.h/.cpp` — a topmost, click-through-except-the-button popup
  window docked to the right edge, with a mic drawn at runtime (no image assets); click = stop,
  right-click = quit; idle/needs-attention states.
- [x] `src/platform/win32/worker_client.h/.cpp` — spawns the worker with `TRANSCRIBE_BACKENDS` in its
  environment, connects the named pipe, sends/receives frames, and owns the 60 s idle timer.

### Task 4: Worker as a pipe server

- [x] Extend `src/worker/main.cpp` — keep the CLI mode; add `--serve` mode: connect to a pipe
  (name given by `--pipe`), load the model, then loop over frames: JSON commands (`load`, `begin`,
  `finalize`, `shutdown`) and PCM frames (feed the live stream). Replies are JSON.
- [x] The worker replies `{"ok":true,"load_ms":…}` after the model is ready, and
  `{"ok":true,"text":"…"}` after `finalize`.

### Task 5: The controller

- [x] `src/controller/main.cpp` — the state machine:
  `IDLE → RECORDING → (model loading ∥ capturing) → DECODING → TYPING → IDLE`,
  with the catch-up replay of buffered audio once the model is ready.
- [x] The audio buffer grows on demand (64 KB/s) and is capped by `max_buffer_seconds`.
- [x] After typing, the idle timer starts; when it fires the worker is told to exit and the model
  memory goes back to the OS.

### Task 6: Packaging + tests

- [x] `tools/build_release.sh` — copy the two exes and the engine folder into `dist/`.
- [x] `python tools/audit.py` extended to audit both binaries.
- [x] Manual test script for the user: press the hotkey, speak, press again, check the text.

---

## Out of scope

Speaker separation · chunked long-form · live translation · LLM post-processing · custom word lists ·
language detection · tray icon · overlay panel · model catalogue · updater.
