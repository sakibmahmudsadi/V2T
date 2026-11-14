#!/usr/bin/env python3
"""Frame-size sweep.

The live stream decodes slightly differently depending on how much audio is
handed to it at a time. Handy feeds the frames its VAD produces (16 ms for
Earshot, 32 ms for Silero), so the app should use the same frame size to
produce the same words.

This script decodes the golden corpus with several frame sizes and reports the
word error rate against Handy's stored transcripts.

Usage:
    python tools/frame_sweep.py [--frames 160,320,480,512,640,960]
"""

import argparse
import glob
import json
import os
import sqlite3
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO / "tools"))

from corpus_check import DEFAULT_MODEL, word_error_rate  # noqa: E402

RECORDINGS = Path(os.environ.get("APPDATA", "")) / "com.pais.handy" / "recordings"
DATABASE = RECORDINGS.parent / "history.db"


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--worker", default=str(REPO / "build" / "V2TWorker.exe"))
    parser.add_argument("--model", default=str(DEFAULT_MODEL))
    parser.add_argument("--frames", default="160,320,480,512,640,960")
    args = parser.parse_args()

    if not DATABASE.exists():
        sys.exit(f"error: {DATABASE} not found")
    expectations = dict(sqlite3.connect(str(DATABASE)).execute(
        "select file_name, transcription_text from transcription_history"))

    files = [p for p in sorted(RECORDINGS.glob("*.wav")) if p.name in expectations]
    if not files:
        sys.exit("error: no recordings with a stored transcript")

    print(f"corpus: {len(files)} recordings\n")
    best = None
    for frame in [int(f) for f in args.frames.split(",")]:
        total_errors = 0.0
        total_words = 0
        for path in files:
            result = subprocess.run(
                [args.worker, "--model", args.model, "--file", str(path), "--json",
                 "--frame", str(frame)],
                capture_output=True, text=True)
            if result.returncode != 0:
                sys.exit(f"error: the worker failed on {path.name}: {result.stderr}")
            ours = json.loads(result.stdout)["text"].strip()
            expected = expectations[path.name].strip()
            words = len(expected.split())
            total_errors += word_error_rate(expected, ours) / 100.0 * words
            total_words += words
        wer = total_errors / total_words * 100.0 if total_words else 0.0
        print(f"  frame {frame:>4} samples ({frame / 16:.0f} ms): WER {wer:5.2f}%")
        if best is None or wer < best[1]:
            best = (frame, wer)

    print(f"\nbest: {best[0]} samples ({best[0] / 16:.0f} ms) at {best[1]:.2f}% WER")
    return 0


if __name__ == "__main__":
    sys.exit(main())
