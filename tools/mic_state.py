#!/usr/bin/env python3
"""Reads the default capture endpoint's state through Core Audio.

Answers the question a silence probe cannot: is the microphone muted, at
zero volume, or disabled? A muted or disconnected input is the usual reason
a dictation tool records nothing.

Usage:
    python tools/mic_state.py
"""

import ctypes
import ctypes.wintypes
import sys

ole32 = ctypes.WinDLL("ole32")

CLSCTX_ALL = 23
E_CAPTURE = 1
E_CONSOLE = 0
DEVICE_STATE_ACTIVE = 1


class GUID(ctypes.Structure):
    _fields_ = [("d1", ctypes.c_uint32), ("d2", ctypes.c_uint16), ("d3", ctypes.c_uint16),
                 ("d4", ctypes.c_ubyte * 8)]


def make_guid(text):
    """Builds a GUID in its in-memory layout (the first three fields are
    little-endian on x86)."""
    parts = text.strip("{}").split("-")
    guid = GUID()
    guid.d1 = int(parts[0], 16)
    guid.d2 = int(parts[1], 16)
    guid.d3 = int(parts[2], 16)
    tail = bytes.fromhex(parts[3] + parts[4])
    guid.d4 = (ctypes.c_ubyte * 8).from_buffer_copy(tail)
    return guid


def vtable_call(obj, index, restype, *args):
    table = ctypes.cast(obj, ctypes.POINTER(ctypes.POINTER(ctypes.c_void_p))).contents
    argtypes = [ctypes.c_void_p] + [ctypes.c_void_p] * len(args)
    function = ctypes.WINFUNCTYPE(restype, *argtypes)(table[index])
    return function(obj, *args)


def main():
    unmute = "--unmute" in sys.argv

    result = ole32.CoInitializeEx(None, 2)  # COINIT_APARTMENTTHREADED
    if result not in (0, -2147221008):  # S_FALSE / RPC_E_CHANGED_MODE
        print(f"CoInitializeEx failed: {result}")
        return 1

    enumerator = ctypes.c_void_p()
    clsid = make_guid("{BCDE0395-E52F-467C-8E3D-C4579291692E}")
    iid = make_guid("{A95664D2-9614-4F35-A746-DE8DB63617E6}")
    hr = ole32.CoCreateInstance(ctypes.byref(clsid), None, CLSCTX_ALL, ctypes.byref(iid),
                                 ctypes.byref(enumerator))
    if hr != 0:
        print(f"cannot create the device enumerator (0x{hr & 0xffffffff:08x})")
        return 1

    device = ctypes.c_void_p()
    # IMMDeviceEnumerator: 0,1,2 = IUnknown, 3 = EnumAudioEndpoints, 4 = GetDefaultAudioEndpoint
    hr = vtable_call(enumerator, 4, ctypes.c_long, E_CAPTURE, E_CONSOLE,
                      ctypes.cast(ctypes.pointer(device), ctypes.c_void_p))
    if hr != 0 or not device:
        print(f"no default capture endpoint (0x{hr & 0xffffffff:08x})")
        return 1

    # IMMDevice: 6 = GetState
    state = ctypes.c_uint32(0)
    vtable_call(device, 6, ctypes.c_long, ctypes.cast(ctypes.pointer(state), ctypes.c_void_p))
    print(f"default capture endpoint: device state {state.value} "
          f"({'ACTIVE' if state.value == DEVICE_STATE_ACTIVE else 'NOT ACTIVE'})")

    # IAudioEndpointVolume
    volume_iid = make_guid("{5CDF2C82-841E-4546-9722-0CF74078229A}")
    volume = ctypes.c_void_p()
    # IMMDevice: 3 = Activate
    hr = vtable_call(device, 3, ctypes.c_long,
                      ctypes.cast(ctypes.pointer(volume_iid), ctypes.c_void_p), CLSCTX_ALL, None,
                      ctypes.cast(ctypes.pointer(volume), ctypes.c_void_p))
    if hr != 0 or not volume:
        print("cannot read the endpoint volume")
        return 1

    master = ctypes.c_float(0.0)
    vtable_call(volume, 9, ctypes.c_long,
                ctypes.cast(ctypes.pointer(master), ctypes.c_void_p))  # GetMasterVolumeLevelScalar
    muted = ctypes.c_int(0)
    vtable_call(volume, 15, ctypes.c_long,
                ctypes.cast(ctypes.pointer(muted), ctypes.c_void_p))  # GetMute

    if unmute and muted.value:
        # IAudioEndpointVolume: 14 = SetMute
        vtable_call(volume, 14, ctypes.c_long, 0,
                    ctypes.cast(ctypes.pointer(muted), ctypes.c_void_p))
        vtable_call(volume, 15, ctypes.c_long,
                    ctypes.cast(ctypes.pointer(muted), ctypes.c_void_p))
        print(f"  muted         : {'YES' if muted.value else 'no'}  (after --unmute)")

    print(f"  master volume : {master.value * 100:.0f}%")
    print(f"  muted         : {'YES' if muted.value else 'no'}")

    if muted.value:
        print("\n  -> The microphone is MUTED. Windows Settings > System > Sound > Input, "
              "unmute it.")
    elif master.value < 0.02:
        print("\n  -> The microphone volume is at zero. Raise it in the same place.")
    else:
        print("\n  -> The endpoint looks healthy: check the hardware mute key or the "
              "driver.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
