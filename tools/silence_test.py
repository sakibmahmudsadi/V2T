#!/usr/bin/env python3
"""Regression test: the live stream must not hang on silence.

The microphone delivers silence when the user has not said anything yet. The
RNN-T streaming decoder must return from stream_feed() for silent audio too,
otherwise the controller would block forever on the pipe.

Usage:
    python tools/silence_test.py [--seconds 5]
"""

import argparse
import json
import struct
import subprocess
import sys
import time
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO / "tools"))

from ipc_test import (DEFAULT_MODEL, kernel32, read_json, write_frame)  # noqa: E402

DEFAULT_WORKER = REPO / "build" / "V2TWorker.exe"


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--worker", default=str(DEFAULT_WORKER))
    parser.add_argument("--model", default=str(DEFAULT_MODEL))
    parser.add_argument("--seconds", type=float, default=5.0)
    args = parser.parse_args()

    pipe_name = "v2t-silence-test"
    handle = kernel32.CreateNamedPipeW(
        "\\\\.\\pipe\\" + pipe_name, 3, 0, 1, 65536, 65536, 0, None)
    if handle is None:
        sys.exit("cannot create the pipe")

    worker = subprocess.Popen([args.worker, "--serve", "--pipe", pipe_name],
                              stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        kernel32.ConnectNamedPipe(handle, None)

        write_frame(handle, 1, json.dumps({"cmd": "load", "model": args.model}).encode())
        reply = read_json(handle)
        if not reply.get("ok"):
            sys.exit(f"load failed: {reply.get('error')}")
        print("load  : ok")

        write_frame(handle, 1, json.dumps({"cmd": "begin"}).encode())
        reply = read_json(handle)
        if not reply.get("ok"):
            sys.exit(f"begin failed: {reply.get('error')}")
        print("begin : ok")

        # Digital silence, in the same 30 ms frames the microphone produces.
        chunks = int(args.seconds * 16000 / 4000)
        start = time.time()
        for index in range(chunks):
            chunk = [0.0] * 4000
            write_frame(handle, 2, struct.pack(f"<{len(chunk)}f", *chunk))
        print(f"fed   : {chunks} silent chunks ({args.seconds:.1f} s) in "
              f"{time.time() - start:.1f} s")

        write_frame(handle, 1, json.dumps({"cmd": "finalize"}).encode())
        reply = read_json(handle)
        print(f"finalize: ok={reply.get('ok')} text={reply.get('text', '')[:40]!r}")

        write_frame(handle, 1, json.dumps({"cmd": "shutdown"}).encode())
        read_json(handle)
    finally:
        kernel32.CloseHandle(handle)
        try:
            worker.wait(timeout=10)
        except subprocess.TimeoutExpired:
            worker.kill()

    print("\nPASS: the stream does not hang on silence")
    return 0


if __name__ == "__main__":
    sys.exit(main())
