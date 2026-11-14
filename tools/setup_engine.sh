#!/usr/bin/env bash
# Copies the inference engine out of the Handy install into ./engine so
# V2T is self-contained and never needs Handy's folder at runtime.
#
# What is copied:
#   transcribe.dll        transcribe-cpp v0.4.0  (the C ABI we bind)
#   ggml*.dll             the ggml compute backends
#   msvcp140*.dll         MSVC runtime (transcribe.dll is an MSVC build)
#   vcruntime140*.dll     MSVC runtime
#
# What is deliberately NOT copied:
#   onnxruntime.dll       only needed for the ONNX Silero VAD build; the
#                         ggml Silero VAD (v6.2) is what we use.
set -euo pipefail

HANDY_DIR="${HANDY_DIR:-W:/TOOLS/portable app/Handy}"
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DEST="$HERE/engine"

if [ ! -d "$HANDY_DIR" ]; then
  echo "error: Handy install not found at '$HANDY_DIR'" >&2
  echo "       set HANDY_DIR=/path/to/Handy and re-run" >&2
  exit 1
fi

mkdir -p "$DEST"

copy() {
  local pattern="$1"
  local found=0
  for f in "$HANDY_DIR"/$pattern; do
    [ -e "$f" ] || continue
    cp -f "$f" "$DEST/"
    found=$((found + 1))
  done
  if [ "$found" -eq 0 ]; then
    echo "warning: no match for '$pattern' in $HANDY_DIR" >&2
  fi
}

copy "transcribe.dll"
copy "ggml*.dll"
copy "msvcp140*.dll"
copy "vcruntime140*.dll"

echo "engine payload -> $DEST"
ls -1 "$DEST"
echo "count: $(ls -1 "$DEST" | wc -l)"
