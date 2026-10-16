# motor / native raylib comparison: 2026-10-12

This is a direct, unlit draw-call workload comparison, not a general engine rating.
motor's Scene Graph, Wire connections, lights, shadows and postprocessing are not
part of this measurement. No motor engine source was changed for this original
series. The later [glFinish A/B experiment](GLFINISH.md) measures the engine change
separately; do not merge its runs with the baseline below.

## Conditions

- Intel Core i7-6700 (4 cores / 8 threads), NVIDIA GeForce RTX 2060.
- Windows 11 Pro 26100, driver 32.0.15.9621, Visual Studio 2026, x64 Release.
- motor `cc4c402daf10b05404c1e158f15beb2449447330`;
  suites base `d68f55c942191b2816a68e8b36d895d54e014c6f` plus the native raylib test.
- Raylib source archive pinned to `fdc8b37391b52fdc8cc62af37d02235c9ec59daa`.
- One window, verified 1280x720 client area and 144 DPI in every run.
- Matching cube grid, colors, orthographic camera, scale and fixed-step animation.
  Both applications use motor's transform math. Shared mesh: 24 vertices,
  36 indices per cube, one indexed draw per object; no instancing.
- Depth test/write enabled, LEQUAL; no blending or backface culling.
  VSync and application frame limiting requested off. No MSAA requested.
- 180 warmup frames, 600 measured intervals, three fresh-process repeats per case.
  The three variants rotate execution order between repeats; all runs are sequential.
- No concurrent build. Normal desktop/browser applications remained active.
  Background CPU activity was observed: this is not an isolated idle-machine baseline.

The initial checks exposed two confounders: motor and raylib interpreted decorated
window dimensions differently, and motor was DPI-unaware while raylib was DPI-aware.
Both Visual Studio benchmark targets now use a PerMonitor manifest. The runner
normalizes client dimensions at startup and verifies size and matching DPI.
Earlier interrupted diagnostic series are **not** included below.

**Synchronization follow-up:** motor's measured GL4 path calls `glFinish()` on
every frame; the Windows/GLFW raylib path does not. The results below include that
policy difference. They must not be presented as equal GPU-completion policies or
as the performance motor would achieve without the forced wait. See the follow-up
section below and the separate [finish-on/off measurements](GLFINISH.md).

## Results

**Milliseconds per CPU frame-start interval; lower is better.** Each cell is the
median of three run medians, not a pooled frame median or a GPU timestamp.
It is not measured display FPS or presentation latency.

| Objects / draws per frame | motor GL4, three matrices | Raylib, three matrices | Raylib, combined MVP |
|---|---:|---:|---:|
| 100 | 2.174 | 0.713 | 0.806 |
| 1,000 | 10.315 | 3.942 | 4.150 |
| 5,000 | 46.840 | 22.599 | 20.586 |

Against the closer three-matrix raylib variant, motor's interval is approximately
**3.05x / 2.62x / 2.07x** at these object counts. All three motor run medians were
higher than all three raylib run medians at the corresponding count, in both variants.

There is meaningful run-to-run variation:

| Variant | 100: min-max run median | 1,000: min-max run median | 5,000: min-max run median |
|---|---:|---:|---:|
| motor GL4 | 2.002-2.590 | 10.162-10.317 | 45.153-59.711 |
| Raylib, three matrices | 0.704-0.734 | 3.889-5.814 | 21.049-22.603 |
| Raylib, combined MVP | 0.737-0.857 | 3.958-4.372 | 19.359-31.455 |

The three-matrix raylib path remains faster than motor here. Thus the original
MVP-versus-three-matrices difference does not explain the overall gap. The raylib
variants' ranges overlap; this run does not isolate a reliable cost for the extra
matrix uniforms. Repeat under quieter conditions before making small-effect claims.

## Diagnostic timings

motor's measured transform/update medians were **0.019 / 0.209 / 1.122 ms** for
100 / 1,000 / 5,000 objects. Its frontend command-recording medians were
**0.012 / 0.076 / 0.494 ms**. The transform evaluation alone therefore does not
account for the whole frame interval.

Do not compare motor's command-recording time directly with raylib's submission
column: raylib includes graphics API execution and EndDrawing/presentation there;
motor performs backend work on another thread. Nor should medians be subtracted
to claim an exact backend time. GPU timestamps and backend phase instrumentation
are still missing.

## Remaining implementation differences

- motor uses its GL4 backend; raylib is built for OpenGL 3.3.
- motor keeps a variable set per object, cached variable pointers and frontend
  commands. Raylib calls SetShaderValue and DrawMesh directly for each object.
- The closer raylib mode uses matModel, matView and matProjection with the same
  shader multiplication order. DrawMesh still performs its internal matrix work,
  even when its combined MVP uniform is unused.
