# Native rendering references

Three optional references beside [raylib](../suite_raylib/README.md). Each is a
normal executable in the motor AI Suites solution, with a directly linked external
library where applicable. No child CMake/MSBuild invocations, launcher copies or
automatic full benchmark runs. motor's source and pinned submodule are unchanged.

| Option (default OFF) | Target | Rendering |
| --- | --- | --- |
| `MOTOR_AI_BUILD_BGFX` | `00_bgfx_benchmark` | bgfx OpenGL or D3D11; normal threaded renderer |
| `MOTOR_AI_BUILD_CINDER` | `00_cinder_benchmark` | Cinder OpenGL, VboMesh/Batch |
| `MOTOR_AI_BUILD_OPENGL` | `00_opengl_benchmark` | Direct OpenGL 3.3 calls; GLFW only creates the window/context |

## Build

Enable only the references needed in CMake GUI, or use the existing build:

```powershell
cmake -S . -B build -DMOTOR_AI_BUILD_BGFX=ON -DMOTOR_AI_BUILD_CINDER=ON -DMOTOR_AI_BUILD_OPENGL=ON
cmake --build build --config Release --target 00_bgfx_benchmark 00_cinder_benchmark 00_opengl_benchmark --parallel 2
```

bgfx requires CMake 3.21 or newer. Initial external-library compilation still
takes time, particularly Cinder; later builds are incremental. Examples, upstream
tests and bgfx tools are disabled. Cinder audio, video and ImGui are disabled.
The wrapper removes Cinder's unrestricted `/MP`; build concurrency is controlled
by the build command. External targets are excluded from ALL unless a selected
benchmark needs them. The benchmark targets themselves build with the solution
when their option is enabled.

In Visual Studio, choose the desired target under `suites/suite_bgfx`,
`suites/suite_cinder` or `suites/suite_opengl` as Startup Project. Libraries appear
under `externals`. F5 uses the selected configuration and the normal
`bin/<configuration>` directory. Use Release without the debugger for timings.

## Dependencies

FetchContent downloads sources only when the corresponding option is enabled:

- bgfx.cmake `a046ef085427ff7dd5a499d7f48e06f5d1bb049d`, bgfx
  `8f451e9102b1845b29dfe500116611ad283632ec`, bx
  `36b2b6943f7c62276d96ff7a34d59db8057a5d24`, bimg
  `671fe06dd7012a1ab85aa931019ed16112909756`.
- Cinder `f90f332d1a4d453282c76435f3401271f25922f9`.
- GLFW 3.4 for bgfx/direct GL when raylib has not already provided target `glfw`.
  These executables do not link raylib itself.

Archive URLs pin revisions but are not separately checksum-verified. Existing
sources can be supplied through `MOTOR_AI_BGFX_SOURCE_DIR` (bgfx.cmake including
its bx/bimg/bgfx subdirectories), `MOTOR_AI_CINDER_SOURCE_DIR` and
`MOTOR_AI_GLFW_SOURCE_DIR`. Overrides are not revision-checked. Record any changes
and use the listed revisions when comparing results.

The four small bgfx shader binaries are versioned with their sources in
`suite_bgfx/shaders`. They were generated for the pinned bgfx revision, GLSL 330
and HLSL shader model 5. A normal build does **not** build shaderc, Tint, glslang,
SPIR-V tools, geometry tools or texture tools. After shader or bgfx format changes,
regenerate with a compatible Windows shaderc:

```powershell
./suite_bgfx/shaders/regenerate.ps1 -Shaderc path/to/shaderc.exe -BgfxSource path/to/bgfx
```

Runtime shaders are copied next to the executable in `bgfx_shaders`. No hardcoded
developer source path is needed to launch from Visual Studio. Changing a shader
binary requires building the executable target to run its small shader-copy target.
Linux uses the GLSL binary and X11; system OpenGL/X11 development dependencies
are required. Platform coverage is recorded in [validation](VALIDATION.md).

