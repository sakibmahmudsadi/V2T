#!/usr/bin/env python3
"""Edge-case suite for the V2T worker.

Every case builds a deliberately awkward input and checks that the worker
fails cleanly (clear message, non-zero exit) instead of crashing, hanging, or
silently producing nonsense.

Usage:
    python tools/edge_cases.py [--worker build/V2TWorker.exe]
"""

import argparse
import math
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
DEFAULT_MODEL = (
    Path("C:/Users/SADI/.cache/huggingface/hub/models--handy-computer--parakeet-unified-en-0.6b-gguf"
         "/snapshots/7e948f21b7bdbac698d3318db9d350f1096f3b6c"
         "/parakeet-unified-en-0.6b-Q8_0.gguf")
)


def wav_pcm16(samples, rate=16000, channels=1):
    data = b"".join(struct.pack("<h", max(-32768, min(32767, int(s * 32767)))) for s in samples)
    block = channels * 2
    return (b"RIFF" + struct.pack("<I", 36 + len(data)) + b"WAVEfmt " +
            struct.pack("<IHHIIHH", 16, 1, channels, rate, rate * block, block, 16) +
            b"data" + struct.pack("<I", len(data)) + data)


def wav_float32(samples, rate=16000):
    data = b"".join(struct.pack("<f", s) for s in samples)
    return (b"RIFF" + struct.pack("<I", 36 + len(data)) + b"WAVEfmt " +
            struct.pack("<IHHIIHH", 16, 3, 1, rate, rate * 4, 4, 32) +
            b"data" + struct.pack("<I", len(data)) + data)


def tone(seconds, rate=16000, freq=220.0):
    return [0.4 * math.sin(2 * math.pi * freq * i / rate) for i in range(int(seconds * rate))]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--worker", default=str(REPO / "build" / "V2TWorker.exe"))
    parser.add_argument("--model", default=str(DEFAULT_MODEL))
    args = parser.parse_args()

    worker = Path(args.worker)
    model = Path(args.model)

    failures = 0
    tmp = Path(tempfile.mkdtemp(prefix="v2t-edge-"))

    def run(files, extra=None):
        cmd = [str(worker), "--model", str(model)] + [str(f) for f in files] + (extra or [])
        return subprocess.run(cmd, capture_output=True, text=True, timeout=300)

    def expect_failure(label, result, expect_in_message=None):
        nonlocal failures
        ok = result.returncode != 0
        message = (result.stderr or "").strip().splitlines()
        detail = message[-1] if message else "(no stderr)"
        if ok and expect_in_message and expect_in_message.lower() not in detail.lower():
            ok = False
            detail = f"{detail}  (expected it to mention '{expect_in_message}')"
        print(f"  {'ok  ' if ok else 'FAIL'} {label:<34} exit={result.returncode}  {detail[:90]}")
        if not ok:
            failures += 1

    def expect_success(label, result):
        nonlocal failures
        ok = result.returncode == 0
        detail = (result.stdout or "").strip().splitlines()
        text = detail[0] if detail else ""
        print(f"  {'ok  ' if ok else 'FAIL'} {label:<34} exit={result.returncode}  {text[:90]}")
        if not ok:
            print((result.stderr or "")[:400])
            failures += 1

    print("edge cases")

    # --- malformed inputs --------------------------------------------------
    empty = tmp / "empty.wav"
    empty.write_bytes(b"")
    expect_failure("empty file", run(["--file", str(empty)]), "empty")

    junk = tmp / "junk.wav"
    junk.write_bytes(b"this is not a wav file at all")
    expect_failure("not a RIFF file", run(["--file", str(junk)]), "RIFF")

    header_only = tmp / "header.wav"
    header_only.write_bytes(wav_pcm16([]))
    expect_failure("no audio frames", run(["--file", str(header_only)]), "frames")

    missing = tmp / "does-not-exist.wav"
    expect_failure("missing file", run(["--file", str(missing)]), "cannot open")

    # --- unusual but valid audio ------------------------------------------
    one_sample = tmp / "one-sample.wav"
    one_sample.write_bytes(wav_pcm16([0.5]))
    expect_failure("one sample (too short)", run(["--file", str(one_sample)]), "too short")

    eight_k = tmp / "8k.wav"
    eight_k.write_bytes(wav_pcm16(tone(2.0, rate=8000), rate=8000))
    expect_success("8 kHz (resampled)", run(["--file", str(eight_k)]))

    stereo = tmp / "stereo.wav"
    stereo.write_bytes(wav_pcm16(tone(2.0) * 2, channels=2))
    expect_success("stereo (downmixed)", run(["--file", str(stereo)]))

    float_wav = tmp / "float.wav"
    float_wav.write_bytes(wav_float32(tone(2.0)))
    expect_success("32-bit float WAV", run(["--file", str(float_wav)]))

    long_wav = tmp / "long.wav"
    long_wav.write_bytes(wav_pcm16(tone(75.0)))
    expect_success("75 s (past the model's window)", run(["--file", str(long_wav)]))

    # --- bad arguments -----------------------------------------------------
    bad_model = tmp / "not-a-model.gguf"
    bad_model.write_bytes(b"nope")
    expect_failure("model file is not a model", run(["--file", str(eight_k), "--model", str(bad_model)]),
                   "model")

    result = subprocess.run([str(worker), "--model", str(model)], capture_output=True, text=True)
    ok = result.returncode != 0 and "--model" in (result.stdout + result.stderr)
    print(f"  {'ok  ' if ok else 'FAIL'} {'missing --file':<34} exit={result.returncode}")
    if not ok:
        failures += 1

    result = subprocess.run([str(worker), "--model", str(model), "--file", str(eight_k),
                             "--backend", "nonsense"], capture_output=True, text=True)
    ok = result.returncode != 0 and "backend" in (result.stdout + result.stderr).lower()
    print(f"  {'ok  ' if ok else 'FAIL'} {'unknown backend':<34} exit={result.returncode}")
    if not ok:
        failures += 1

    result = subprocess.run([str(worker), "--engine", str(tmp / "nope.dll"), "--selftest"],
                            capture_output=True, text=True)
    ok = result.returncode != 0 and "cannot load" in (result.stderr or "").lower()
    print(f"  {'ok  ' if ok else 'FAIL'} {'missing engine DLL':<34} exit={result.returncode}")
    if not ok:
        failures += 1

    print()
    if failures:
        print(f"FAILED: {failures} edge case(s)")
        return 1
    print("all edge cases handled cleanly")
    return 0


if __name__ == "__main__":
    sys.exit(main())