- motor's cube uses interleaved position/normal data and 32-bit indices. Raylib's
  generated cube uses separate attribute buffers, unused UV data and 16-bit indices.
  This is equal visible work and topology counts, not byte-identical GPU resources.
- motor starts its normal application services and render thread; the raylib test
  uses motor math/containers/logging but not those application services.

The increasing absolute gap with object count warrants profiling per-draw work,
not a conclusion that the Scene Graph or lighting architecture is slow. Useful
next measurements are GL error-query/uniform/state-call counts and costs, backend
data-access guards, and frame-handoff/wait time. The source observations in the
[archived analysis](../comparison/RESULTS.md#draw-path-analysis) are hypotheses to
measure, not proven attribution of this gap. No locks or checks were disabled here.

## Per-frame GPU synchronization follow-up

Source inspection after the original measurement confirmed this sequence in
`motor/platform/graphics/gl/gl4.cpp`, `gl4_backend::pimpl::end_frame`:

```cpp
glFlush() ;
glFinish() ;
_state_stack.pop() ;
_ctsd->notify_thread() ;
```

The Windows carrier calls `render_end()` before the context's buffer swap.
`glFinish` blocks the calling render thread until prior commands in that GL
context have completed. This limits CPU/GPU overlap across frames. `glFlush`
requests progress without waiting for completion; it is not an equivalent wait.
See [Khronos synchronization](https://wikis.khronos.org/opengl/Synchronization).

In the pinned raylib source, the Windows/GLFW `SwapScreenBuffer` uses
`glfwSwapBuffers` without an explicit per-frame `glFinish`. Driver/presentation
backpressure can still block it. A finish call in raylib's DRM implementation is
not part of this Windows benchmark. motor's ES3 backend has the same flush/finish
pair, but was not benchmarked here.

The subsequent [controlled A/B experiment](GLFINISH.md) retains `glFlush` and
removes only the frame-end `glFinish`. It runs both saved finish-on and rebuilt
finish-off binaries in one series, alongside raylib, leaving this original data
unchanged. GPU timestamp queries remain necessary for separating GPU execution
time from application frame cadence.

For normal rendering, an unconditional GPU drain every frame should not be the
default synchronization policy. However, removal needs lifecycle regression tests:
dynamic reconfigure/release, shader reload via the support thread's shared context,
transform feedback, render-to-texture, and window close/reopen. Cases that truly
need completion should use explicit, scoped GPU synchronization, for example
[GL sync objects](https://wikis.khronos.org/opengl/Sync_Object), rather than depend
on an incidental end-of-frame wait.

CPU ownership is a separate contract. `render_engine::execute_frame` releases the
command-list handoff before `render_end`; `frontend::fence` invokes a CPU callback
from the command list (also on cancellation). Neither is a GL GPU fence, even with
the former end-of-frame glFinish. Shared-context visibility/order must also be
handled at the actual producer/consumer boundary; one context's finish is not a
general synchronization protocol for all contexts.

The GL4 call has now been removed; ES3 remains unchanged. Results, regression
coverage and remaining validation limits are recorded in the [A/B report](GLFINISH.md).

## Validation and reproduction

- 27/27 measured runs exited with code 0 and all 600 samples.
- All nine motor runs reported the expected variable-set count and an empty memory dump.
- The known `wglMakeCurrent(00)` shutdown diagnostic appeared in all nine motor runs,
  after reporting timings. No crash or benchmark timeout occurred.
- Both raylib shader variants passed graphical runs; their separately captured
  first-frame 1280x720 images matched exactly. Captures were not part of timed runs.
- 3/3 raylib CLI CTests passed. The optional graphical CTests were disabled in this
  build; graphics validation above used direct launches and the comparison runs.
- Draw counts come from the application loops, not an independent GPU capture.

```powershell
./suite_raylib/run_comparison.ps1 -BuildDirectory E:/repos_aconstlink/_build/motor_ai_suites.x64.vs2026 -Objects 100,1000,5000 -Warmup 180 -Frames 600 -Repeats 3
```

Saved data: [27 run summaries](results/2026-10-12.csv),
[aggregates](results/2026-10-12-summary.csv),
[environment and source/executable hashes](results/2026-10-12-environment.json).
CSV numeric strings retain the measuring host's `de-DE` decimal comma; fields are quoted.
Unabridged stdout/stderr logs remain locally under
`E:/repos_aconstlink/_build/motor_ai_suites.x64.vs2026/raylib-comparison/20261012-214429-763/`.
The older multi-framework result files remain unchanged and must not be mixed into
this series: adapters, window/DPI normalization and measurement conditions differ.
