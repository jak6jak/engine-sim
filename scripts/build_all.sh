#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ENGINE_CORE_DIR="$ROOT_DIR/addons/engine_sim/engine-core"
BUILD_DIR="${ENGINE_SIM_BUILD_DIR:-"$ENGINE_CORE_DIR/build"}"
GDEXT_DIR="$ROOT_DIR/addons/engine_sim"

if [[ ! -f "$BUILD_DIR/CMakeCache.txt" ]]; then
  mkdir -p "$BUILD_DIR"
  cmake -S "$ENGINE_CORE_DIR" -B "$BUILD_DIR"
fi

cmake --build "$BUILD_DIR"

scons -C "$GDEXT_DIR" platform=macos arch=arm64 target=template_debug
