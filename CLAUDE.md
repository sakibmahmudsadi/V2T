# CLAUDE.md — orientation for Claude Code

V2T is an **offline voice-typing app for Windows**. Right Ctrl + Right Alt starts and stops
dictation; the transcript is typed at the cursor with `SendInput`. It never touches the network.

Read `AGENTS.md` first — it is the working agreement (non-negotiable rules, engine contract,
code conventions) and §7a lists bugs that were real and fixed. Do not reintroduce them.

## Architecture in one paragraph

Two processes. `V2T.exe` (controller) is always-on and never holds the model: it owns the hotkey
hook (`WH_KEYBOARD_LL`, right/left aware), waveIn capture (16 kHz mono, four 250 ms buffers), the
mic button and tray icon, SendInput typing, and the IDLE→RECORDING→DECODING→TYPING state machine.
`V2TWorker.exe` holds the Parakeet model (~1.05 GB, mostly mmap-backed), is reused between takes,
and exits 60 s after the last take. They talk over a named pipe with length-prefixed frames:
kind 1 is JSON control, kind 2 is float32 audio. Audio crosses the pipe exactly once; the worker
keeps its own copy for the batch fallback. Live streaming decode is the primary path; a batch
decode is the fallback when the stream yields nothing.

## Build and verify

- MinGW g++ 14.2 lives at `C:/mingw64/bin` — `export PATH="/c/mingw64/bin:$PATH"` before cmake.
- `engine/` is git-ignored. After a fresh clone, `bash tools/setup_engine.sh` copies
  `transcribe.dll` and its dependencies out of the Handy install (`HANDY_DIR=` overrides the
  path). Nothing runs without it.
- Full verification, the single command: `bash tools/run_tests.sh` — nine suites, all must pass.
- Release build: `bash tools/build_release.sh` → `dist/V2T/`.

## Machine-local facts

- Model: `C:/Users/SADI/.cache/huggingface/hub/models--handy-computer--parakeet-unified-en-0.6b-gguf/snapshots/7e948f21b7bdbac698d3318db9d350f1096f3b6c/parakeet-unified-en-0.6b-Q8_0.gguf`.
  It is the default in `config.json` and in the tests.
- Golden corpus: the user's own Handy recordings in `%APPDATA%/com.pais.handy/recordings/` plus the
  transcripts Handy stored in `history.db`. Tests reference `handy-1794634700.wav` (earlier default
  files were deleted).
- PowerShell is not on PATH in the bash environment — use
  `/c/Windows/System32/WindowsPowerShell/v1.0/powershell.exe` when a script needs it.
- Git push works with the user's existing credentials. Branch is `main`; everything is pushed.

## The microphone lesson (2026-11-16)

The user reported "nothing is transcribing". The cause: the Windows input endpoint was **muted**
(device active, volume 70 %, mute = YES), so V2T recorded silence and transcribed nothing,
quietly. Handy was ruled out (it was stopped and silence persisted), privacy settings were ruled
out, and the endpoint was unmuted with `tools/mic_state.py --unmute`.

What the app does since commit `5b6479e`: lists the capture devices in the log at startup, turns
the tray red with "no sound from the microphone" after 4 s of silence, and accepts a
`microphone` substring in `config.json` to pick a device (a disconnected Bluetooth headset is
often also in the list).

Handy was stopped during the diagnosis; it can be relaunched with `Handy Offline.cmd` and both
apps coexist.

When someone says "it's not transcribing": read `logs/v2t.log` first, then run
`python tools/mic_state.py` (mute state; `--unmute` fixes) and `python tools/mic_probe.py`
(the actual signal level).

## Scope and standing offers

English only. Filler removal for uh / uhm / umm. No LLM post-processing, no speaker separation,
no translation, no chunked long-form.

The user has been offered, but has not requested: Silero VAD in the live stream (would close the
last ~1.5 % corpus WER vs Handy; the VAD model is already on disk in Handy's install), autostart
at login, and an installer. Do not build any of them unprompted.
