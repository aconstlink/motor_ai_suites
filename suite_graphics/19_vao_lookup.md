# VAO lookup benchmark

`19_vao_lookup` measures the GL4 draw path with a fixed number of draws and an
increasing number of linked geometries in one render object. It does not use the
scene graph. This targets the `std::find_if` over `render_data::geo_to_vaos`.

## Run

```powershell
cmake --build build --config Release --target 19_vao_lookup --parallel 2
.\build\bin\Release\19_vao_lookup.exe --geometries 256 --draws 4096 --warmup 80 --frames 400
```

The regular test runs against the unchanged engine. The environment switch below
only works with the optional diagnostic patch; the runner checks for its marker.
This is a manual graphics benchmark, not a timing-sensitive CTest assertion.

## Workload and timing

- One GL4 window, requested 640 x 480, VSync disabled.
- One shader, one empty variable set, 4096 indexed draws per measured frame.
- 1, 16, 256 or 4096 distinct geometries linked to that shader. Each has identical
  tiny triangle data. Links are drawn cyclically and first encountered in order.
- No changing uniforms, lights, postprocessing, scene traversal or reconfiguration.
- 80 warmup frames create all VAOs before 400 measured frames.
- Two frontend fence callbacks bracket the execution of the draw-command block.
  `backend_*_ms` includes backend work, GL calls, driver overhead and any blocking;
  it is not GPU time or isolated search time. Push/clear, pop and presentation are
  outside that interval. `frame_*_ms` measures successive application `on_render`
  callbacks. No per-draw timing calls or extra `glFinish` are added.
- Sample storage is preallocated. An atomic completion counter publishes the last
  result before the app reads it. Early close and timeout invalidate the run.
- Successful runs require exit code zero and an empty motor memory dump. There is
  no pixel readback, and the memory dump does not validate driver allocations.

The MSVC-only C4723 suppression in this test is for the inlined buffer setup:
`vertex_buffer::resize` calculates a floating-point ratio with an initially zero
element count. The engine implementation is unchanged.

## Optional A/B diagnostic

`diagnostics/vao_lookup.patch` changes only the VAO lookup in GL4. Both modes use
the same binary. `MOTOR_VAO_LOOKUP=linear` keeps the search;
`MOTOR_VAO_LOOKUP=direct` first tries `geo_to_vaos[geo_idx]`, checks the matching
geometry ID, and falls back to the original search on a miss. A one-time log line
confirms the selected mode. The single-mode paths have no per-draw counters or
diagnostic logs.

For a comparison less sensitive to differences between process launches, the
patch also supports `MOTOR_VAO_LOOKUP=paired`. A render-thread-local static draw
counter alternates the lookup every 4096 draws. This mode is only valid with
exactly 4096 draws per frame, one render thread and even warmup/frame counts.
The test's `VAO_PAIR_SAMPLES` line reports even/odd backend means and the mean
and median of neighboring even-minus-odd samples. With this mode, even frames
use linear search and odd frames use direct lookup. The counter adds a small
common overhead to both frame types; it is not a production feature.

This direct path is valid for the ordered, stable links used here. It is **not a
complete production replacement**: relinking, invalidated entries, shader changes
and transform-feedback geometry can require a different mapping/invalidation policy.

Apply only to a clean, compatible engine checkout, then rebuild:

```powershell
git -C motor apply --check ../suite_graphics/diagnostics/vao_lookup.patch
git -C motor apply ../suite_graphics/diagnostics/vao_lookup.patch
cmake --build build --config Release --target 19_vao_lookup --parallel 2
& .\suite_graphics\diagnostics\run_vao_lookup.ps1 `
    -Executable .\build\bin\Release\19_vao_lookup.exe `
    -OutputDirectory .\vao-results
```

The runner makes three repetitions and reverses both the workload order and mode
order on the second repetition. It saves every run plus `summary.csv`. Do not run
other benchmarks or builds concurrently. The results still depend on scheduling,
driver behavior, power management and the desktop environment.

For paired measurements, use a separate output directory:

