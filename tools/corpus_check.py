#!/usr/bin/env python3
"""Golden-corpus regression check.

Runs V2T's worker over the recordings Handy kept in
%APPDATA%/com.pais.handy/recordings and compares the transcripts with the ones
Handy stored in %APPDATA%/com.pais.handy/history.db.

The corpus is the user's own dictation, so it is both an accuracy check and a
regression guard: if the word error rate climbs above the threshold, something
in the engine binding or the text rules broke.

Usage:
    python tools/corpus_check.py [--worker build/V2TWorker.exe]
                                 [--model <gguf>] [--threshold 5.0]
"""

import argparse
import os
import sqlite3
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
HANDY_ROAMING = Path(os.environ.get("APPDATA", "")) / "com.pais.handy"

DEFAULT_MODEL = (
    Path(os.environ.get("USERPROFILE", "C:/Users/SADI"))
    / ".cache/huggingface/hub/models--handy-computer--parakeet-unified-en-0.6b-gguf"
    / "snapshots/7e948f21b7bdbac698d3318db9d350f1096f3b6c"
    / "parakeet-unified-en-0.6b-Q8_0.gguf"
)


def word_error_rate(reference: str, hypothesis: str) -> float:
    """Word-level Levenshtein distance as a percentage of the reference length."""
    ref = reference.split()
    hyp = hypothesis.split()
    if not ref:
        return 0.0 if not hyp else 100.0

    previous = list(range(len(hyp) + 1))
    for i, r in enumerate(ref, start=1):
        current = [i]
        for j, h in enumerate(hyp, start=1):
            current.append(
                min(
                    previous[j] + 1,          # deletion
                    current[j - 1] + 1,       # insertion
                    previous[j - 1] + (r != h),  # substitution
                )
            )
        previous = current
    return previous[len(hyp)] / len(ref) * 100.0


def load_expectations() -> dict:
    db_path = HANDY_ROAMING / "history.db"
    if not db_path.exists():
        sys.exit(f"error: Handy history database not found at {db_path}")

    connection = sqlite3.connect(f"file:{db_path}?mode=ro", uri=True)
    try:
        rows = connection.execute(
            "select file_name, transcription_text from transcription_history"
        ).fetchall()
    finally:
        connection.close()
    return {name: text for name, text in rows}


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--worker", default=str(REPO / "build" / "V2TWorker.exe"))
    parser.add_argument("--model", default=str(DEFAULT_MODEL))
    parser.add_argument("--threshold", type=float, default=5.0,
                        help="maximum allowed corpus word error rate, in percent")
    parser.add_argument("--file-threshold", type=float, default=15.0,
                        help="maximum allowed word error rate for a single recording")
    parser.add_argument("--recordings", default=str(HANDY_ROAMING / "recordings"))
    args = parser.parse_args()

    worker = Path(args.worker)
    if not worker.exists():
        sys.exit(f"error: worker not found at {worker} (build it first)")
    model = Path(args.model)
    if not model.exists():
        sys.exit(f"error: model not found at {model}")

    recordings = Path(args.recordings)
    if not recordings.is_dir():
        sys.exit(f"error: recordings directory not found at {recordings}")

    expectations = load_expectations()
    wavs = sorted(recordings.glob("*.wav"))
    if not wavs:
        sys.exit(f"error: no WAV files in {recordings}")

    print(f"worker  : {worker}")
    print(f"model   : {model}")
    print(f"corpus  : {len(wavs)} recordings, {len(expectations)} Handy transcripts\n")

    header = f"{'file':<28} {'WER':>7} {'load ms':>8} {'run ms':>8}  actual"
    print(header)
    print("-" * len(header))

    total_errors = 0
    total_words = 0
    failures = 0

    for wav in wavs:
        expected = expectations.get(wav.name)
        if expected is None:
            print(f"{wav.name:<28} {'-':>7} {'-':>8} {'-':>8}  (no Handy transcript)")
            continue

        result = subprocess.run(
            [str(worker), "--model", str(model), "--file", str(wav), "--json"],
            capture_output=True,
            text=True,
        )
        if result.returncode != 0:
            print(f"{wav.name:<28} {'ERR':>7} {'-':>8} {'-':>8}  {result.stderr.strip()}")
            failures += 1
            continue

        import json
        payload = json.loads(result.stdout)
        actual = payload["text"].strip()
        wer = word_error_rate(expected.strip(), actual)

        ref_words = len(expected.split())
        total_errors += wer / 100.0 * ref_words
        total_words += ref_words
        if wer > args.file_threshold:
            failures += 1

        print(f"{wav.name:<28} {wer:>6.1f}% {payload['load_ms']:>8.0f} "
              f"{payload['run_ms']:>8.0f}  {actual[:60]}")

    print()
    if total_words == 0:
        print("error: no comparable transcripts")
        return 1

    corpus_wer = total_errors / total_words * 100.0
    print(f"corpus word error rate: {corpus_wer:.2f}%  (threshold {args.threshold:.1f}%)")
    if corpus_wer > args.threshold:
        print(f"FAILED: the corpus is above the threshold")
        failures += 1
    if failures:
        print(f"FAILED: {failures} recording(s) above the per-file threshold")
        return 1
    print("PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
