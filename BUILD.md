# V2T — build & rebuild guide

Everything you need to build V2T from scratch on any Windows machine, and to rebuild it
after a change. Written for a human, not for an agent (the agent rules live in
[`AGENTS.md`](AGENTS.md)).

---

## 1. What V2T is

An offline voice-typing tool for Windows:

- **Right Ctrl + Right Alt** starts recording, press again to stop → the transcript is typed
  at your cursor.
- The model is **NVIDIA Parakeet Unified EN 0.6B** (GGUF, Q8) on your own disk. Nothing is
  uploaded; the binaries contain no networking code at all.
- It reuses the inference engine that ships with **Handy** (`transcribe.dll`, transcribe-cpp
  v0.4.0), so the words it produces are the same words Handy produces.
- Two processes: a tiny always-on **controller** (~7 MB) and a **worker** that holds the
  model (~1 GB) and exits 60 seconds after your last take.

Repository: <https://github.com/sakibmahmudsadi/V2T>

---

## 2. What you need

| Thing | Version | Where it comes from |
|---|---|---|
| C++ compiler | **g++ 14.2 (MinGW-w64, x86_64)** | already installed at `C:\mingw64\bin` |
| CMake | **≥ 3.20** (3.31.5 used) | `C:\mingw64\bin\cmake.exe` |
| Ninja | **≥ 1.10** (1.12.1 used) | `C:\mingw64\bin\ninja.exe` |
| Python | 3.9+ (only for the test tools) | already installed |
| The engine DLLs | `transcribe.dll` + `ggml*.dll` + MSVC runtime | copied out of the Handy install |
| The model | `parakeet-unified-en-0.6b-Q8_0.gguf` (731 MB) | your Hugging Face cache |

Nothing else — no Visual Studio, no Python packages, no network at build or run time.

---

## 3. Where the two external pieces come from

### 3.1 The engine (`engine/` folder)

V2T does **not** implement inference. It calls the C ABI of `transcribe.dll`, which ships with
Handy. `tools/setup_engine.sh` copies the payload out:

```bash
cd /path/to/V2T
bash tools/setup_engine.sh
```

It copies from `W:/TOOLS/portable app/Handy` by default (21 files: `transcribe.dll`,
`ggml*.dll`, `msvcp140*.dll`, `vcruntime140*.dll`). Point it elsewhere with:

```bash
HANDY_DIR=/path/to/any/folder/that/has/the/dlls bash tools/setup_engine.sh
```

> **If you no longer have Handy installed:** any V2T release folder contains the same
> payload — `dist/V2T/engine/`. Keep that folder (or a copy of it) and use it as `HANDY_DIR`.
> The `engine/` folder is git-ignored, so it is *not* in the GitHub repo.

### 3.2 The model

The GGUF lives in your Hugging Face cache:

```
C:/Users/<you>/.cache/huggingface/hub/
  models--handy-computer--parakeet-unified-en-0.6b-gguf/
    snapshots/<hash>/parakeet-unified-en-0.6b-Q8_0.gguf
```

The path goes into `config.json` as `model_path`. Any copy of the GGUF works — point the
config at it.

---

## 4. Build it

```bash
cd /path/to/V2T

# 1. engine payload (once, or after the DLLs change)
bash tools/setup_engine.sh

# 2. configure + build
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build

# 3. check it
build/V2TWorker.exe --selftest
```

Expected output of the self-test:

```
selftest: core
  ok   16 kHz mono 16-bit WAV decodes to 1000 samples
  ...
selftest: engine
  ok   engine DLL loads
  ok   engine ABI version is 0.4.0
  ok   model load params struct size matches
  ok   session params struct size matches
selftest: OK
```

If `cmake` is not on your `PATH` (the MinGW tools are not, by default), put it there first:

```bash
export PATH="/c/mingw64/bin:$PATH"        # git-bash
# or, in cmd/PowerShell:
set PATH=C:\mingw64\bin;%PATH%
```

### 4.1 The portable release folder

```bash
bash tools/build_release.sh
```

Produces `dist/V2T/`:

```
dist/V2T/
  V2T.exe              the controller
  V2TWorker.exe        the model host
  engine/              transcribe.dll + ggml + MSVC runtime
  config.json          your settings (model path filled in)
  config.example.json  the same with placeholder paths
  README.md
```

Copy that folder anywhere and run `V2T.exe`. The model is **not** copied — it stays where it
is, referenced by `config.json`.

### 4.2 Build output

| File | Size | What it is |
|---|---|---|
| `build/V2T.exe` | ~3.0 MB | controller: hotkey, microphone, mic button, typing |
| `build/V2TWorker.exe` | ~0.9 MB | worker: model host (also a standalone CLI) |

