# Temporary Village Backend Timing

This diagnostic instruments the Windows carrier, not individual draw calls. It
is deliberately **not enabled in the engine checkout by default**. The stored
patch changes only measurement code; original sleeps, polling thresholds,
commands, synchronization and presentation settings are unchanged.

Use one D3D11 window per process. The recorder is local to the carrier thread;
multi-window and mixed-backend traces are unsupported. It reserves space for
32768 records before the render loop and writes CSV only when that loop exits.
It reports overflow/write failure through `motor::log`. No per-frame file I/O,
profiling-manager locks, GPU queries, timer-resolution changes or new GPU fences
are introduced. Measurement overhead can still perturb timing, especially the
number of empty polling iterations; compare with an uninstrumented build.

## Run

From the AI suites repository, with a clean/unmodified carrier source:

```powershell
git -C motor apply --check ../suite_graphics/diagnostics/village_backend_trace.patch
git -C motor apply ../suite_graphics/diagnostics/village_backend_trace.patch
cmake --build <build> --config Release --target 15_village --parallel 2
.\suite_graphics\diagnostics\run_village_backend_trace.ps1 `
    -Executable <build>\bin\Release\15_village.exe
```

The runner sets `MOTOR_VILLAGE_BACKEND_TRACE` to a per-process CSV path, restoring
the parent environment afterward. It runs one/high, four/high and four/low three
times, each with 120 warmup and 360 measured frames. It records logs, environment
information, raw carrier traces and `summary.csv`. It fails if instrumentation
is absent, data was dropped, the process failed, or frame alignment is invalid.

Remove the instrumentation and rebuild after testing:

```powershell
git -C motor apply --reverse --check ../suite_graphics/diagnostics/village_backend_trace.patch
git -C motor apply --reverse ../suite_graphics/diagnostics/village_backend_trace.patch
cmake --build <build> --config Release --target 15_village --parallel 2
git -C motor status --short
```

Do not reset unrelated engine changes if the patch does not apply cleanly. This
patch was produced against motor `0bb59ef`; later carrier edits may need rebasing.

## Timing Boundaries

`15_village --benchmark` prints `VILLAGE_BENCH_WINDOW` with steady-clock nanosecond
timestamps for the measured application frame interval. The recorder uses the
same clock on the carrier thread. Only complete carrier cycles with
`previous_end_ns >= window_start_ns` and `end_ns <= window_end_ns`
are included by the runner. Typically this produces 359 complete carrier cycles
for 360 application samples; startup, warmup, boundary cycles and release are
excluded. Frame indices across the threads are not assumed to match.

Each cycle is split into:

- `gap_ms`: previous carrier render completion to the next render start.
- `setup_ms`: context activation, client rectangle and backend window info.
- `begin_ms`: backend `render_begin`.
- `execute_ms`: `render_engine::execute_frame`, including both upstream and
  execute queues, command cleanup and clearing its ready flag.
- `end_ms`: backend `render_end`.
- `present_ms`: `ctx.swap`, which calls DXGI `Present`.
- `deactivate_ms`: post-present bookkeeping and context deactivation.

The gap is further attributed to measured wall time in:

- `miss_sleep_ms`: the existing D3D11 `frame_miss > 5` backoff sleep, requested
  as 10 microseconds. Its actual duration is measured, without changing it.
- `outer_sleep_ms`: the carrier's existing general outer-loop sleep.
- `maintenance_ms`: D3D11 window-info/title/vsync-maintenance block, including
  any waits inside it; it is not a pure `SetWindowText` timer.
- `gap_other_ms`: remainder (other carrier work, scheduling and probe overhead).

`active_ms` is setup through deactivation. `cycle_ms = active_ms + gap_ms`.
Sleep counts give actual mean wall time **per sleep call**, separately from sleep
time averaged over all frames. The helper does not distinguish OS sleep time
from delayed scheduling after the nominal timeout.

Application `on_graphics` and recording may run while the carrier sleeps or
executes commands, so **do not add their times to the carrier decomposition**.
This trace does not directly time the application's readiness polling, mutex
contention individually, GPU execution, or how long a ready command queue stays
pending during a sleep. An active backend span may include driver/GPU waits.
Short `Present` wall time does not mean that all GPU work completed quickly.

See [measured results](../../docs/VILLAGE_D3D11_FRAME_TIMING.md).
