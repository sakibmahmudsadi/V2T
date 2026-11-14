#!/usr/bin/env python3
"""Idle-unload test.

Starts the controller with a short `model_unload_seconds`, takes one dictation
(so the worker is spawned and the model is loaded), and checks that the worker
process exits once the model has been idle — which is what returns the ~1 GB
to the operating system.

Usage:
    python tools/idle_unload_test.py [--idle-seconds 5]
"""

import argparse
import ctypes
import ctypes.wintypes
import json
import subprocess
import sys
import time
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO / "tools"))

from controller_test import key, press_combo, VK_RCONTROL, VK_RMENU  # noqa: E402

kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
kernel32.CreateToolhelp32Snapshot.restype = ctypes.wintypes.HANDLE
kernel32.Process32FirstW.restype = ctypes.wintypes.BOOL
kernel32.Process32NextW.restype = ctypes.wintypes.BOOL


class PROCESSENTRY32W(ctypes.Structure):
    _fields_ = [
        ("dwSize", ctypes.wintypes.DWORD), ("cntUsage", ctypes.wintypes.DWORD),
        ("th32ProcessID", ctypes.wintypes.DWORD),
        ("th32DefaultHeapID", ctypes.c_size_t),
        ("th32ModuleID", ctypes.wintypes.DWORD),
        ("cntThreads", ctypes.wintypes.DWORD),
        ("th32ParentProcessID", ctypes.wintypes.DWORD),
        ("pcPriClassBase", ctypes.c_long), ("dwFlags", ctypes.wintypes.DWORD),
        ("szExeFile", ctypes.c_wchar * 260),
    ]


def worker_pids():
    snapshot = kernel32.CreateToolhelp32Snapshot(0x00000002, 0)
    if snapshot == ctypes.c_void_p(-1).value:
        return []
    entry = PROCESSENTRY32W()
    entry.dwSize = ctypes.sizeof(PROCESSENTRY32W)
    pids = []
    if kernel32.Process32FirstW(snapshot, ctypes.byref(entry)):
        while True:
            if entry.szExeFile == "ParakeetTypeWorker.exe":
                pids.append(entry.th32ProcessID)
            if not kernel32.Process32NextW(snapshot, ctypes.byref(entry)):
                break
    kernel32.CloseHandle(snapshot)
    return pids


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--idle-seconds", type=int, default=5)
    parser.add_argument("--app", default=str(REPO / "build" / "ParakeetType.exe"))
    args = parser.parse_args()

    app = Path(args.app)
    if not app.exists():
        sys.exit(f"error: the controller is not built ({app})")

    # A configuration with a short idle timeout, so the test does not take a
    # minute.
    config_path = app.parent / "config_idle_test.json"
    base = json.loads((REPO / "config.json").read_text(encoding="utf-8"))
    base["model_unload_seconds"] = args.idle_seconds
    config_path.write_text(json.dumps(base, indent=2), encoding="utf-8")

    print(f"controller: {app}")
    print(f"idle timeout: {args.idle_seconds} s")
    process = subprocess.Popen([str(app), "--config", str(config_path)],
                               stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    failures = 0
    try:
        time.sleep(1.5)
        if worker_pids():
            print("  FAIL a worker exists before the first dictation")
            failures += 1
        else:
            print("  ok   no worker while idle")

        press_combo()
        time.sleep(3.0)
        pids = worker_pids()
        spawned = len(pids) > 0
        print(f"  {'ok  ' if spawned else 'FAIL'} the worker starts for the dictation")
        if not spawned:
            failures += 1

        press_combo()  # stop
        time.sleep(3.0)

        deadline = time.time() + args.idle_seconds + 15
        exited = False
        while time.time() < deadline:
            if not worker_pids():
                exited = True
                break
            time.sleep(0.5)

        print(f"  {'ok  ' if exited else 'FAIL'} the worker exits after the idle timeout")
        if not exited:
            failures += 1
    finally:
        process.terminate()
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            process.kill()
        for pid in worker_pids():
            subprocess.run(["taskkill", "/F", "/PID", str(pid)], capture_output=True)
        if config_path.exists():
            config_path.unlink()

    print()
    if failures:
        print(f"FAILED: {failures} check(s)")
        return 1
    print("PASS: the model is released after the idle timeout")
    return 0


if __name__ == "__main__":
    sys.exit(main())
