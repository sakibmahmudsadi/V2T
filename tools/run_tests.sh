#!/usr/bin/env bash
# Runs the whole test suite. Every step prints what it checked.
set -uo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$HERE"

export PATH="/c/mingw64/bin:$PATH"

MODEL="${MODEL:-C:/Users/SADI/.cache/huggingface/hub/models--handy-computer--parakeet-unified-en-0.6b-gguf/snapshots/7e948f21b7bdbac698d3318db9d350f1096f3b6c/parakeet-unified-en-0.6b-Q8_0.gguf}"

failures=0
step() {
  local name="$1"; shift
  echo
  echo "=============================================================="
  echo "  $name"
  echo "=============================================================="
  if "$@"; then
    echo "--> PASS: $name"
  else
    echo "--> FAIL: $name"
    failures=$((failures + 1))
  fi
}

echo "building..."
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release > /dev/null 2>&1
cmake --build build > /dev/null 2>&1 || { echo "build failed"; exit 1; }
echo "build ok"

step "engine self-test" ./build/V2TWorker.exe --selftest
step "golden corpus (vs Handy's transcripts)" python tools/corpus_check.py
step "edge cases" python tools/edge_cases.py
step "controller <-> worker protocol" python tools/ipc_test.py
step "streaming does not hang on silence" python tools/silence_test.py
step "controller hotkey" python tools/controller_test.py
step "the model is released when idle" python tools/idle_unload_test.py --idle-seconds 5
step "offline audit (no networking DLL)" python tools/audit.py --model "$MODEL" \
  --file "C:/Users/SADI/AppData/Roaming/com.pais.handy/recordings/handy-1794634700.wav"

echo
echo "=============================================================="
if [ "$failures" -eq 0 ]; then
  echo "  ALL TESTS PASSED"
  echo "=============================================================="
  exit 0
fi
echo "  $failures TEST(S) FAILED"
echo "=============================================================="
exit 1
