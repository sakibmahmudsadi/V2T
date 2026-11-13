# ParakeetType — offline voice typing on Parakeet Unified EN 0.6B

**Date:** 2026-11-14
**Status:** Design approved in conversation; pending written-spec review
**Supersedes:** Handy (cjpais/Handy) as the dictation tool — kept installed as a fallback for now

---

## 1. Goal

A permanently-listening, **fully offline** dictation tool for Windows that:

- uses the **exact model the user already has and trusts** —
  `parakeet-unified-en-0.6b-Q8_0.gguf` (731 MB, NVIDIA Parakeet Unified EN 0.6B, RNN‑T head);
- produces **identical transcription behaviour** to Handy, because it drives the *same*
  `transcribe.dll` (transcribe-cpp v0.4.0) through its published C ABI;
- **never opens a network socket** — not for update checks, not for model downloads, not for
  post-processing;
- is **cheap**: an idle footprint of ~8 MB, the model only resident while it is needed, and the
  user never waits for a model load to start talking.

Explicitly **not** a goal: replacing Handy's full feature surface. Only the features below.

---

## 2. Why not just keep using Handy?

Handy works; the problem is that it is a Tauri webview app that (a) phones home for update checks
and (b) carries a large feature surface (LLM post-processing, model catalogue, update flow,
onboarding) that is dead weight for a single local model.

Evidence gathered during design (2026-11-13/14):

| Finding | How it was established |
|---|---|
| `handy.exe` is a 32 MB Rust/Tauri PE that imports `ws2_32`, `secur32`, `crypt32` (Rustls HTTPS) | REA `inspect_pe_imports` on `handy.exe` |
| Inference is done by `transcribe.dll` = transcribe-cpp **v0.4.0**, exposing a ~130-function C ABI (`transcribe_version()` returns `0.4.0`) | `ctypes` probe of the shipped DLL; ABI header pulled from crates.io `transcribe-cpp-sys 0.4.0` |
| The model is GGUF v3, `general.architecture=parakeet`, `stt.parakeet.head_kind=rnnt`, **`stt.capability.streaming=true`**, `general.languages=["en"]` | REA `inspect_model_artifact` + `inspect_analysis_view` on the GGUF header |
| Handy itself uses a **separate worker process** for the model | `tasklist`: `handy.exe` 53 MB (UI) + `handy.exe` 868 MB (model) |
| Update checks can be force-disabled | Handy source `settings.rs::update_checks_forced_disabled()` honours `HANDY_DISABLE_UPDATER` |

**Phase 0 (done, 2026-11-13):** Handy's updater was disabled three ways so the nag and the
network chatter stop immediately while ParakeetType is built:

1. user env var `HANDY_DISABLE_UPDATER=1`;
2. `settings_store.json` → `update_checks_enabled: false`, `show_whats_new_on_update: false`
   (backup at `settings_store.json.bak`) — *may be rewritten by Handy if it is still running and
   later saves settings; the env var is authoritative regardless*;
3. `Handy\Handy Offline.cmd` launcher that sets the var and starts `handy.exe`.

---

## 3. Final feature list (locked)

### 3.1 Trigger

| Feature | Behaviour |
|---|---|
| Hotkey | **Right Ctrl + Right Alt** (low-level `WH_KEYBOARD_LL` hook; distinguishes left/right keys, which `RegisterHotKey` cannot) |
| Mode | **Tap to toggle** — press once to start, press again to stop and insert the text |
| Cancel | `Esc` while recording → discard the take, type nothing |
| Configurable | The key pair is read from `config.json`; the hook is generic, so it is not hard-coded |

### 3.2 Recording

