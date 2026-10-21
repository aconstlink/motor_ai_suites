# Render Access and Runtime Shader Compilation

This is a bounded measurement, not a lock-removal patch or a renderer rewrite.
No motor source was changed for these tests.

## Tests

- [`08_render_access_cost`](../suite_concurrent/08_render_access_cost.cpp) calls the
  actual `object::get_oid`, `data_manipulator::get_status`, compilation listener
  and variable-set lookup APIs. Controls use cached IDs/pointers and an isolated
  MRSW reader. One and two workers read immutable data; there are no concurrent
  writes to shader values. The four-ID case models call count, not an entire draw.
- [`13_live_recompile --benchmark`](../suite_graphics/13_live_recompile.cpp)
  measures motor's existing GL4 support thread, including msl processing and
  backend installation. The original interactive and negative smoke modes remain.
- [`01_raylib_compile_cost`](../suite_raylib/01_raylib_compile_cost.cpp) compares
  synchronous compile/link with a test-supplied worker/shared GLFW context.
  This worker is **not an out-of-the-box raylib asynchronous shader API**.

Build Release with `MOTOR_AI_BUILD_RAYLIB=ON`, then:

```sh
cmake --build build --config Release --target 08_render_access_cost 06_variable_set_cost 13_live_recompile 01_raylib_compile_cost --parallel 2
ctest --test-dir build -C Release -R "concurrent_render_access_cost|concurrent_variable_set_cost|raylib_compile_cost_help" --output-on-failure
```

Run in `build/bin/Release` on Windows (`build/bin` with a single-config generator):

```sh
08_render_access_cost --benchmark
06_variable_set_cost --benchmark
13_live_recompile --benchmark --gl-only --seed 1234
01_raylib_compile_cost --sync --seed 1235
01_raylib_compile_cost --worker --seed 1236
```

The [Windows runner](../comparison/run_compile_cost.ps1) runs the programs
sequentially, alternates order, saves stdout/stderr and imposes a 90-second
external timeout per process. Pass `-BuildDirectory`, `-OutputDirectory` and a
fresh `-SeedBase` as needed. Graphics programs are not registered in headless CI.

## Measurement Contract

The graphics tests use a 640 x 640 window with VSync off, thirteen cheap quad
draws, six compilations and settling intervals between requests. The compiled
program is used once on delivery; the steady workload does not retain its costly
pixel shader. No ImGui panel is drawn in motor's benchmark mode.

The shared source helper emits 128 dependent `sin` operations. Values depend on
a uniform, and constants vary by seed/revision. This is an intentionally
compiler-heavy synthetic shader, not a typical material or a throughput scene.
Seeds differ between modes to avoid deliberately warming the exact source for
the next mode. Reusing a seed permits cache reuse. We do not clear the driver
cache and do not claim a fully cold compiler.

motor checks both backend readiness and the compilation listener for every
revision. Its progress counter is incremented by executed command fences.
Raylib checks for successful non-default programs and required uniforms; its
progress counter counts completed main-loop frames before readiness is observed.
Neither counter is a GPU-completion timer or an exact compiler-overlap duration.
There is no automated image-equivalence assertion.

Raylib's worker only calls the inspected `rlLoadShaderProgram` compile/link path
on its shared context. It does not call high-level raylib drawing APIs. Adoption,
uniform lookup and deletion happen on the main thread. The worker is joined
before the contexts are destroyed. A `glFinish` after each compilation establishes
completion before publication; the synchronous control uses the same step.
There is **no per-frame glFinish**. This is not simply the timing of an unmodified
`LoadShaderFromMemory` call, and future raylib updates require rechecking the
worker path's use of shared global state.

Frame samples are CPU/application cadence, not GPU timestamps or measured
display presentation. motor samples `on_frame_done` intervals; raylib samples
whole main-loop iterations through `EndDrawing`. Compile-phase samples include
submission and delivery/first-use frames. Idle samples exclude startup and the
first settling frames. Cross-engine absolute frame times are not an isolated
API-overhead comparison: motor also runs its application infrastructure and
msl frontend, and the generated shader interfaces differ.

