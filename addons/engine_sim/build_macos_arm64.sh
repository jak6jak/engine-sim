#!/usr/bin/env bash
set -euo pipefail

# Builds the GDExtension on macOS arm64.
# Assumes:
# - engine-sim CMake build is at ../../../build
# - godot-cpp is cloned at ./godot-cpp and already built

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

# Release by default (better for profiling / performance testing)
TARGET="${1:-template_release}"

ENGINE_SIM_BUILD_DIR="${ENGINE_SIM_BUILD_DIR:-$SCRIPT_DIR/../../../build}"
export ENGINE_SIM_BUILD_DIR

cd "$SCRIPT_DIR"

GODOT_CPP_PATH="${GODOT_CPP_PATH:-$SCRIPT_DIR/godot-cpp}"
if [[ ! -d "$GODOT_CPP_PATH" ]] && [[ -d "$SCRIPT_DIR/../../../godot-cpp" ]]; then
	GODOT_CPP_PATH="$SCRIPT_DIR/../../../godot-cpp"
fi
export GODOT_CPP_PATH

if [[ ! -d "$GODOT_CPP_PATH" ]]; then
	echo "ERROR: Missing godot-cpp at: $GODOT_CPP_PATH" >&2
	echo "Set GODOT_CPP_PATH or clone it next to this script." >&2
	echo "Clone example:" >&2
	echo "  git clone https://github.com/godotengine/godot-cpp.git" >&2
	echo "  cd godot-cpp && git submodule update --init --recursive" >&2
	exit 1
fi

# Build godot-cpp if its static library isn't present yet.
if ! ls "$GODOT_CPP_PATH"/bin/libgodot-cpp*.a >/dev/null 2>&1; then
	echo "Building godot-cpp..." >&2
	scons -C "$GODOT_CPP_PATH" platform=macos arch=arm64 target="$TARGET"
fi

scons platform=macos arch=arm64 target="$TARGET"

echo "Built to addons/engine_sim/bin/"
