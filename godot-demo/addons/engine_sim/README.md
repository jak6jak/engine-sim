# EngineSim GDExtension demo (minimal)

This is a **minimal** Godot 4 GDExtension that calls the `engine-sim` runtime **C API** (`es_runtime_*`).

It links against the static libraries produced by this repo’s CMake build (not delta-studio).

## 1) Prereqs

- Godot 4.x (tested intent: 4.2+)
- Python + SCons (`brew install scons`)
- Boost via Homebrew (`brew install boost`)

## 2) Build engine-sim runtime (CMake)

From the repo root:

- `cmake -S . -B build`
- `cmake --build build --target engine-sim-runtime -j 8`

You should have:
- `build/libengine-sim-runtime.a`

## 3) Get and build godot-cpp

From `godot-demo/addons/engine_sim`:

- `git clone https://github.com/godotengine/godot-cpp.git`
- `cd godot-cpp && git submodule update --init --recursive`

Build godot-cpp (macOS arm64):

- `scons platform=macos arch=arm64 target=template_debug`

(For release builds, use `target=template_release`.)

## 4) Build the extension

From `godot-demo/addons/engine_sim`:

- Debug: `scons platform=macos arch=arm64 target=template_debug`
- Release: `scons platform=macos arch=arm64 target=template_release`

Or use the helper script:

- `chmod +x ./build_macos_arm64.sh`
- Debug: `./build_macos_arm64.sh template_debug`
- Release: `./build_macos_arm64.sh template_release`

If your engine-sim build dir is not `../../../build`, set:

- `ENGINE_SIM_BUILD_DIR=/absolute/path/to/engine-sim/build`

## 5) Run

Open `godot-demo/` as a project in Godot and run the main scene.

Notes:
- The demo script defaults to loading `res://../assets/main.mr` (this repo’s script). If Godot can’t resolve that path on your machine, point it at an absolute path.
- Audio is produced as mono and duplicated into stereo for `AudioStreamGenerator`.
