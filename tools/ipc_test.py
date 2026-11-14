#!/usr/bin/env python3
"""End-to-end test of the controller <-> worker protocol.

Spawns the real worker in serve mode, talks to it over the same named pipe
the controller uses, and streams a real recording through it. It proves the
whole offline path: framed IPC, model load, live streaming decode, finalize.

Usage:
    python tools/ipc_test.py [--worker build/ParakeetTypeWorker.exe]
                              [--model <gguf>] [--file <wav>]
"""

import argparse
import ctypes
import ctypes.wintypes
import json
import os
import struct
import subprocess
import sys
import time
import wave
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
DEFAULT_MODEL = (
    Path("C:/Users/SADI/.cache/huggingface/hub/models--handy-computer--parakeet-unified-en-0.6b-gguf"
         "/snapshots/7e948f21b7bdbac698d3318db9d350f1096f3b6c"
         "/parakeet-unified-en-0.6b-Q8_0.gguf")
)
DEFAULT_FILE = Path(
    "C:/Users/SADI/AppData/Roaming/com.pais.handy/recordings/handy-1794595283.wav")

PIPE_ACCESS_DUPLEX = 0x00000003
PIPE_TYPE_BYTE = 0x00000000
PIPE_READMODE_BYTE = 0x00000000
PIPE_WAIT = 0x00000000
INVALID_HANDLE_VALUE = ctypes.c_void_p(-1).value

kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
kernel32.CreateNamedPipeW.restype = ctypes.wintypes.HANDLE
kernel32.CreateNamedPipeW.argtypes = [ctypes.wintypes.LPCWSTR, ctypes.wintypes.DWORD,
                                      ctypes.wintypes.DWORD, ctypes.wintypes.DWORD,
                                      ctypes.wintypes.DWORD, ctypes.wintypes.DWORD,
                                      ctypes.wintypes.DWORD, ctypes.c_void_p]
kernel32.ConnectNamedPipe.restype = ctypes.wintypes.BOOL
kernel32.ConnectNamedPipe.argtypes = [ctypes.wintypes.HANDLE, ctypes.c_void_p]
kernel32.ReadFile.restype = ctypes.wintypes.BOOL
kernel32.ReadFile.argtypes = [ctypes.wintypes.HANDLE, ctypes.c_void_p, ctypes.wintypes.DWORD,
                               ctypes.POINTER(ctypes.wintypes.DWORD), ctypes.c_void_p]
kernel32.WriteFile.restype = ctypes.wintypes.BOOL
kernel32.WriteFile.argtypes = [ctypes.wintypes.HANDLE, ctypes.c_void_p, ctypes.wintypes.DWORD,
                                ctypes.POINTER(ctypes.wintypes.DWORD), ctypes.c_void_p]
kernel32.CloseHandle.restype = ctypes.wintypes.BOOL
kernel32.CloseHandle.argtypes = [ctypes.wintypes.HANDLE]


def write_frame(handle, kind, payload: bytes):
    header = struct.pack("<II", len(payload), kind)
    assert kernel32.WriteFile(handle, header, len(header), None, None), "write header failed"
    if payload:
        assert kernel32.WriteFile(handle, payload, len(payload), None, None), "write payload failed"


def read_exact(handle, count: int) -> bytes:
    buffer = ctypes.create_string_buffer(count)
    read = ctypes.wintypes.DWORD(0)
    assert kernel32.ReadFile(handle, buffer, count, ctypes.byref(read), None), "read failed"
    if read.value != count:
        raise EOFError(f"short read: {read.value} of {count}")
    return buffer.raw[:count]


def read_frame(handle):
    length, kind = struct.unpack("<II", read_exact(handle, 8))
    payload = read_exact(handle, length) if length else b""
    return kind, payload


def read_json(handle):
    kind, payload = read_frame(handle)
    assert kind == 1, f"expected a control frame, got kind {kind}"
    return json.loads(payload.decode("utf-8"))


def load_wav_as_float32(path: Path):
    with wave.open(str(path)) as wav:
        assert wav.getnchannels() == 1 and wav.getsampwidth() == 2, "expected 16-bit mono"
        rate = wav.getframerate()
        frames = wav.readframes(wav.getnframes())
    samples = struct.unpack(f"<{len(frames) // 2}h", frames)
    pcm = [s / 32768.0 for s in samples]
    return pcm, rate


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--worker", default=str(REPO / "build" / "ParakeetTypeWorker.exe"))
    parser.add_argument("--model", default=str(DEFAULT_MODEL))
    parser.add_argument("--file", default=str(DEFAULT_FILE))
    args = parser.parse_args()

    worker = Path(args.worker)
    model = Path(args.model)
    audio_file = Path(args.file)
    for path, label in ((worker, "worker"), (model, "model"), (audio_file, "audio file")):
        if not path.exists():
            sys.exit(f"error: {label} not found at {path}")

    pipe_name = "parakeettype-ipctest"
    handle = kernel32.CreateNamedPipeW(
        f"\\\\.\\pipe\\{pipe_name}", PIPE_ACCESS_DUPLEX,
        PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT, 1, 65536, 65536, 0, None)
    if handle == INVALID_HANDLE_VALUE:
        sys.exit(f"error: cannot create the pipe (error {ctypes.get_last_error()})")

    print(f"worker : {worker}")
    print(f"model  : {model}")
    print(f"audio  : {audio_file}")

    process = subprocess.Popen([str(worker), "--serve", "--pipe", pipe_name],
                               stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        connected = kernel32.ConnectNamedPipe(handle, None)
        if not connected and ctypes.get_last_error() != 535:  # ERROR_PIPE_CONNECTED
            sys.exit(f"error: the worker did not connect (error {ctypes.get_last_error()})")

        pcm, rate = load_wav_as_float32(audio_file)
        if rate != 16000:
            sys.exit(f"error: expected 16 kHz audio, got {rate}")

        # 1. load
        write_frame(handle, 1, json.dumps({"cmd": "load", "model": str(model)}).encode())
        reply = read_json(handle)
        if not reply.get("ok"):
            sys.exit(f"error: load failed: {reply.get('error')}")
        print(f"\nload   : ok in {reply.get('load_ms', 0):.0f} ms")

        # 2. begin
        write_frame(handle, 1, json.dumps({"cmd": "begin"}).encode())
        reply = read_json(handle)
        if not reply.get("ok"):
            sys.exit(f"error: begin failed: {reply.get('error')}")
        print("begin  : ok")

        # 3. stream the audio in 30 ms frames, like the microphone does
        frame_samples = 480
        for offset in range(0, len(pcm), frame_samples):
            chunk = pcm[offset:offset + frame_samples]
            write_frame(handle, 2, struct.pack(f"<{len(chunk)}f", *chunk))
        print(f"fed    : {len(pcm)} samples ({len(pcm) / 16000:.1f} s) in 30 ms frames")

        # 4. finalize
        write_frame(handle, 1, json.dumps({"cmd": "finalize"}).encode())
        reply = read_json(handle)
        if not reply.get("ok"):
            sys.exit(f"error: finalize failed: {reply.get('error')}")
        text = reply.get("text", "")
        print(f"\ntext   : {text}")

        # 5. shutdown
        write_frame(handle, 1, json.dumps({"cmd": "shutdown"}).encode())
        read_json(handle)
    finally:
        kernel32.CloseHandle(handle)
        try:
            process.wait(timeout=10)
        except subprocess.TimeoutExpired:
            process.kill()

    print("\nPASS: the controller <-> worker protocol works end to end")
    return 0


if __name__ == "__main__":
    sys.exit(main())
