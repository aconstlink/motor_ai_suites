# Initial Windows comparison: 2026-10-12

These are historical adapter measurements, not general engine ratings. The adapter
code and external-framework build integration have been removed; the results and
analysis are retained as a reference for profiling motor.
See [methodology and implementation differences](README.md#what-the-numbers-mean).

The motor numbers below are retained **12 October baselines**, not current
performance. The [22 October follow-up](../docs/D3D11_ASYNC_RENDERING.md#latest-alternating-backend-comparison)
measured about 19.36 ms (gl4) and 11.95 ms (d3d11) for 5000 direct unlit cubes,
versus 37.815/30.630 ms here. That is approximately 49%/61% lower frame time.
This is a historical comparison across revisions, drivers and measurement
sessions, not a controlled attribution to any single optimization. The external
framework rows have not been rerun as part of that follow-up.

A separate [24 October gl4 render-cache A/B test](../docs/D3D11_ASYNC_RENDERING.md#gl4-render-cache-comparison-2026-10-24)
measured **16.19 -> 7.27 ms** for 5000 direct cubes, with p95 **21.22 -> 9.97 ms**.
The same change showed no stable gain in the full village workload. These are
controlled motor-only comparisons, not replacements for the historical
framework rows below; the later update-path cache change was not measured.

## Setup

- Intel Core i7-6700, 4 cores / 8 logical processors; NVIDIA GeForce RTX 2060.
- NVIDIA driver 596.21 (Windows driver 32.0.15.9621).
- Windows, Visual Studio 2026 / MSVC 19.51, x64, optimized Release builds;
  ezEngine uses its optimized Shipping configuration. No debug graphics device.
- motor commit `cc4c402daf10b05404c1e158f15beb2449447330`;
  suites base commit `d68f55c942191b2816a68e8b36d895d54e014c6f` plus these adapters.
- Other source revisions are pinned in [dependencies.json](dependencies.json).
- 1280 x 720, unlit cubes, one indexed draw per cube, no instancing or scene graph.
- motor: existing `05_scene_benchmark --path direct --lights 0`.
- 120 warmup frames, 300 measured intervals, three fresh-process repetitions per case.
- Sequential runs, reversed order on repetition two. No builds running during measurement.

Historical command used for these measurements (the runner is no longer included):

```powershell
./comparison/run.ps1 -Engines motor,raylib,bgfx,cinder,ezengine -Backends gl,d3d `
    -Objects 100,1000,5000 -Warmup 120 -Frames 300 -Repeats 3 `
    -EzExecutable ./build/comparison-ezengine/bin/WinVs2026Shipping64/compare_ezengine.exe
```

## Results

**CPU frame-start interval in milliseconds; lower is better for this workload.**
Each cell is the median of three reported run medians, not a pooled sample median.
These are not GPU timestamps, presentation latency or measured display FPS.

| Adapter | Backend | 100 cubes | 1,000 cubes | 5,000 cubes |
|---|---|---:|---:|---:|
| motor | OpenGL | 1.937 | 9.707 | 37.815 |
| motor | D3D11 | 0.725 | 3.848 | 30.630 |
| raylib | OpenGL | 0.792 | 3.598 | 17.492 |
| bgfx | OpenGL | 0.421 | 0.825 | 2.369 |
| bgfx | D3D11 | 0.194 | 0.412 | 1.858 |
| Cinder | OpenGL | 0.417 | 0.782 | 2.518 |
| ezEngine | D3D11 | 0.207 | 0.516 | 1.919 |

There was visible run-to-run variation. At 5,000 cubes:

| Adapter / backend | Range of run medians (ms) | Median of run p95 values (ms) |
|---|---:|---:|
| motor / OpenGL | 36.985-45.459 | 51.978 |
| motor / D3D11 | 30.413-31.594 | 40.093 |
| raylib / OpenGL | 17.324-18.007 | 20.421 |
| bgfx / OpenGL | 1.815-2.375 | 3.260 |
| bgfx / D3D11 | 1.726-2.260 | 3.220 |
| Cinder / OpenGL | 2.352-2.586 | 4.310 |
| ezEngine / D3D11 | 1.856-1.945 | 3.479 |

All 63 run summaries, including diagnostic phase timings, are in
[2026-10-12.csv](results/2026-10-12.csv). Local raw logs, executable hashes and
environment details are under
`build/comparison-results/20261012-123258-124/` (intentionally not versioned).

## Validation

- All four external adapters built successfully; motor's reference was rebuilt too.
- The shared CPU workload test passed in all four external build configurations.
- All five optional graphical smoke CTests passed too: nine passing CTests in total.
- Every measurement exited with code 0 and reported all 300 requested samples.
- Separate 100-cube captures from raylib, Cinder, bgfx/OpenGL, bgfx/D3D11 and
  ezEngine/D3D11 were 1280 x 720, with 100 separated foreground shapes.
  Their first-frame RGB pixels matched exactly. Captures were not taken in timing runs.
- bgfx reported the expected actual draw count in every run. Other draw counts are
  expected from the submission loop, not independently verified GPU draw counts.
- motor reported the expected variable-set count and an empty memory-manager dump.
- motor's OpenGL runs logged the existing `wglMakeCurrent(00)` error during shutdown,
  after the result was printed. There were no benchmark timeouts or process crashes.

## Interpretation

motor's direct path at this historical revision was measurably slower here,
especially at high draw counts.
This is useful evidence for profiling its frame synchronization, backend processing
and variable handling; it does not identify which of them is the bottleneck.
The relatively small measured transform and frontend-recording times alone do not
explain the full frame interval.

The adapters do not have identical internal work: motor keeps a variable set per
object and runs its application services; bgfx records submissions for its render
thread; raylib uses DrawMesh; Cinder uses Batch; ezEngine uses RendererCore directly
without a world or editor scene. The external adapters also share simpler transform
evaluation. No attempt was made to tune each framework to its fastest possible path.

This baseline cannot predict a lit game's frame time, GPU-heavy scenes, streaming,
multiple windows or the development effort saved by higher-level motor features.
It records a baseline workload. Longer runs and targeted profiling should
precede architectural conclusions. No motor source was changed for this comparison.

## Draw-path analysis

Source inspection after the baseline identified the following differences. These
are **observed code paths, not measured explanations of the performance gap**.
References describe the revisions listed above; function names are included because
line numbers will change. No optimization or new timing run was performed for this
analysis.

### Synchronization around backend data access

In motor's [datas.hpp](../motor/platform/graphics/datas.hpp), `access` and
`try_access` protect the container with an `mrsw_t` reader lock and protect the
selected entry with a guard. `guard_data::enter`, `try_enter` and `leave` acquire
a mutex, including the same-thread recursive path. Rendering accesses several
containers, including msl objects, render objects, shaders and geometry.

This makes a backend lookup more than an indexed array access. Uncontended locks
still have a cost, but a mutex operation does not imply an OS sleep, and notifying
a condition variable does not prove there are waiting threads. Both call frequency
and actual contention need measurement. Do not remove these guards without first
establishing which threads can access or mutate the data.

### Repeated OpenGL state changes

In [gl4.cpp](../motor/platform/graphics/gl/gl4.cpp), the normal path calls
`glUseProgram` in `update_variables` and again in `render`. The draw path also
unbinds the VAO with `glBindVertexArray(0)` after drawing. The next draw binds it
again, even though this benchmark uses the same mesh and shader throughout.

In the pinned bgfx `src/renderer_gl.cpp`, the renderer tracks `currentProgram` and
changes the program when its handle changes. Its uniform state cache also avoids
uploading unchanged values. Thus, equal draw counts do not imply equal numbers of
state changes or uniform uploads. The adapter uses sequential submission order;
this comparison does not depend on bgfx sorting different materials together.

### OpenGL error queries in the release path

motor's `uniform_data::do_uniform_funk` checks for errors after uniform uploads,
and several binding operations do so as well. The implementation in
[error.hpp](../motor/ogl/gl/error.hpp), `check_and_log`, calls `glGetError` without
a release-build exclusion.

These are additional driver calls even when no error is logged. Their cost is
driver-dependent; they must not be described as an unconditional GPU finish.
A diagnostic-only A/B switch would let us measure their contribution without
changing resource management or rendering semantics.

### D3D11 variable storage and binding

motor keeps separate variable sets for the objects in this workload. In
[d3d11.cpp](../motor/platform/graphics/d3d/d3d11.cpp), `update_variables` copies
the selected set's values into constant-buffer memory and calls
`UpdateSubresource`. The draw path binds shader stages and constant buffers,
and unbinds pixel-shader constant buffers after the draw.

The pinned bgfx `src/renderer_d3d11.cpp` uses shader-associated constant buffers
and scratch data in `commitShaderConstants`, with program-change tracking around
shader and constant-buffer binding. It also uses `UpdateSubresource`: the
difference is not simply the upload API, but resource organization and repeated
work around it. Allocation, copying, binding and driver costs are not yet timed
separately.

### Frame handoff and overlap

Follow-up source inspection on 2026-10-12 also identified an unconditional
`glFlush(); glFinish();` in motor GL4's `end_frame`, before presentation. That
forced GPU-completion policy was present in this baseline and is absent from the
Windows/GLFW raylib draw loop. It is an additional confounder, not a measured
explanation of the whole gap. See the [native raylib synchronization follow-up](../suite_raylib/RESULTS.md#per-frame-gpu-synchronization-follow-up).
The archived timings and raw CSV remain unchanged. A later
[finish-on/off A/B experiment](../suite_raylib/GLFINISH.md) is recorded separately.

In motor's [render_engine.cpp](../motor/graphics/render_engine.cpp),
`execute_frame` processes and clears the command lists before changing the
`_has_commands` state checked by `can_enter_frame`. This restricts reuse of that
submission buffer until backend command execution finishes. It does not mean
that all application work is serialized or that the GPU has finished.

bgfx uses separate frame data for API-thread and render-thread processing; see
its [internals documentation](https://bkaradzic.github.io/bgfx/internals.html).
Different handoff and overlap can affect the measured frame interval. However,
Cinder's OpenGL result is close to bgfx here, so bgfx's render-thread architecture
alone cannot explain the gap.

### What this does not establish

- Scene Graph and Wire execution are not involved in motor's direct benchmark.
- bgfx reports one draw per cube; it is not secretly replacing the workload with instancing.
- motor's measured transform and frontend-recording phases are too small to explain
  the whole frame interval on their own. Backend execution and waits remain unprofiled.
- Existing variable-set hashes and geometry-link checks do not imply that every
  draw reconnects or reallocates resources. Unchanged resources follow their checks.
- `glUniform` alone is not an explanation: the inspected bgfx GL path also uses
  `glUniform` calls, with caching. UBOs are not a demonstrated solution to this gap.
- The adapters differ in transform evaluation and application services. These
  results are not an isolated microbenchmark of one backend function.

### Profiling checklist

All items below are pending, not fixes already made. Change one factor at a time
and keep the baseline available for comparison.

- [ ] Compare the GL release baseline with diagnostic `glGetError` queries disabled.
  Keep normal validation enabled outside the experiment and verify the same output.
- [ ] Count `datas` accesses and measure guard acquisition/wait time separately
  from work inside the callback. Check instrumentation overhead with a control run.
- [ ] Count repeated program, VAO and constant-buffer bindings. Test state caching
  separately, including correct invalidation across passes and reconfiguration.
- [ ] Time backend variable processing, draw submission and frame-handoff waits
  separately. Use GPU timings before attributing the remaining interval to GPU work.
- [ ] Repeat 100/1,000/5,000-object cases with the same build, driver, warmup and
  measurement settings; record run variation and recheck visual correctness.

These measurements should determine whether synchronization, variable handling,
driver-call overhead or frame scheduling deserves attention first. They do not
require a backend rewrite as a starting point.
