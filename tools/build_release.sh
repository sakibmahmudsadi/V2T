#!/usr/bin/env bash
# Builds a portable release folder: dist/V2T/
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$HERE"
export PATH="/c/mingw64/bin:$PATH"

cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release > /dev/null
cmake --build build > /dev/null

DIST="$HERE/dist/V2T"
rm -rf "$DIST"
mkdir -p "$DIST/engine"

cp build/V2T.exe "$DIST/"
cp build/V2TWorker.exe "$DIST/"
cp engine/*.dll "$DIST/engine/"
cp config.json "$DIST/"
cp README.md "$DIST/"

echo "release -> $DIST"
ls -la "$DIST"
du -sh "$DIST"