The CPU test uses 200,000 operations per worker, three repetitions with rotated
case order and warmup outside timing. Checksums prevent dead-code elimination;
thread launch is outside timing, while joining is included. Lookup measurements
include name/type search as well as locks. Two-reader `ns/op` divides wall time
by the total operations of both workers: it is aggregate throughput, not each
thread's individual latency. Cached controls require immutable/stable lifetimes;
they are not safe replacements for concurrently mutated object state.

## Results: 2026-10-14

Windows, MSVC Release, Intel Core i7-6700, NVIDIA RTX 2060,
driver `32.0.16.1088`. motor `1ebdc51` **plus the already-present experimental
GL4 `datas` lock bypass** from the [owner-thread experiment](GL4_OWNER_THREAD_EXPERIMENT.md).
Object/status and variable-set locks remain enabled. `MOTOR_PROFILING=OFF`.
Raylib is pinned by its wrapper to `fdc8b37391b52fdc8cc62af37d02235c9ec59daa`.
No builds ran alongside the recorded measurements.

### Actual CPU Access Costs

One-worker medians, rounded, from the final run:

| Operation | ns/op |
| --- | ---: |
| Cached ID control | 2.9 |
| MRSW reader + cached ID | 66.3 |
| `get_oid` | 67.9 |
| `get_status` | 74.7 |
| Listener `has_changed` | 69.0 |
| Cached data-variable read | 3.2 |
| Data-variable lookup, last of 16 | 164.1 |
| Cached texture pointer control | 3.8 |
| Texture-variable lookup, last of 8 | 85.5 |
| Four `get_oid` calls | 273.5 |
| Four cached ID calls | 6.6 |

This confirms a meaningful cost at these call sites. It does **not** mean that
the renderer becomes 20 times faster without locks. The measured difference
for four OID calls corresponds arithmetically to about 0.267 ms per 1,000 such
groups, or 1.33 ms per 5,000. Those are microbenchmark extrapolations, not measured
frame savings. A full draw has more work and other locks. Cached numeric uniform
reads themselves do not acquire variable-set locks.

### Compilation While Rendering

Three separate process runs per mode, six revisions per run, alternating order.
Ranges below are the three per-run values; latency is pooled over 18 requests.

| Mode | Idle frame mean | Compile-phase frame p95 | Worst frame per run | Mean request-to-ready | Progress per run |
| --- | ---: | ---: | ---: | ---: | ---: |
| motor GL4 support thread | 1.10-1.13 ms | 2.11-2.20 ms | 59-70 ms | 142.8 ms | 423-435 command rounds |
| raylib synchronous control | 0.41-0.43 ms | 84-103 ms | 139-149 ms | 87.9 ms | 0 frames |
| raylib + shared-context worker | 0.33-0.43 ms | 0.63-0.87 ms | 57-64 ms | 95.1 ms | 461-629 frames |

The synchronous phase has only one sample per compile, so its p95 is based on
six samples per run. Worker phases have hundreds of samples: p95 alone can hide
the remaining spikes. motor had 7 frames above 16.667 ms per run; the raylib
worker also had 7. Both still had roughly one >33 ms stall per revision.

**Conclusion:** asynchronous compilation demonstrably permits useful rendering
progress while awaiting a program. It did not reduce total compilation latency
in the raylib control. It also did not eliminate long frames in either worker
implementation. These measurements cannot separate driver contention, result
installation, first-use work and other synchronization as causes of those stalls.
motor's extra latency cannot be attributed solely to locks: it includes msl and
different backend integration work. There is no synchronous motor A/B in this test.

### Verification and Limits

- All nine graphics benchmark processes exited 0; all 54 new programs were
  reported successful and submitted for drawing.
- Both CPU benchmarks passed checksums with empty motor memory dumps.
- The three selected CTests passed.
- The existing GL4 invalid-shader/recovery smoke test also passed after the extension.
- motor's pre-existing `wglMakeCurrent(00)` shutdown diagnostic remains.
- Linux, D3D11 performance and pixel correctness were not established here.
- No new locks were removed, and no unsafe unlocked status/value experiment was
  run during concurrent compilation. The previous GL4 `datas` experiment remains
  an experimental prerequisite of these recorded results, not a production recommendation.

The tables above retain the results. Generated console logs are not kept in the
repository.