| Feature | Behaviour |
|---|---|
| Capture | WASAPI, default input device, shared-mode, mono **16 kHz float32** (the model's native rate) |
| Starts | **Immediately** on hotkey press — before and during the model load |
| Buffer | **Dynamic, not a fixed length.** The buffer exists for exactly as long as the model needs to load into RAM — however long that is, however long the user keeps talking. It grows on demand (64 KB/s of float32) and is capped at `max_buffer_seconds` (default 120 s ≈ 7.7 MB). Nothing is lost while the model loads. |
| Endpointing | Manual (second hotkey press, or clicking the mic button) only. **VAD auto-stop is off by default** so a slow speaker is never cut off; it is a one-line config flip (`vad_autostop`) if wanted later |

### 3.3 Model

| Feature | Behaviour |
|---|---|
| Model | `parakeet-unified-en-0.6b-Q8_0.gguf`, path in `config.json`, default = the existing Hugging Face cache path |
| Load | **In the background, in a separate worker process**, started the moment the hotkey is pressed |
| Residency | **Worker exits 60 s after the last transcription** → model RAM is returned to the OS |
| Backend | **CPU** by default (a 0.6B model decodes ~10 s of speech in ≈1 s on CPU); `backend: cpu \| vulkan` in config for GPU offload. GPU is off by default: it costs 1–3 s of GPU init and carries driver risk (Handy force-disables GPU on AMD hosts) |
| Residency re-warm | If the worker is still alive inside the 60 s window, the next dictation starts instantly |

### 3.4 Transcription

| Feature | Behaviour |
|---|---|
| Path | **Live streaming RNN‑T decode.** Buffered audio is replayed into the stream as fast as the model consumes it (catch-up), then the stream continues 1:1 with live mic input |
| Fallback | If the stream yields no text, re-run a one-shot batch transcription over the whole buffer (same behaviour Handy uses) |
| Filler-word removal | **Always on.** Removes: `uh, uhm, umm, uhh, uhhh, ehh, ehm, ahm, hmm, hm, mmm, um, ah, eh` (Handy's universal list + its English list, applied unconditionally — no language gate, this model is English-only) |
| Custom word list | **Not included** |
| LLM post-processing | **Not included** — it is Handy's only cloud feature, and the app must never touch the network |
| Live translation | **Not included** — the model is English-only; no second model is loaded |

### 3.5 Output

| Feature | Behaviour |
|---|---|
| Insertion | The finished text is typed into the **currently focused window** at the cursor via `SendInput` with `KEYEVENTF_UNICODE` (no clipboard touched) |
| Timing | Typed **only after** transcription completes — never streamed character-by-character into the target app |
| Trailing space | Appended by default (`append_trailing_space: true`) |
| Auto-submit | `Enter` after the text — **off** by default |

### 3.6 UI — one mic button, nothing else

| Feature | Behaviour |
|---|---|
| Nothing shown | The app shows **no UI at all** while idle: no tray icon, no panel, no overlay, no window. It is invisible until you press the hotkey |
| Mic button | On dictation start, a small **mic button appears docked to the right edge of the screen** (vertically centred, always-on-top, click-through everywhere except the button itself) |
| Stop | **Clicking the mic button stops the recording** (same as pressing the hotkey again). The button is the only control the user needs |
| Button states | Idle grey mic → recording (red/accent, gently pulsing) → working (busy badge while the model decodes) → back to invisible |
| Right-click | Right-click the mic button → quit the app. (This is the only exit path; there is no tray icon.) |
| No text overlay | The transcript is **not** displayed on screen. It goes straight into the focused window when it is finished. (Config flag `show_transcript_button_tip` may put the final text in the button's tooltip.) |
| Errors | A failure turns the mic button amber and puts the reason in its tooltip. No dialogs, no popups, no sound |

### 3.7 Offline guarantee

| Guarantee | How it is enforced and verified |
|---|---|
| The app makes no network calls | The binary is built with no networking imports at all; verified after the build with REA `inspect_pe_imports` (expect no `ws2_32`, `winhttp`, `wininet`, `urlmon`) |
| No update mechanism exists | There is no updater code to disable — nothing to phone home to |
| Model is local | Loaded from a local path; the app has no download logic |
| Optional hard block | Windows Firewall outbound-block rules can be added for `ParakeetType.exe` and `handy.exe` (needs Administrator; offered as a separate, clearly-labelled step) |

---

## 4. Architecture

### 4.1 Processes

Two processes, mirroring Handy's proven split:

```
  ParakeetType.exe  (controller, always on, ~4-8 MB)
    ├─ hotkey thread      WH_KEYBOARD_LL hook, Right Ctrl + Right Alt
    ├─ mic button        created ONLY while dictating; docked to the right edge
    ├─ audio thread       WASAPI capture → dynamic ring buffer → chunk queue
    └─ worker supervisor  spawns / reaps the model worker
             │  (named pipe, JSON lines)
             ▼
  ParakeetTypeWorker.exe  (model host, exists only while needed)
    └─ transcribe.dll → ggml → Parakeet RNN‑T
```

**Why two processes:** the controller stays at a few MB and never holds model pages, so it stays
snappy and can survive a model crash (the worker is simply restarted). The worker holds ~870 MB
only while the model is loaded and exits 60 s after the last use, so idle RAM is genuinely idle.

**Why the controller is kept tiny:** it does the least possible work — listen for two keys, move
PCM bytes, show one button, tell the worker what to do. No webview, no tray icon, no icon
resources, no overlay window while idle, no threads that spin. Target: ≤ 8 MB resident, expected
4–6 MB.

**Single-process alternative** (one exe, model loaded in-process) was rejected: after a model
load the whole app's RSS is ~870 MB, and reloading the model would restart the hotkey listener.
The controller/worker split keeps the always-on part tiny, which is the whole point.

### 4.2 Timeline of one dictation

```
t=0.00  hotkey press
        ├─ audio capture starts  ──────────────────────────────► ring buffer
        │  (buffer grows as long as the model needs; NOT a fixed 5 s)
        ├─ mic button appears on the right edge of the screen
        └─ worker spawn + model load (background)              [mmap'd GGUF]
           the load is I/O + page-in on the CPU; its duration is
           exactly what the buffer is sized for

t=load  model ready
        └─ buffered audio replayed into the live stream as fast as the model consumes it
           (10 s of audio ≈ 1 s of compute)  → mic button shows the busy badge

t=0..N  user speaks; stream keeps up in real time

t=N     hotkey press again, or click the mic button (Esc cancels)
        └─ stream finalise → text → filler words removed → trailing space
           → SendInput into the focused window → button briefly shows the
             final text in its tooltip

t=N+60  worker exits, ~870 MB returned
```

### 4.3 Modules (each one job, independent interfaces)

| Module | Responsibility | Depends on |
|---|---|---|
| `config` | Read/validate `config.json`; defaults for everything | — |
| `abi` | Load `transcribe.dll` via `LoadLibrary`+`GetProcAddress`; declare the C ABI subset; validate `transcribe_version()` == 0.4.0 and cross-check every struct with `transcribe_abi_struct_size()` | `transcribe.dll` |
| `model_host` | Worker-process logic: model load, session, live stream, batch fallback, text extraction | `abi` |
| `pipe` | Controller↔worker protocol (JSON lines over a named pipe): `load`, `stream_begin`, `feed`, `finalize`, `cancel`, `shutdown` | — |
| `audio` | WASAPI capture → mono 16 kHz float32 → growing ring buffer + chunk queue | `mmdevapi`, `ole32` |
| `hotkey` | `WH_KEYBOARD_LL` hook; right/left-aware modifier combo; press/release events | `user32` |
| `type_text` | `SendInput` Unicode typing into the focused window; trailing space; optional auto-submit | `user32` |
| `mic_button` | The one visible thing: a small always-on-top, click-through window docked to the right edge, drawn from a runtime-generated icon; state colours; tooltip | `user32`, `gdi32` |
| `text` | Filler-word removal, trailing space, whitespace tidy-up — pure functions, no platform code | — |
| `main` | Wiring, state machine, idle timer, worker lifecycle | all of the above |

### 4.3.1 Portability layering (Windows first, Linux later)

The code is written **simple and modular**, with all platform-specific calls confined to a thin
backend layer, so a Linux port means writing new backends — not rewriting the app:

```
  core/     config · state machine · pipe protocol · text rules · timing
            (plain C++17, no OS headers, no platform assumptions)
  platform/ hotkey · audio · typing · mic_button   ← interface + win32 impl
  worker/   model host (links the engine; portable to Linux unchanged)
```

Each `platform/` backend is one interface with a Win32 implementation. Porting to Linux means
implementing the same four interfaces (evdev hotkey, PipeWire/PulseAudio capture, X11/Wayland
typing, a small GTK/Qt button) and changing nothing else. Rule for the port: **Windows
efficiency first** — a module that is slightly heavier on Windows but keeps the port clean is
acceptable; a module that bloats the Windows controller is not.

### 4.4 Controller state machine

```
IDLE ──hotkey──► RECORDING ──hotkey──► TRANSCRIBING ──done──► TYPING ──► IDLE
                    │  │                                      (worker idle timer starts)
                    │  └──Esc──► CANCELLED ──► IDLE
                    └── (model still loading) ──► audio keeps buffering
```

---

## 5. Configuration (`config.json`)

```jsonc
{
  "model_path": "C:/Users/SADI/.cache/huggingface/hub/models--handy-computer--parakeet-unified-en-0.6b-gguf/snapshots/7e948f21b7bdbac698d3318db9d350f1096f3b6c/parakeet-unified-en-0.6b-Q8_0.gguf",
  "engine_dir": "W:/Pipeline/V2T/engine",           // transcribe.dll + ggml*.dll
  "backend": "cpu",                 // "cpu" | "vulkan"
  "hotkey": ["rctrl", "ralt"],      // tap to toggle
  "cancel_key": "esc",
  "device": null,                   // null = system default input device
  "model_unload_seconds": 60,       // worker exits after this much inactivity
  "max_buffer_seconds": 120,        // hard cap on the audio buffer (≈7.7 MB)
  "vad_autostop": false,            // manual stop by default
  "filler_removal": true,
  "filler_words": ["uh","uhm","umm","uhh","uhhh","ehh","ehm","ahm","hmm","hm","mmm","um","ah","eh"],
  "append_trailing_space": true,
  "auto_submit": false,
  "mic_button_edge": "right",       // "right" | "left"
  "log_level": "info",
  "log_path": "logs/parakeettype.log"
}
```

Unknown keys are ignored; a missing file yields defaults; the model path is the only required
value (the app refuses to start without it and says so in the log).

---

## 6. Resource budget

| State | RAM | Notes |
|---|---|---|
| Idle (controller only) | **≤ 8 MB target, ~4–6 MB expected** | no mic open, no model, no worker, no visible UI |
| Recording, model loading | ~5 MB + growing buffer | buffer grows only as fast as you speak (64 KB/s) |
| Recording, model loaded | ~5 MB + ~870 MB | same weights Handy loads today |
| 60 s after last transcription | back to ~5 MB | worker exits, model pages released |
| Disk | engine DLLs ~100 MB (ggml-vulkan 43 MB optional) + model 731 MB (referenced, not copied) | |

CPU: the RNN‑T decode of 10 s of audio is ≈1 s of one core. No keep-alive spin — the controller
sleeps on a message loop and the worker is idle-blocked between requests.

---

## 7. Build & packaging

| Item | Decision |
|---|---|
| Language | **C++17**, Win32 API — no runtime, no framework, no webview. Built with the `clang`/`gcc`/`cmake`/`ninja` already installed in `C:\mingw64\bin` (MSVC toolchain is *not* installed, so MinGW is the build path) |
| Linking | `transcribe.dll` is loaded at runtime via `LoadLibrary`+`GetProcAddress` — no import lib needed, and the ABI version is validated at runtime |
| Artifacts | `ParakeetType.exe` (controller) + `ParakeetTypeWorker.exe` (worker), ~200–400 KB each |
| Engine payload | `transcribe.dll`, `ggml*.dll`, `msvcp140*.dll`, `vcruntime140*.dll` copied from the Handy folder into `engine/` so the tool is self-contained. `onnxruntime.dll` is **not** needed (Silero VAD v6.2 runs on ggml) |
| Model | Referenced from the existing Hugging Face cache path — not copied (731 MB) |
| Layout | Project root: `W:\Pipeline\V2T\` |
| Repository | `W:\Pipeline\V2T\` (git). `AGENTS.md` holds the working agreement for agents on this repo; the spec lives in `docs/superpowers/specs/` |
| Source layout | `src/core/` (portable logic) · `src/platform/win32/` (Windows backends) · `src/worker/` (model host) · `src/controller/` · `tools/` (scripts) · `engine/` (runtime DLLs, git-ignored) |
| Rust alternative | The same C ABI works from Rust (`transcribe-cpp` crate) if the C++ build stalls; not required |

---

## 8. Failure handling

| Failure | Behaviour |
|---|---|
| Model file missing / unreadable | Mic button turns amber with the reason in its tooltip; no crash; hotkey stays live |
| `transcribe.dll` missing or wrong ABI version | Hard, explicit error at startup ("engine DLL version mismatch") |
| Model load fails | Recording continues; on stop the mic button turns amber with the reason; no text typed |
| Worker crashes mid-take | Supervisor restarts it; the take is lost gracefully; button reports it |
| Stream returns empty text | Automatic one-shot batch transcription of the buffer (Handy's own fallback) |
| Audio device lost / no mic | Mic button turns amber; no crash |
| Target window elevated (admin) | `SendInput` cannot reach it — the button's tooltip says "cannot type into an elevated window" instead of silently failing |
| Text longer than expected | Typed in chunks with a small inter-chunk delay to avoid input queue overflow |
| Buffer cap reached | Recording stops cleanly at `max_buffer_seconds` and transcribes what was captured |

---

## 9. Verification plan

1. **Golden-corpus accuracy** — the 7 WAVs already in
   `%APPDATA%\com.pais.handy\recordings\` plus their transcripts in `history.db` form a
   regression corpus: ParakeetType must produce the same text as Handy for the same audio.
2. **Latency** — measured: hotkey → text inserted; hotkey → model ready.
3. **Load time** — measured worker-side model load (mmap expected ≈0.3–1.5 s). This number sets
   how long the dynamic buffer has to hold.
4. **Memory** — `tasklist` RSS in idle / recording / post-idle states vs. the budget in §6.
5. **Offline proof** — REA `inspect_pe_imports` on both built exes: assert no networking DLL.
6. **Hotkey correctness** — right Ctrl + Right Alt only (left Ctrl + Right Alt must not trigger).
7. **Cancel** — Esc discards the take and types nothing.
8. **UI contract** — nothing visible while idle; mic button appears docked right on press; a
   click on it stops; the button disappears afterwards.

---

## 10. Milestones

| # | Milestone | Outcome |
|---|---|---|
| 0 | Disable Handy's updater | **Done** (2026-11-13) |
| 0.5 | `AGENTS.md` + repo skeleton + build script | Working agreement for agents; `cmake`/`ninja` build of an empty exe; `engine/` populated from the Handy folder |
| 1 | `abi` + `model_host` + CLI mode (`--file in.wav`) | Transcribes a WAV offline via `transcribe.dll`, text matches Handy on the golden corpus; model-load time measured |
| 2 | `audio` (WASAPI) + `hotkey` + `type_text` | Full tap-to-toggle dictation with keystroke insertion |
| 3 | Controller/worker split + 60 s idle unload | Resident ~870 MB only while needed; controller stays ≤8 MB |
| 4 | Live streaming + catch-up replay + mic button | Live text while you talk; click the button to stop; final text typed at the cursor |
| 5 | Filler-word removal, trailing space, config file, autostart, packaging | Shippable portable tool |
| 6 | Optional hardening | Firewall block rules; retire Handy |

---

## 11. Risks

| Risk | Mitigation |
|---|---|
| Hand-written C ABI bindings are subtly wrong | Validate `transcribe_version()` and every struct size via `transcribe_abi_struct_size()` at startup; fail loudly; compare against the published 0.4.0 header |
| `mmap` load time longer than assumed | Measure in Milestone 1; if slow, the buffer+catch-up design still hides it (the user talks for ≥5 s anyway) |
| GPU/Vulkan instability | CPU is the default; Vulkan is opt-in and off by default |
| Right Ctrl / Right Alt conflicts with other software | Combo is configurable; documented in the README |
| Elevated target windows reject synthetic input | Detected and reported on the mic button |

---

## 12. Explicitly out of scope

LLM post-processing · live translation · custom word lists · language detection · multi-language
filler gating · model catalogue / download UI · update mechanism · file-transcription history UI ·
audio feedback sounds · macOS/Linux · mobile.
