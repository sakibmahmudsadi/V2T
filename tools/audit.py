#!/usr/bin/env python3
"""Offline audit + memory probe for the ParakeetType binaries.

Offline audit: the import table of the executable must contain no networking
DLL. A program that cannot load ws2_32/winhttp/wininet/urlmon cannot open a
socket — that is the product's core promise, checked mechanically.

Memory probe: runs the worker and reports its peak working set, so the RAM
budget in the spec is backed by a number rather than an estimate.

Usage:
    python tools/audit.py [--worker build/ParakeetTypeWorker.exe]
"""

import argparse
import ctypes
import ctypes.wintypes
import subprocess
import sys
import time
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
NETWORKING_DLLS = {"ws2_32.dll", "winhttp.dll", "wininet.dll", "urlmon.dll",
                   "dnet.dll", "mswsock.dll", "wship6.dll"}


def audit_imports(path: Path):
    import pefile

    pe = pefile.PE(str(path))
    imported = []
    for entry in getattr(pe, "DIRECTORY_ENTRY_IMPORT", []):
        imported.append(entry.dll.decode().lower())
    for entry in getattr(pe, "DIRECTORY_ENTRY_DELAY_IMPORT", []):
        imported.append(entry.dll.decode().lower())

    offenders = sorted(set(imported) & NETWORKING_DLLS)
    print(f"imports of {path.name}: {len(imported)} DLLs")
    for dll in sorted(set(imported)):
        print(f"  {dll}")
    return offenders


class ProcessMemoryCounters(ctypes.Structure):
    _fields_ = [
        ("cb", ctypes.wintypes.DWORD),
        ("PageFaultCount", ctypes.wintypes.DWORD),
        ("PeakWorkingSetSize", ctypes.c_size_t),
        ("WorkingSetSize", ctypes.c_size_t),
        ("QuotaPeakPagedPoolUsage", ctypes.c_size_t),
        ("QuotaPagedPoolUsage", ctypes.c_size_t),
        ("QuotaPeakNonPagedPoolUsage", ctypes.c_size_t),
        ("QuotaNonPagedPoolUsage", ctypes.c_size_t),
        ("PagefileUsage", ctypes.c_size_t),
        ("PeakPagefileUsage", ctypes.c_size_t),
    ]


def peak_working_set(pid: int) -> int:
    counters = ProcessMemoryCounters()
    counters.cb = ctypes.sizeof(ProcessMemoryCounters)
    handle = ctypes.windll.kernel32.OpenProcess(0x1000, False, pid)
    if not handle:
        return 0
    try:
        ctypes.windll.psapi.GetProcessMemoryInfo(
            handle, ctypes.byref(counters), counters.cb)
        return counters.PeakWorkingSetSize
    finally:
        ctypes.windll.kernel32.CloseHandle(handle)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--worker", default=str(REPO / "build" / "ParakeetTypeWorker.exe"))
    parser.add_argument("--model", default="")
    parser.add_argument("--file", default="")
    args = parser.parse_args()

    worker = Path(args.worker)
    failures = 0

    print("=== offline audit ===")
    offenders = audit_imports(worker)
    if offenders:
        print(f"FAILED: networking DLLs present: {', '.join(offenders)}")
        failures += 1
    else:
        print("PASS: no networking DLL in the import table")

    if args.model and args.file:
        print()
        print("=== memory probe ===")
        process = subprocess.Popen(
            [str(worker), "--model", args.model, "--file", args.file, "--json"],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        peak = 0
        while process.poll() is None:
            peak = max(peak, peak_working_set(process.pid))
            time.sleep(0.05)
        peak = max(peak, peak_working_set(process.pid))
        print(f"peak working set: {peak / 1024 / 1024:.1f} MB "
              f"(model resident for the duration)")

    print()
    if failures:
        print(f"FAILED: {failures} audit problem(s)")
        return 1
    print("audit passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
