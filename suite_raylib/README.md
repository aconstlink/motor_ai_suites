# Native raylib benchmark

One ordinary executable target, `00_raylib_benchmark`, linked directly against the
raylib external in this solution. There are no nested builds, copied launchers or
other comparison engines. See [results](RESULTS.md) for the original baseline and
the subsequent motor GL4 frame-end synchronization change.

## Build and run

Enable `MOTOR_AI_BUILD_RAYLIB` in CMake GUI, or configure your existing build:

```powershell
cmake -S . -B build -DMOTOR_AI_BUILD_RAYLIB=ON
cmake --build build --config Release --target 00_raylib_benchmark --parallel 2
```

CMake 3.22 or newer is required when this option is enabled. It defaults to OFF.
FetchContent downloads a checksum-verified archive of raylib commit
`fdc8b37391b52fdc8cc62af37d02235c9ec59daa` once, without Git history or submodules.
An existing source tree can be supplied using `MOTOR_AI_RAYLIB_SOURCE_DIR` instead.
That override is deliberately not revision-checked; use the pinned revision for comparisons.

In Visual Studio, libraries appear under **externals/raylib** and the application
under **suites/suite_raylib**. Select `00_raylib_benchmark` as Startup Project and
use F5 or Ctrl+F5. It follows the selected Debug/Release configuration, just like
the motor samples, and writes to the same `bin/<configuration>` directory.
Use Release without the debugger for timings. When enabled, the application builds
normally with the solution; raylib's own examples and audio module are disabled.
No extra `/MP` flag or child MSBuild process is started by this integration.

```powershell
./build/bin/Release/00_raylib_benchmark.exe --objects 1000 --warmup 120 --frames 300
./build/bin/Release/05_scene_benchmark.exe --backend gl --path direct --lights 0 --objects 1000 --warmup 120 --frames 300
```

Both runs close automatically after measurement. Object counts are limited to
1..5000. Early window closure or the 300-second watchdog produces a nonzero exit
code rather than a partial timing result. `--help` prints the supported arguments.
`--capture <path.png>` captures the first rendered frame during warmup; use this
only for visual validation, not published timing runs.

`--matrices combined` (default) uploads a precomputed MVP matrix through DrawMesh.
`--matrices separate` uses raylib's standard matModel/matView/matProjection uniforms,
matching motor's three-matrix shader expression more closely. Both modes use the
same DrawMesh API; raylib still performs its internal matrix preparation.

## Workload and measurements

The grid, colors, object scale, fixed-step rotation and orthographic camera match
motor's direct/unlit benchmark: 1280x720, camera at (0,0,30), width 24, height 13.5,
near/far 1/100. One shared indexed cube (24 vertices, 36 indices), one unlit shader,
one DrawMesh call per object. No instancing, lights, postprocessing, MSAA, culling
or blending. VSync and frame limiting are requested off.

This version uses motor's math for the same transform evaluation, motor containers,
and `motor::log` for diagnostics and results. It does not start motor's application,
render backend, Scene Graph or Wire system. Shader/mesh creation and application-side
container allocations are outside the measured loop. Raylib's per-object color uniform upload and
DrawMesh path remain part of the submission measurement.

The `RAYLIB_BENCH` log record contains backend, object count, sample count, expected
draw count per frame, frame median/p95 and update/submission medians in milliseconds,
followed by the matrix mode.
Frame intervals include presentation/backpressure, not GPU timestamps. Update and
submission phases are not directly equivalent to motor's asynchronous phases.
The median averages the two middle samples for even counts, like motor.

The [archived results](../comparison/RESULTS.md) remain unchanged. This new native
test uses OpenGL 3.3 and motor's transform evaluation, unlike the archived standalone
raylib adapter (OpenGL 4.3 and a shared direct matrix formula). Record new results
separately; do not splice them into the old CSV. Repeat runs under the same conditions.

## Repeated motor comparison

See the [measured comparison and limitations](RESULTS.md) for the current baseline.

Build both `05_scene_benchmark` and `00_raylib_benchmark` in Release, then run:

```powershell
./suite_raylib/run_comparison.ps1 -BuildDirectory ./build
```

This runs motor's direct OpenGL path and both raylib matrix modes sequentially,
rotating their order across three repetitions: 100/1000/5000 cubes, 180 warmup
frames and 600 measured intervals. At startup, the Windows runner adjusts the outer
window size if necessary and checks each actual client area is 1280x720. This avoids
the difference between motor's outer-window dimensions and raylib's client dimensions.
Both Visual Studio benchmark targets use a PerMonitor DPI manifest; the runner also
rejects mismatched window DPI. Other motor applications are unchanged.
The runner performs no builds or downloads. Do not run
builds or other graphics workloads concurrently; keep the windows visible and
do not interact with them during measurement.

Raw logs, per-run CSV, aggregate CSV and machine/executable metadata are saved in
the build directory under `raylib-comparison/<timestamp>`. Aggregates are medians
of run medians, not pooled frame samples. motor's submission phase records commands;
raylib's includes execution of API calls and presentation. Do not compare these
phase columns as equivalent backend timings.

For a frame-end `glFinish` A/B run, first save the finish-on Release executable
`05_scene_benchmark.exe` and all DLLs from `bin/Release` in a separate directory.
Save the corresponding `gl4.cpp` there as `gl4-finish-on.cpp`. After rebuilding
motor without the frame-end finish, add the saved directory:

```powershell
./suite_raylib/run_comparison.ps1 -BuildDirectory ./build -MotorFinishOnDirectory ./saved-finish-on
```

This adds a fourth variant, `motor-gl-finish-on`; `motor-gl` still refers to the
current build. The runner does not toggle or rebuild engine code. Keep the other
sources, configuration and dependencies identical between snapshots. Metadata
records both platform DLL hashes and GL4 source snapshots as well as executables:
the changed backend resides in `motor_platform.dll`, not in the benchmark EXE.

## CTest

CLI help and invalid-count checks are registered with `BUILD_TESTING`.
Window tests require a separate opt-in:

```powershell
cmake -S . -B build -DMOTOR_AI_BUILD_RAYLIB=ON -DMOTOR_AI_RAYLIB_GRAPHICS_TESTS=ON
cmake --build build --config Release --target 00_raylib_benchmark --parallel 2
ctest --test-dir build -C Release -L raylib --output-on-failure
```

These are functional checks, not performance thresholds. The graphics test needs
a working display and OpenGL driver; it is disabled by default for headless CI.
