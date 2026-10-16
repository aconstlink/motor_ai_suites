# Native reference validation, 2026-10-15

## Tested

Windows x64, Visual Studio 2026 / MSVC, Release, NVIDIA GeForce RTX 2060.
The direct OpenGL context reported NVIDIA driver 610.88. The documented pinned
bgfx/Cinder checkouts were supplied through source overrides; the already enabled
raylib supplied GLFW. No motor source changes were needed.

- `00_opengl_benchmark`, `00_bgfx_benchmark`, `00_cinder_benchmark` and
  `00_reference_workload_test` built successfully as native solution targets.
- All 11 `reference` CTests passed: workload math/statistics, CLI help/invalid
  inputs, GL smoke tests for all three adapters and bgfx/D3D11 smoke.
- Separate 100-object captures: direct OpenGL, bgfx/GL, bgfx/D3D11, Cinder and
  the existing raylib three-matrix test. All five first-frame RGB images were
  pixel-identical at 1280x720, with 256,885 nonblack pixels. The image was also
  inspected visually. Captures were not used as timing runs.
- The runner completed all four new render paths with 5,000 cubes, 2 warmup
  frames and 10 measured intervals. Each process exited with code 0 and returned
  the requested sample count. bgfx additionally checked its actual draw count.
- Cinder must be launched visibly: its hidden-window startup did not draw frames.
  The runner now starts visible graphics windows and retains an external timeout.

These were functional checks, **not a new performance comparison**. Short warmup,
short sample windows and concurrent compilation during some earlier smoke runs
make those timings unsuitable for ranking engines. Use repeated full runs with
all builds stopped before publishing performance results.

## Not tested

- Fresh network population of every dependency archive; this build reused local
  sources. The standalone GLFW fallback still needs a clean-build validation.
- Linux, Debug and RelWithDebInfo builds/runs.
- GPU timestamp timings, input latency or GPU-capture draw counting for Cinder,
  raylib and direct GL.
- Cross-engine lighting, scene graphs, streaming, runtime shader recompilation,
  multiple windows or resource-sharing semantics.

Historical measurements remain in `comparison/` and are not replaced or extended
by these smoke checks.
