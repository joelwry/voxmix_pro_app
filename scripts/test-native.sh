#!/usr/bin/env bash
# Builds and runs the native engine test suite on the host (Linux / macOS).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cmake -S "$ROOT/native" -B "$ROOT/build/host" -G Ninja -DCMAKE_BUILD_TYPE=Release -DVOXMIX_BUILD_TESTS=ON
cmake --build "$ROOT/build/host"
"$ROOT/build/host/audio-engine/tests/voxmix_tests" "$@"