Both are statically linked (no MSVC runtime needed on the target machine, apart from the
DLLs inside `engine/` that `transcribe.dll` itself needs).

---

## 5. Run it

```bash
dist/V2T/V2T.exe
```

Nothing appears on screen while it is idle — that is the design. The **tray icon** is the one
thing that is always there: grey while V2T waits for the hotkey, **green while it records**,
amber while the model decodes, red on an error, with the tooltip telling you which. Right-click it
for *Quit V2T*. Windows puts new tray icons in the overflow area — drag it onto the taskbar to
pin it where you can always see it.

Press **Right Ctrl + Right Alt**:

1. a **mic button** docks to the right edge of the screen (grey when quiet, **green while
   your voice is arriving**),
2. the model loads in the background while you speak (the first take takes ~1 s longer),
3. press the hotkey again (or click the mic button) → the text is typed at your cursor,
4. `Esc` cancels the take.

Right-click the mic button quits the app.

### Configuration — `config.json` next to the executable

| Key | Meaning | Default |
|---|---|---|
| `model_path` | absolute path to the Parakeet GGUF | your HF cache path |
| `engine_dir` | folder holding `transcribe.dll` | `engine/` next to the exe |
| `backend` | `cpu` or `vulkan` | `cpu` |
| `hotkey` | keys that toggle dictation | `["rctrl", "ralt"]` |
| `microphone` | substring of the capture device name (`""` = the system default) | `""` |
| `cancel_key` | key that cancels the take | `esc` |
| `model_unload_seconds` | when the worker releases the model | `60` |
| `max_buffer_seconds` | cap on the captured audio | `120` |
| `filler_removal` | strip `uh uhm umm uhh uhhh ehh ehm ahm hmm hm mmm um ah eh` | `true` |
| `append_trailing_space` | add a space after the typed text | `true` |
| `auto_submit` | send Enter after the text | `false` |
| `mic_button_edge` | `right` or `left` | `right` |

`V2T.exe --config <path>` uses a different configuration file.

---

## 6. The worker as a command line tool

Handy for checking the model and the engine without the UI:

```bash
MODEL="C:/Users/<you>/.cache/huggingface/hub/models--handy-computer--parakeet-unified-en-0.6b-gguf/snapshots/<hash>/parakeet-unified-en-0.6b-Q8_0.gguf"

build/V2TWorker.exe --model "$MODEL" --file recording.wav            # plain text
build/V2TWorker.exe --model "$MODEL" --file recording.wav --json     # machine readable
build/V2TWorker.exe --model "$MODEL" --file recording.wav --batch    # one-shot decode
build/V2TWorker.exe --model "$MODEL" --file recording.wav --repeat 3 # three takes, one model
build/V2TWorker.exe --selftest                                       # built-in checks
build/V2T.exe --render-icon mic.bmp                                  # write the mic button
```

---

## 7. Tests

```bash
bash tools/run_tests.sh
```

Builds and runs everything:

| Suite | What it proves |
|---|---|
| `V2TWorker.exe --selftest` | WAV decoding, resampling, filler rules, engine ABI version and struct sizes |
| `tools/corpus_check.py` | the transcripts match Handy's own, word for word (golden corpus) |
| `tools/edge_cases.py` | 13 malformed inputs fail cleanly instead of crashing |
| `tools/ipc_test.py` | the controller ↔ worker protocol works end to end with real audio |
| `tools/silence_test.py` | the live stream never hangs on silence |
| `tools/controller_test.py` | the hotkey shows and hides the mic button, and the model is loaded only once |
| `tools/idle_unload_test.py` | the worker exits when the model goes idle |
| `tools/icon_test.py` | the mic button renders a real glyph |
| `tools/audit.py` | neither binary links a networking DLL |

Useful extras:

```bash
python tools/frame_sweep.py     # how the stream frame size affects the accuracy
python tools/corpus_check.py    # per-recording word error rate
python tools/mic_state.py       # is the microphone muted / at zero volume?
python tools/mic_state.py --unmute   # unmute it
python tools/mic_probe.py       # the actual signal level the microphone delivers
```

### The microphone

`config.json` has a `microphone` key: a substring of the capture device name, or `""` for the
system default.

```json
"microphone": ""
```

The available devices are listed in the log at startup. If the microphone stops working — a
muted input, an unplugged headset, a driver that hands out silence — V2T says so in the log and
on the tray icon instead of quietly transcribing nothing.

The golden corpus is the recordings in `%APPDATA%/com.pais.handy/recordings` with the
transcripts Handy stored in `%APPDATA%/com.pais.handy/history.db`. If those are gone, the
corpus check reports "no comparable transcripts" and skips.

