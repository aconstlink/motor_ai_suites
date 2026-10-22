# D3D11 async rendering: performance and village shutdown

2026-10-22. Follow-up to the frame preparation/execution split and the explicit
application wakeup between `execute_prepare()` and `execute()`. No engine source
files were edited for these measurements or the shutdown investigation.

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

## Performance results

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
