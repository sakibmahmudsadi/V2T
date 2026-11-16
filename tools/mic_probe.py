#!/usr/bin/env python3
"""Microphone probe.

Opens the default capture device with waveIn and reports the signal level, so
a silent capture can be told apart from a broken model.

It tries the format V2T asks for (16 kHz mono 16-bit) and the device's native
format (48 kHz stereo 16-bit), so a mapper that cannot convert is visible.

Usage:
    python tools/mic_probe.py [--seconds 3]
"""

import argparse
import ctypes
import ctypes.wintypes
import math
import sys
import time

winmm = ctypes.WinDLL("winmm")
kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)

WAVE_MAPPER = -1
WAVE_FORMAT_PCM = 1
CALLBACK_FUNCTION = 0x30000
WAVEINPROC = ctypes.WINFUNCTYPE(None, ctypes.c_void_p, ctypes.c_uint,
                                ctypes.c_void_p, ctypes.c_void_p, ctypes.c_uint)


class WAVEHDR(ctypes.Structure):
    _fields_ = [
        ("lpData", ctypes.c_void_p),
        ("dwBufferLength", ctypes.c_uint),
        ("dwBytesRecorded", ctypes.c_uint),
        ("dwUser", ctypes.c_void_p),
        ("dwFlags", ctypes.c_uint),
        ("dwLoops", ctypes.c_uint),
        ("lpNext", ctypes.c_void_p),
        ("reserved", ctypes.c_void_p),
    ]


class WAVEFORMATEX(ctypes.Structure):
    _fields_ = [
        ("wFormatTag", ctypes.c_ushort),
        ("nChannels", ctypes.c_ushort),
        ("nSamplesPerSec", ctypes.c_uint),
        ("nAvgBytesPerSec", ctypes.c_uint),
        ("nBlockAlign", ctypes.c_ushort),
        ("wBitsPerSample", ctypes.c_ushort),
        ("cbSize", ctypes.c_ushort),
    ]


def probe(rate, channels, bits, seconds):
    block_align = channels * bits // 8
    fmt = WAVEFORMATEX()
    fmt.wFormatTag = WAVE_FORMAT_PCM
    fmt.nChannels = channels
    fmt.nSamplesPerSec = rate
    fmt.wBitsPerSample = bits
    fmt.nBlockAlign = block_align
    fmt.nAvgBytesPerSec = rate * block_align

    handle = ctypes.c_void_p()
    if winmm.waveInOpen(ctypes.byref(handle), WAVE_MAPPER, ctypes.byref(fmt), None, 0,
                        CALLBACK_FUNCTION) != 0:
        return None, "waveInOpen failed"

    buffer_bytes = rate // 4 * block_align  # 250 ms
    buffers = []
    headers = []
    for _ in range(4):
        data = ctypes.create_string_buffer(buffer_bytes)
        header = WAVEHDR()
        header.lpData = ctypes.cast(data, ctypes.c_void_p)
        header.dwBufferLength = buffer_bytes
        if winmm.waveInPrepareHeader(handle, ctypes.byref(header), ctypes.sizeof(header)) != 0:
            winmm.waveInClose(handle)
            return None, "waveInPrepareHeader failed"
        headers.append(header)
        buffers.append(data)
        if winmm.waveInAddBuffer(handle, ctypes.byref(header), ctypes.sizeof(header)) != 0:
            winmm.waveInClose(handle)
            return None, "waveInAddBuffer failed"

    if winmm.waveInStart(handle) != 0:
        winmm.waveInClose(handle)
        return None, "waveInStart failed"

    peak = 0.0
    total = 0.0
    samples = 0
    deadline = time.time() + seconds
    while time.time() < deadline:
        time.sleep(0.05)
        for header, data in zip(headers, buffers):
            if header.dwFlags & 0x1:  # WHDR_DONE
                recorded = header.dwBytesRecorded
                raw = bytes(data)[:recorded]
                if bits == 16:
                    count = len(raw) // 2
                    values = memoryview(raw).cast("h")
                    for v in values:
                        value = v / 32768.0
                        total += value * value
                        peak = max(peak, abs(value))
                    samples += count
                header.dwFlags = 0
                winmm.waveInAddBuffer(handle, ctypes.byref(header), ctypes.sizeof(header))

    winmm.waveInStop(handle)
    winmm.waveInReset(handle)
    for header in headers:
        winmm.waveInUnprepareHeader(handle, ctypes.byref(header), ctypes.sizeof(header))
    winmm.waveInClose(handle)

    rms = math.sqrt(total / samples) if samples else 0.0
    return (peak, rms, samples), None


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--seconds", type=float, default=3.0)
    args = parser.parse_args()

    print(f"probing the default capture device for {args.seconds:.0f} s\n")
    for label, rate, channels, bits in [
            ("V2T asks for  : 16 kHz mono 16-bit", 16000, 1, 16),
            ("native guess  : 48 kHz stereo 16-bit", 48000, 2, 16)]:
        result, error = probe(rate, channels, bits, args.seconds)
        if error:
            print(f"  {label}: {error}")
            continue
        peak, rms, samples = result
        level = "SILENT" if peak < 0.001 else "has signal"
        print(f"  {label}: {samples} samples, peak {peak:.4f}, rms {rms:.5f}  -> {level}")
    print("\nIf the first line is SILENT and the second has signal, the Windows audio")
    print("mapper cannot convert to 16 kHz mono — V2T must capture at the native format.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
