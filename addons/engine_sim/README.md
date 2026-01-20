# EngineSim GDExtension demo (minimal)

This is a **minimal** Godot 4 GDExtension that calls the `engine-sim` runtime **C API** (`es_runtime_*`).

It links against the static libraries produced by this repo’s CMake build (not delta-studio).

## 1) Prereqs

- Godot 4.x (tested intent: 4.2+)
- Python + SCons (`brew install scons`)
- Boost via Homebrew (`brew install boost`)

## 2) Build engine-sim runtime (CMake)

From the repo root (recommended: Release + arm64 for profiling):

- `cd addons/engine_sim/engine-core`
- `cmake --preset macos-arm64-release`
- `cmake --build --preset macos-arm64-release -j 8`

Or without presets:

- `cmake -S addons/engine_sim/engine-core -B addons/engine_sim/engine-core/build/macos-arm64-release -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=arm64`
- `cmake --build addons/engine_sim/engine-core/build/macos-arm64-release -j 8`

You should have:
- `build/libengine-sim-runtime.a`

For profiling inside the Godot editor with macOS Instruments signpost markers, use the signpost preset:

- `cd addons/engine_sim/engine-core`
- `cmake --preset macos-arm64-relwithdebinfo-signpost`
- `cmake --build --preset macos-arm64-relwithdebinfo-signpost -j 8`

## 3) Get and build godot-cpp

From `addons/engine_sim`:

- `git clone https://github.com/godotengine/godot-cpp.git`
- `cd godot-cpp && git submodule update --init --recursive`

Build godot-cpp (macOS arm64):

- `scons platform=macos arch=arm64 target=template_debug`

(For release builds, use `target=template_release`.)

## 4) Build the extension

From `addons/engine_sim`:

- Debug: `scons platform=macos arch=arm64 target=template_debug engine_sim_build_dir=engine-core/build/macos-arm64-release`
- Release: `scons platform=macos arch=arm64 target=template_release engine_sim_build_dir=engine-core/build/macos-arm64-release`

If you're running the game inside the Godot editor, Godot will typically load the **debug** library variant.
For profiling inside the editor, build an optimized debug variant:

- Editor profiling: `scons platform=macos arch=arm64 target=template_debug optimize=yes engine_sim_build_dir=engine-core/build/macos-arm64-release`

If you built engine-core with the signpost preset, point the extension at that build dir instead:

- Editor profiling (signposts): `scons platform=macos arch=arm64 target=template_debug optimize=yes engine_sim_build_dir=engine-core/build/macos-arm64-relwithdebinfo-signpost`

For lightweight step timing prints during profiling, add:

- `perf=yes`

Note: `perf=yes` is a GDExtension build flag; engine-core timing/signposts are controlled via CMake options/presets.

### Recording with `xctrace` (optional)

If you want to record from the command line, prefer the `os_signpost` instrument (it reliably captures the `engine-sim` signposts):

- `xcrun xctrace record --template "Time Profiler" --instrument os_signpost --time-limit 20s --launch -- /path/to/godot.macos.editor.arm64 --path "$PWD" --scene "res://scenes/demo.tscn"`

Note: `--quit-after` counts iterations/frames, not seconds.

## 5) Run

Open `godot-demo/` as a project in Godot and run the main scene.

Notes:
- The demo script defaults to loading `res://../assets/main.mr` (this repo’s script). If Godot can’t resolve that path on your machine, point it at an absolute path.
- Audio is produced as mono and duplicated into stereo for `AudioStreamGenerator`.
