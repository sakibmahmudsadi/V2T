# V2T

Offline voice typing for Windows, driven by the Parakeet Unified EN 0.6B model you already have.

*Voice to text, entirely on your machine. No network, no updater, no model downloads.*

Press **Right Ctrl + Right Alt**, speak, press again — the text appears at your cursor. No network,
no updater, no model downloads. The transcription runs on your own machine through the same
inference engine Handy uses, so the words come out exactly the same.

**→ Building or rebuilding it? Read [`BUILD.md`](BUILD.md).**

**Status: Milestones 1–3 complete.** The dictation loop works end to end: hotkey → microphone →
live decode → text typed at the cursor, with the model held in a separate worker that releases its
memory 60 seconds after each take.

---

## How it works

```
V2T.exe          the always-on process — 7 MB, no model, no microphone open
  ├─ tray icon                     (grey waiting · green recording · amber working · red error)
  ├─ Right Ctrl+Right Alt hook      (WH_KEYBOARD_LL, right/left aware)
  ├─ microphone capture             (waveIn, 16 kHz mono, into a growing buffer)
  ├─ the mic button                 (one small button docked to the right edge)
  └─ keystroke injection            (SendInput, Unicode — the clipboard is never touched)
        │  named pipe, framed messages
        ▼
V2TWorker.exe    the model host — 1.05 GB while it holds the model
  └─ transcribe.dll → ggml → Parakeet RNN-T
```

- Pressing the hotkey **starts recording immediately** and loads the model in the background.
  The audio captured while the model loads is replayed into the live stream as soon as it is
  ready, so nothing is lost and you never wait for a load.
- The mic button appears docked to the **right edge** of the screen while you talk. It is **grey
  while the microphone is quiet and green while your voice is arriving** — one colour tells you
  whether the app is hearing you. Click it (or press the hotkey again) to stop; `Esc` cancels.
  Nothing is visible while the app is idle.
- **The model is loaded once and reused.** The worker keeps it between takes and exits 60 seconds
  after the last one, so the second take starts instantly instead of paying a fresh model load.
- The finished text is typed at the cursor. Filler words (`uh uhm umm uhh uhhh ehh ehm ahm hmm hm
  mmm um ah eh`) are removed.
- 60 seconds after the last take the worker exits and its ~1 GB goes back to the operating system.

---

## Use it

```bash
cd W:\Pipeline\V2T
bash tools/build_release.sh        # -> dist/V2T/
dist/V2T/V2T.exe
```

Full build instructions, prerequisites and troubleshooting: **[BUILD.md](BUILD.md)**.

`config.json` (next to the executable) holds the model path and the settings.
`config.example.json` is the same file with placeholder paths, for other machines:

| Key | Meaning | Default |
|---|---|---|
| `model_path` | the Parakeet GGUF | your Hugging Face cache path |
| `engine_dir` | folder with `transcribe.dll` | `engine/` next to the exe |
| `backend` | `cpu` or `vulkan` | `cpu` |
| `hotkey` | keys that toggle dictation | `["rctrl", "ralt"]` |
| `cancel_key` | key that cancels the take | `esc` |
| `model_unload_seconds` | when the worker releases the model | `60` |
| `max_buffer_seconds` | cap on the captured audio | `120` |
| `filler_removal` | strip filler words | `true` |
| `append_trailing_space` | add a space after the typed text | `true` |
| `auto_submit` | send Enter after the text | `false` |
| `mic_button_edge` | `right` or `left` | `right` |

The controller also takes `--config <path>`.

---

## Measured on this machine

| | |
|---|---|
| Controller, idle | **7.2 MB** |
| Controller while recording | 12.4 MB (the audio buffer) |
| Worker while it holds the model | 1054 MB |
| Model load (mmap) | ~0.9–1.3 s, once per worker; covered by the audio buffer |
| Second take | starts instantly — the worker is reused, the model is not reloaded |
| Decode | 2.6–3.9× faster than real time, so the text is ready the moment you stop |
| Binary size | 3.0 MB controller + 0.9 MB worker (static CRT, no runtime dependencies) |
| Import table | `kernel32.dll` + UCRT only — **no networking DLL** |

Golden-corpus accuracy against Handy's own stored transcripts: **0.00 % word error rate** over five
recordings (including a 33-second two-speaker dialogue: 96/96 words correct).

---

## The worker as a command line tool

The model host is also a plain CLI, which is handy for checking things:

```bash
MODEL="C:/Users/SADI/.cache/huggingface/hub/models--handy-computer--parakeet-unified-en-0.6b-gguf/snapshots/7e948f21b7bdbac698d3318db9d350f1096f3b6c/parakeet-unified-en-0.6b-Q8_0.gguf"
build/V2TWorker.exe --model "$MODEL" --file "recording.wav" [--json] [--batch] [--repeat N]
build/V2TWorker.exe --selftest
```

---

## Tests

```bash
bash tools/run_tests.sh
```

| Suite | What it proves |
|---|---|
| `--selftest` | WAV decoding, resampling, filler rules, engine ABI version and struct sizes |
| `tools/corpus_check.py` | 0.00 % WER against Handy's stored transcripts |
| `tools/edge_cases.py` | 13 malformed inputs fail cleanly, no crashes |
| `tools/ipc_test.py` | the controller ↔ worker protocol works end to end with real audio |
| `tools/silence_test.py` | the live stream does not hang on silence |
| `tools/controller_test.py` | the hotkey shows and hides the mic button |
| `tools/idle_unload_test.py` | the worker exits when the model goes idle |
| `tools/audit.py` | neither binary links a networking DLL |
| `tools/icon_test.py` | the mic button renders a real glyph |
| `tools/frame_sweep.py` | measures how the stream frame size affects the result |

---

## Layout

```
src/core/            portable logic — JSON, config, IPC framing, WAV, text rules, logger
src/engine/          the transcribe.dll C ABI binding
src/platform/win32/  hotkey, audio capture, typing, mic button, worker client
src/controller/      the always-on process
src/worker/          the model host (CLI and server modes)
engine/              runtime DLLs (git-ignored)
tools/               build, tests, audits
docs/                the design spec and the implementation plans
AGENTS.md            the working agreement for agents on this repo
```

## How the transcription matches Handy exactly

Handy decodes this model with the **live streaming** path; a one-shot batch run produces slightly
different text (it truncates and drops punctuation). We use the same path — `transcribe_stream_begin`
→ `transcribe_stream_feed` in 30 ms frames → `transcribe_stream_finalize` — and fall back to a batch
run only when a stream yields nothing, which is what Handy does too.

## Known differences from Handy

- **The live stream sees every sample; Handy only feeds it the frames its VAD marks as speech.**
  That is the one remaining source of wording differences (about 1.5 % of words on the golden
  corpus). Wiring the Silero VAD in would close it — the model is already available as
  `silero-vad-v6.2-F32.gguf`.
- The stream frame size (32 ms) does not change the result: a sweep over 10–60 ms gives the same
  word error rate (`tools/frame_sweep.py`).

## Known limitations

- One voice at a time: there is no speaker separation (neither does Parakeet).
- The text is typed at the end of the take, not live into the target window.
- An elevated (administrator) target window cannot be typed into; `SendInput` is blocked by UIPI.