---

## 8. How the code is laid out

```
src/core/            portable logic, no OS headers
  wav.*              WAV file -> mono float32 @ 16 kHz
  text.*             filler-word removal, whitespace, trailing space
  json.*             a small JSON reader/writer (config + control messages)
  config.*           config.json
  ipc.*              the framed pipe protocol
  log.*              leveled logger
src/engine/          the transcribe.dll C ABI binding — the only file that knows the engine exists
src/platform/win32/  hotkey · audio (waveIn) · typing (SendInput) · mic button · tray icon
                    · the shared microphone glyph · worker client
src/controller/      the always-on process
src/worker/          the model host: CLI mode and server mode
engine/              runtime DLLs (git-ignored)
tools/               build + test scripts
docs/                the design spec and the implementation plans
```

Read `docs/superpowers/specs/2026-11-14-v2t-offline-dictation-design.md` for the design and
the reasoning behind it, and `docs/superpowers/plans/` for the step-by-step plans.

### The flow of one dictation

```
hotkey press
   ├─ microphone starts (waveIn, 16 kHz mono) -> growing buffer
   ├─ mic button appears on the right edge
   └─ worker: load model (first take only) / begin stream
        └─ buffered audio is replayed into the stream, then it tracks you live
hotkey again
   └─ finalize -> filler words removed -> SendInput types the text at the cursor
        └─ 60 s later the worker exits and the model memory goes back to the OS
```

---

## 9. Rebuilding after a change

```bash
cd /path/to/V2T
export PATH="/c/mingw64/bin:$PATH"      # if cmake is not on PATH
cmake --build build                      # incremental, seconds
bash tools/run_tests.sh                  # make sure nothing broke
bash tools/build_release.sh              # refresh dist/V2T/
git add -A && git commit -m "..." && git push
```

Only the changed `.cpp` files are recompiled; `build/` and `dist/` are git-ignored.

A **clean** rebuild (when something is deeply weird):

```bash
rm -rf build && bash tools/build_release.sh
```

---

## 10. Troubleshooting

| Symptom | Cause | Fix |
|---|---|---|
| `cannot load engine DLL ... (Windows error 126)` | `engine/` is missing or incomplete | `bash tools/setup_engine.sh` |
| `parakeet: failed to initialize CPU backend` | the engine was not asked to register backends | should not happen; check you rebuilt after the fix in `src/engine/transcribe_api.cpp` |
| `no model_path in config.json` | the config is missing or empty | copy `config.example.json` to `config.json` and set `model_path` |
| The mic button never appears | the hotkey is taken by another program | change `hotkey` in `config.json` (e.g. `["ralt","rshift"]`) |
| Nothing is transcribed, "no sound from the microphone" | the input device is muted, muted in hardware, or the wrong device | `python tools/mic_state.py` shows the state, `python tools/mic_state.py --unmute` unmutes it, `python tools/mic_probe.py` shows the level. Set `microphone` in `config.json` to pick the device (e.g. `"Realtek"`) |
| The button stays grey while you speak | the mic is muted or the wrong device | Windows → Settings → System → Sound → Input; check the level meter in the log |
| Nothing is typed | the target window is elevated | `SendInput` cannot reach admin windows — use a normal window |
| The app never starts | another instance is running | right-click the tray icon (or the mic button) to quit, or end `V2T.exe` in Task Manager |
| The tray icon is hidden | Windows puts new icons in the overflow area | drag it from the `^` arrow onto the taskbar to pin it |
| The model reloads on every take | the worker is being restarted | check the log for `reusing the worker`; it should appear from the second take on |
| `the model was idle; the worker exited` during a take | fixed — the timer must not fire while recording | rebuild and check the log |

The log is the fastest way to see what happened: `logs/v2t.log` next to the executable.

---

## 11. Git

```bash
git clone https://github.com/sakibmahmudsadi/V2T.git
cd V2T
git status
git log --oneline
```

Branch `main`. Commit style: `feat(scope): …`, `fix(scope): …`, `chore(scope): …`,
`test(scope): …`, `docs(scope): …`.

---

## 12. Everything in one block

```bash
# from a fresh clone
git clone https://github.com/sakibmahmudsadi/V2T.git
cd V2T
export PATH="/c/mingw64/bin:$PATH"
bash tools/setup_engine.sh                       # needs the Handy install or a dist/V2T/engine copy
cp config.example.json config.json              # then edit model_path
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
bash tools/run_tests.sh
bash tools/build_release.sh
dist/V2T/V2T.exe
```
