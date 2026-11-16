#!/usr/bin/env python3
"""Renders the mic button and checks that it produced a real image.

The button is the app's only visible element, so a broken icon has to be
caught by a test rather than by the user.

Usage:
    python tools/icon_test.py [--app build/V2T.exe]
"""

import argparse
import struct
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--app", default=str(REPO / "build" / "V2T.exe"))
    args = parser.parse_args()

    app = Path(args.app)
    if not app.exists():
        sys.exit(f"error: the controller is not built ({app})")

    output = REPO / "build" / "mic-button.bmp"
    import subprocess
    result = subprocess.run([str(app), "--render-icon", str(output)],
                            capture_output=True, text=True, timeout=60)
    if result.returncode != 0 or not output.exists():
        sys.exit(f"error: the icon was not rendered: {result.stderr}")

    data = output.read_bytes()
    if data[:2] != b"BM":
        sys.exit("error: the output is not a bitmap")
    width = struct.unpack_from("<i", data, 18)[0]
    height = abs(struct.unpack_from("<i", data, 22)[0])
    offset = struct.unpack_from("<I", data, 10)[0]

    # The glyph is drawn in green (the "audio is arriving" state): count the
    # green pixels to prove something was drawn.
    stride = ((width * 3 + 3) // 4) * 4
    green = 0
    for y in range(height):
        row = data[offset + y * stride: offset + y * stride + width * 3]
        for x in range(width):
            b, g, r = row[x * 3], row[x * 3 + 1], row[x * 3 + 2]
            if g > 120 and g > r + 40 and g > b + 40:
                green += 1

    print(f"  icon: {width}x{height}, {green} green pixels")
    if green < 200:
        sys.exit("error: the icon looks empty (too few lit pixels)")
    print("PASS: the mic button renders a visible glyph")
    return 0


if __name__ == "__main__":
    sys.exit(main())
