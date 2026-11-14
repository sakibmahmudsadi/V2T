#!/usr/bin/env bash
# Builds a portable release folder: dist/ParakeetType/
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$HERE"
export PATH="/c/mingw64/bin:$PATH"

cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release > /dev/null
cmake --build build > /dev/null

DIST="$HERE/dist/ParakeetType"
rm -rf "$DIST"
mkdir -p "$DIST/engine"

cp build/ParakeetType.exe "$DIST/"
cp build/ParakeetTypeWorker.exe "$DIST/"
cp engine/*.dll "$DIST/engine/"
cp config.json "$DIST/"
cp README.md "$DIST/"

echo "release -> $DIST"
ls -la "$DIST"
du -sh "$DIST"
