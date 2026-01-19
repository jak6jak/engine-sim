#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
GDEXT_DIR="$ROOT_DIR/godot-demo/addons/engine_sim"

scons -C "$GDEXT_DIR" platform=macos arch=arm64 target=template_debug
