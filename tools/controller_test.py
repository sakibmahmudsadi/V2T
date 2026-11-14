#!/usr/bin/env python3
"""Controller smoke test.

Starts the real controller, injects Right Ctrl + Right Alt (a synthetic
keystroke is seen by the low-level hook exactly like a real one), and checks
that:

  1. the controller starts and installs the hook,
  2. the hotkey creates the mic button (the app's only visible element),
  3. pressing it again stops the recording and the button goes away,
  4. the log records the state transitions.

The audio path itself is covered by tools/ipc_test.py with a real recording.

Usage:
    python tools/controller_test.py [--app build/ParakeetType.exe]
"""

import argparse
import ctypes
import ctypes.wintypes
import subprocess
import sys
import time
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent

user32 = ctypes.WinDLL("user32", use_last_error=True)
user32.FindWindowW.restype = ctypes.wintypes.HWND
user32.FindWindowW.argtypes = [ctypes.wintypes.LPCWSTR, ctypes.wintypes.LPCWSTR]
user32.IsWindowVisible.restype = ctypes.wintypes.BOOL
user32.IsWindowVisible.argtypes = [ctypes.wintypes.HWND]

KEYEVENTF_KEYUP = 0x0002
KEYEVENTF_EXTENDEDKEY = 0x0001
INPUT_KEYBOARD = 1
VK_RCONTROL = 0xA3
VK_RMENU = 0xA5
VK_ESCAPE = 0x1B


class KEYBDINPUT(ctypes.Structure):
    _fields_ = [("wVk", ctypes.wintypes.WORD), ("wScan", ctypes.wintypes.WORD),
                ("dwFlags", ctypes.wintypes.DWORD), ("time", ctypes.wintypes.DWORD),
                ("dwExtraInfo", ctypes.POINTER(ctypes.wintypes.ULONG))]


class INPUTUNION(ctypes.Union):
    _fields_ = [("ki", KEYBDINPUT), ("padding", ctypes.c_byte * 32)]


class INPUT(ctypes.Structure):
    _anonymous_ = ("u",)
    _fields_ = [("type", ctypes.wintypes.DWORD), ("u", INPUTUNION)]


user32.SendInput.restype = ctypes.wintypes.UINT
user32.SendInput.argtypes = [ctypes.wintypes.UINT, ctypes.POINTER(INPUT), ctypes.c_int]


def key(vk, up=False):
    flags = KEYEVENTF_EXTENDEDKEY | (KEYEVENTF_KEYUP if up else 0)
    event = INPUT(type=INPUT_KEYBOARD)
    event.ki = KEYBDINPUT(wVk=vk, wScan=0, dwFlags=flags, time=0, dwExtraInfo=None)
    sent = user32.SendInput(1, ctypes.byref(event), ctypes.sizeof(INPUT))
    return sent == 1


def press_combo():
    key(VK_RCONTROL)
    key(VK_RMENU)
    time.sleep(0.05)
    key(VK_RMENU, up=True)
    key(VK_RCONTROL, up=True)


def find_mic_button():
    """True when the mic button window exists *and* is on screen.

    The window is created once and hidden between takes, so existence alone
    proves nothing — visibility does.
    """
    handle = user32.FindWindowW("ParakeetTypeMicButton", None)
    if not handle:
        return False
    return user32.IsWindowVisible(handle) != 0


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--app", default=str(REPO / "build" / "ParakeetType.exe"))
    args = parser.parse_args()

    app = Path(args.app)
    if not app.exists():
        sys.exit(f"error: the controller is not built ({app})")

    log_paths = [app.parent / "logs" / "parakeettype.log", REPO / "logs" / "parakeettype.log"]
    for path in log_paths:
        if path.exists():
            path.unlink()

    print(f"controller: {app}")
    process = subprocess.Popen([str(app)], stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                               text=True)
    failures = 0
    try:
        time.sleep(1.5)
        if process.poll() is not None:
            out, err = process.communicate()
            sys.exit(f"error: the controller exited immediately ({process.returncode})\n"
                     f"{out}\n{err}")

        # 1. the hotkey creates the mic button
        press_combo()
        appeared = False
        for _ in range(40):
            time.sleep(0.1)
            if find_mic_button():
                appeared = True
                break
        print(f"  {'ok  ' if appeared else 'FAIL'} the hotkey shows the mic button")
        if not appeared:
            failures += 1

        # 2. pressing it again stops the recording
        time.sleep(1.0)
        press_combo()
        disappeared = False
        for _ in range(60):
            time.sleep(0.1)
            if not find_mic_button():
                disappeared = True
                break
        print(f"  {'ok  ' if disappeared else 'FAIL'} the hotkey again stops and hides it")
        if not disappeared:
            failures += 1

        # 3. the log records what happened
        time.sleep(1.0)
        log = ""
        for path in log_paths:
            if path.exists():
                log += path.read_text(encoding="utf-8", errors="replace")
        started = "recording started" in log
        print(f"  {'ok  ' if started else 'FAIL'} the log records the recording")
        if not started:
            failures += 1
            print(log[-800:])
    finally:
        process.terminate()
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            process.kill()

    print()
    if failures:
        print(f"FAILED: {failures} check(s)")
        return 1
    print("PASS: the controller reacts to the hotkey")
    return 0


if __name__ == "__main__":
    sys.exit(main())