## Run

```powershell
./build/bin/Release/00_opengl_benchmark.exe --objects 1000 --warmup 120 --frames 300
./build/bin/Release/00_cinder_benchmark.exe --objects 1000 --warmup 120 --frames 300
./build/bin/Release/00_bgfx_benchmark.exe --backend gl --objects 1000 --warmup 120 --frames 300
./build/bin/Release/00_bgfx_benchmark.exe --backend d3d --objects 1000 --warmup 120 --frames 300
```

All close automatically. `--help` does not open a window. Counts are bounded:
1..5000 objects, 2..10000 warmup frames, 10..10000 measured intervals. Early close,
setup failure or a 300-second watchdog yields a nonzero exit, not partial results.
`--capture path.ppm` reads back the first frame and checks it is nonblank; keep
capture runs separate from measurements. PPM is a simple RGB image format.

For a repeated series of the new references without builds or downloads:

```powershell
./suite_reference/run_benchmarks.ps1 -BuildDirectory ./build -Objects 1000 -Repeats 3
```

Keep the graphics windows visible and do not minimize them. Cinder can stop
drawing hidden windows. The runner enforces a separate 320-second process timeout
and keeps per-run logs, executable hashes and CSV records under the build tree.

## What is compared

The common helper uses motor math, containers and logging, **not** motor's
application, renderer, Scene Graph or Wire. Transforms follow the native raylib
reference: fixed-step Y rotation, X rotation 0.3, the same grid/scale/colors and
orthographic camera at (0,0,30), width 24, height 13.5, near/far 1/100.

- 1280x720 framebuffer, black clear, one shared indexed cube: 24 position/normal
  vertices and 36 32-bit indices. Normals are unused by the unlit shader.
- One indexed draw per cube. No instancing, multi-draw, lighting or postprocessing.
- Three-matrix vertex shader, depth test/write LEQUAL; culling, blending, MSAA
  and sRGB framebuffer conversion disabled. VSync and application frame caps off.
- OpenGL uploads world/view/projection/color per draw with cached locations and
  reuses the same bound program/VAO. No GL error query in the measured draw loop,
  no `glFinish`; setup and end-of-run error checks remain.
- Cinder uses cached uniform locations and calls `Batch::draw()` per cube.
  A Batch is a mesh/shader binding, **not** cross-object draw batching.
- bgfx sets the camera per view, world/color per submission, and uses its normal
  render thread. Sequential view mode preserves submission order. Its reported
  actual draw count must match the requested object count after warmup.

`REFERENCE_BENCH` records CPU frame-start intervals and update/submission phase
times. Medians average the two middle samples for even counts; p95 is nearest
rank. These are **not GPU timestamps or a universal "ground truth"**. Direct GL
is a small reference path for this workload, not a lower bound for every renderer.
Cinder presents after `draw()`, bgfx hands off via `frame()`, direct GL swaps inside
the measured submission phase. Phase times are therefore not interchangeable.

For motor use `05_scene_benchmark --backend gl --path direct --lights 0` with the
same counts. For raylib prefer `--matrices separate`. Raylib uses 16-bit indices
and its own mesh/matrix submission machinery. motor's window client size needs
checking (its requested size can include borders); the existing raylib comparison
runner performs that adjustment. The new references reject mismatched framebuffer
sizes. Do not combine their new numbers with the [historical table](../comparison/RESULTS.md).

## Tests

With `BUILD_TESTING`, CLI checks and `reference_workload` are CPU-only CTests.
Enable `MOTOR_AI_REFERENCE_GRAPHICS_TESTS=ON` to register short window tests:

```powershell
cmake --build build --config Release --target 00_reference_workload_test --parallel 2
ctest --test-dir build -C Release -L reference --output-on-failure
```

Graphics tests run serially, require a display/driver and have no speed threshold.
