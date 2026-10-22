# Async rendering: gl4/d3d11 performance and village shutdown

2026-10-22. Follow-up to the frame preparation/execution split and the explicit
application wakeup between `execute_prepare()` and `execute()`. No engine source
files were edited for the frame-time comparisons or the shutdown investigation.
The separate [scene preparation profile](../suite_graphics/05_scene_benchmark.md#scene-preparation-breakdown-2026-10-22)
used temporary probes, which were fully removed afterwards.

## Versions and method

- AI suites base: `a82b5739c3e04f9f38aca475d6a05b7b2c7d11e2`.
- Performance build: motor `4990522db6e3d5721449a3b307dd62217acc06e8`.
- Shutdown-fix validation: motor `100ba183a31121cedb2b4c88e013d9c2d230eb53`.
- Historical reference: motor `7779fd8`, early-sync experiment. That version
  released the frame early but did not yet explicitly wake the app between
  prepare and execute. The temporary experiment patch was removed afterwards.
- Windows, MSVC Release, D3D11, one window, VSync requested off; same development
  machine as the earlier reports (i7-6700 / RTX 2060).
- Each run: 120 warmup frames and 400 measured frames. No concurrent benchmark
  or build. No GPU timestamps, scheduler tracing or new CPU-utilization probe.

Cubes use 5000 individual object draws, no lights and no instancing. The direct
path has 5000 variable sets; the scene-graph path has 10000. Village uses four
high-detail tiles, 32768 particles, 347 draws, and 1,285,296 triangles per geometry
pass. Window size is 1280x800; existing HDR/bloom/FXAA and shadow settings are
retained. Benchmark mode freezes the village pose and disables its GUI.

## Latest alternating-backend comparison

Follow-up on motor `100ba183a31121cedb2b4c88e013d9c2d230eb53`, AI suites
`115bb97`, on the same i7-6700 / RTX 2060 machine with NVIDIA driver 610.88.
Each workload/backend combination ran three times, alternating gl4 and d3d11
and reversing the order in the second repetition. Release, one window, VSync
requested off, 120 warmup and 400 measured frames; no engine modifications.
Cubes use 1280x720; village uses 1280x800 and the workload described above.

Values are arithmetic means of three per-run medians/p95s, not pooled percentiles.

| Workload | Backend | Frame median, ms | Frame p95, ms | on_render preparation, ms |
| --- | --- | ---: | ---: | ---: |
| 5000 cubes, direct | gl4 | 19.36 | 26.10 | 0.78 |
| 5000 cubes, direct | d3d11 | 11.95 | 68.87 | 1.00 |
| 5000 cubes, scene graph | gl4 | 25.69 | 30.49 | 10.48 |
| 5000 cubes, scene graph | d3d11 | 28.44 | 54.38 | 9.54 |
| Village, four tiles with postprocessing | gl4 | 4.91 | 7.42 | 1.18 |
| Village, four tiles with postprocessing | d3d11 | 3.80 | 6.31 | 1.31 |

All 18 runs exited successfully with empty tracked-memory dumps. gl4 frame
medians ranged from 19.19-19.60 ms (direct), 25.37-26.04 ms (scene) and
4.79-5.04 ms (village). The previously observed large gl4 variation was not
reproduced in this series. Desktop load and GPU clocks were not held constant;
these results neither identify a unique cause nor rule out future variation.
The high d3d11 cube p95 values remain important: lower medians do not establish
consistently smooth frame pacing. No GPU timestamps or new pixel validation.

Raw records for this series remain in the local measurement workspace:
`C:/Users/acons/Documents/motor help/backend-ab-20261022/summary.csv`, with
per-run logs and CPU/GPU telemetry alongside it. No extra results page is needed.

### Progress relative to earlier measurements

For 5000 direct, unlit cubes with one draw per object, the
[12 October comparison](../comparison/RESULTS.md) reported 37.815 ms for gl4
and 30.630 ms for d3d11. The latest series reports 19.36 and 11.95 ms,
approximately **49% and 61% lower frame times**, respectively.
The earlier d3d11 async scene result of 43.95 ms is now 28.44 ms, about 35% lower.

These are historical trends, not isolated optimization speedups or controlled
same-session A/B results. Revisions, drivers and system load changed; the
12 October report uses the median of run medians, while this report averages
run medians. The much older 5 October single-run results are retained in the
[scene benchmark history](../suite_graphics/05_scene_benchmark.md#direct-path-comparison-2026-10-05),
but are not a suitable basis for a precise overall speedup claim.

The remaining scene preparation costs are measured separately in the
[visitor/bridge/command breakdown](../suite_graphics/05_scene_benchmark.md#scene-preparation-breakdown-2026-10-22).
The roughly 10-12 ms there is application-side preparation, not total frame
time and not backend execution. It must not be compared directly to the total
frame times in this table as though it were the same metric.

## Earlier d3d11 performance results

Retained measurements from the `4990522` build, before the latest comparison.
In this section, "current" refers to that earlier measurement session.

Entries are **arithmetic means of the individual runs' frame medians and p95s**,
not percentiles of pooled frame samples. The historical column uses only the
early-sync rows of the retained reference CSVs.

| Workload | Historical median, ms | Current median, ms | Current p95, ms | Successful current runs |
| --- | ---: | ---: | ---: | ---: |
| 5000 cubes, direct | 20.89 | 13.27 | 72.22 | 3 |
| 5000 cubes, scene graph | 43.95 | 32.06 | 57.91 | 3 |
| Village, four tiles with postprocessing | 5.15 | 4.65 | 8.14 | 2 |

Current per-run medians range from 12.60 to 14.59 ms for direct cubes, 29.85 to
33.49 ms for scene-graph cubes, and 4.58 to 4.72 ms for village. The two successful
village runs have an average **mean frame time of 5.03 ms**.

These results show lower frame medians than the retained measurements, but are
**not a contemporaneous, controlled A/B test**. They do not isolate the effect
of the wakeup or the new execution flag. Scheduling and other system activity
were not independently controlled or measured. High cube p95 values remain;
the improved medians do not establish uniformly smooth frame pacing.

Frame timing measures application cadence, not GPU execution or actual display
presentation intervals. Cube timing spans `on_graphics` starts; village timing
uses its existing `on_frame_done` measurement. Callback preparation timings
must not be interpreted as backend execution time.

All eight successful processes exited with code 0 and empty motor memory dumps.
The third village attempt and one retry timed out after 100 and 30 seconds,
respectively, with no benchmark line visible in their redirected output. They
are excluded from the performance table, not treated as successful runs. The
follow-up below explains the reproduced failure mode.

## Village hang: shutdown, not shader compilation

On the old `4990522` build, the first diagnostic reproduction hung after the
measurement had completed. The optional frame CSV already contained its header
and **all 400 measured frames**. Two external thread-stack samples, taken about
two seconds apart, located the app thread in:

```text
motor::application::app::carrier_shutdown()
  -> motor::application::app::clear_out_window_data()
     -> mutex unlock while repeatedly checking render-engine state
```

Its accumulated user CPU time increased from 11.625 to 13.875 seconds between
the samples: it was repeatedly checking the condition, not permanently blocked
inside the mutex. The carrier thread was waiting in:

```text
motor::platform::win32::win32_carrier::on_exec()
  -> NtUserMsgWaitForMultipleObjectsEx()
```

The old condition in `application/app.cpp`, `clear_out_window_data()`, was:

```cpp
if( !re->can_enter_frame() || !re->needs_execute() ) return false;
```

Once execution was finished, `needs_execute()` was false, so teardown kept
returning false instead of returning the borrowed frontend and releasing the
window data. The user's `100ba18` fix removes that second negation:

```cpp
if( !re->can_enter_frame() || re->needs_execute() ) return false;
```

This waits while there are pending frame commands or execution is still in
progress, and permits cleanup after both are finished.

The truncated shader message in the earlier timeout logs was misleading. The
console logger uses `printf` without an explicit flush; redirected output can
retain later messages in its buffer until shutdown. The completed frame CSV
and thread samples demonstrate that the reproduced hang was **after rendering**,
not at the last shader line visible in the log. The original two timeouts did
not have stack captures, so their exact stacks are not independently known.

## Verification with the fix

With `100ba18`, **eight village runs passed**, each with 120 warmup / 400 measured
frames, exit code 0 and an empty memory dump: one initial run, six consecutive
repetitions, and one final run after restoring and rebuilding the current commit
following the old-version countercheck.

Their mean frame times range from **4.00 to 4.84 ms**. These are shutdown
validation runs from a separate session, not additional samples for the earlier
performance table or proof that the shutdown fix improved rendering speed.

The reproduced shutdown failure is resolved in these checks. This is not an
exhaustive proof of all window/synchronization cases. No visual/pixel comparison
or Linux validation was performed here. The AI-suites engine checkout and village
binary were restored to `100ba18`; no motor source changes or submodule pointer
update are included in this documentation change.

## Reproduction and retained data

```powershell
cmake --build build --config Release --target 05_scene_benchmark 15_village --parallel 2
build/bin/Release/05_scene_benchmark.exe --backend d3d --path direct --objects 5000 --lights 0 --warmup 120 --frames 400
build/bin/Release/05_scene_benchmark.exe --backend d3d --path scene --objects 5000 --lights 0 --warmup 120 --frames 400
build/bin/Release/15_village.exe --d3d-only --benchmark --no-vsync --detail high --tiles 4 --total-particles 32768 --warmup 120 --frames 400
```

For shutdown diagnosis, `MOTOR_VILLAGE_FRAME_CSV` can name a per-run CSV written
after measurement and before `close()`. The external stack sampler briefly
suspended/resumed each thread; it was used only on the hung old-version process,
not during successful performance measurements. No probes were added to motor.

- [Current performance runs](results/d3d11-async-2026-10-22/performance.csv).
- [Historical cube runs](results/d3d11-async-2026-10-22/historical-cubes.csv).
- [Historical village runs](results/d3d11-async-2026-10-22/historical-village.csv).
- [Eight shutdown-fix validation runs](results/d3d11-async-2026-10-22/shutdown-validation.csv).

Only compact measurement data is retained here, not bulk console logs, binaries
or process dumps. The diagnostic stack evidence is summarized above.