```powershell
& .\suite_graphics\diagnostics\run_vao_lookup.ps1 `
    -Executable .\build\bin\Release\19_vao_lookup.exe `
    -OutputDirectory .\vao-paired-results -Modes paired -Repeats 2 -Frames 600
```

Frame-cadence statistics in paired mode mix both lookup strategies; use the
separate even/odd backend times for that comparison. Frame parity statistics in
an unpatched or single-mode run are not a linear/direct comparison.

Remove only this diagnostic patch after the test, then rebuild the normal binary:

```powershell
git -C motor apply --reverse --check ../suite_graphics/diagnostics/vao_lookup.patch
git -C motor apply --reverse ../suite_graphics/diagnostics/vao_lookup.patch
cmake --build build --config Release --target 19_vao_lookup --parallel 2
```

## Interpretation

The search length is per render object, not per scene. One render object with
4096 geometries is very different from 4096 render objects with one geometry
each. For cyclic access to N valid entries, the linear path averages (N+1)/2
comparisons per draw. This benchmark does not establish the gain for the village
demo or for the overall engine; its geometry-link distribution must be considered.

## Measurements: 2026-10-19

Windows, Intel Core i7-6700, NVIDIA RTX 2060, driver 32.0.16.1088,
MSVC 14.51.36231 / Visual Studio 18 Community, Release.
Engine revision: `b9a9632afe6f99e9635639b6817f92e05811abd2` plus the temporary
diagnostic patch. Base AI Suites revision:
`db22c851336f19faf46174f9ab234ba5dad7924a`.

[Raw runs, CSV and environment](../docs/results/vao-lookup-2026-10-19/)
include both experiments. The first 24 runs used separate processes for each
mode. They exhibited substantial run-to-run variation, even at one link, so
their differences should not be interpreted as precise lookup costs. The
original single-mode patch is archived beside those results.

The paired experiment uses two runs per geometry count, each with 80 warmup
frames and 600 measured frames: 300 linear and 300 direct. Alternating the mode
within each process reduces slow drift and context-startup differences, though
it cannot remove scheduling or driver noise. The initial experiment and paired
experiment have slightly different diagnostic binaries; do not subtract their
absolute times from each other.

Backend draw-block times in milliseconds; each mean contains 300 frames:

| Links | Run | Linear mean | Direct mean | Mean saving | Median paired saving |
| ---: | ---: | ---: | ---: | ---: | ---: |
| 1 | 1 | 5.741 | 5.748 | -0.007 | -0.057 |
| 1 | 2 | 16.268 | 16.354 | -0.086 | -0.058 |
| 16 | 1 | 6.281 | 5.920 | 0.361 | 0.427 |
| 16 | 2 | 9.952 | 10.032 | -0.081 | 0.045 |
| 256 | 1 | 18.391 | 17.921 | 0.470 | 0.553 |
| 256 | 2 | 6.438 | 6.157 | 0.282 | 0.275 |
| 4096 | 1 | 24.787 | 20.200 | 4.587 | 4.308 |
| 4096 | 2 | 23.902 | 19.776 | 4.126 | 3.920 |

The large-list result is consistent: approximately 4.1-4.6 ms saved per 4096
draws, or 17-19% of this backend draw block. The 256-link result is smaller
(0.28-0.47 ms); at 16 links the mean difference is not repeatable, and one link
shows practically no benefit. These are measured differences, not confidence
intervals or universal thresholds. Absolute times varied substantially between
runs, so extrapolating a whole-engine speedup would be misleading.

A per-render-object direct indirection is therefore worth considering for large
geometry-link lists. This test does not justify replacing every small linear
search or adding a hash map. It also does not exercise holes/stale entries after
reconfiguration, which can make the existing list longer in other workloads.

All measured processes exited successfully with empty motor memory dumps.
The known `[WGL Context] : wglMakeCurrent(00)` shutdown diagnostic appeared in
the logs; no other engine errors were found. No visual correctness or GPU timing
claim is made. Linux was not tested.

After measurement, the temporary engine source was restored byte-for-byte and
the normal target rebuilt. Only the standalone test, optional diagnostic patch,
runner and documentation/results remain in AI Suites.
