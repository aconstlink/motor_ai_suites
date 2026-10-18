# village: D3D11 execution and waiting

Measured 2026-10-18. This is a diagnostic follow-up to the
[tile/detail comparison](../suite_graphics/15_village_benchmark.md), not an engine
optimization. The temporary carrier probes were removed from source before the
control runs. A later A/B test found that restoring the old file timestamp could
leave the previously built carrier binary in place; see the correction below.
The motor checkout is unchanged.

## Setup

- Release, i7-6700 (4 cores / 8 threads), RTX 2060, driver 32.0.16.1088.
- AI suites base: `a7c8efe00d36800cadfb29e2507a4bbd6eed1e50`;
  motor: `0bb59eff08d41c6301df884885a60c8c745dd119`.
- One D3D11 window, 1280x800, VSync requested off. Existing post-processing
  and shadow settings retained, 32768 particles, frozen benchmark pose.
- Three runs per instrumented case, 120 warmup / 360 measured application
  frames. Only complete carrier cycles inside that measurement window are
  included (359 or 360 cycles).
- No per-draw probes, GPU timestamps, timer-resolution changes, extra waits,
  per-frame logging or synchronization. Records are buffered until shutdown.

## Where the time goes

Representative run = middle application mean of three repeats, independently
for each case. All numbers below are wall-clock milliseconds per carrier cycle.

| Phase | 1 tile, high | 4 tiles, high | 4 tiles, low |
| --- | ---: | ---: | ---: |
| Backend command execution (`execute_frame`) | 0.570 | 1.466 | 1.439 |
| Present (`ctx.swap`) | 1.250 | 0.150 | 0.150 |
| Other active work: setup, begin/end, deactivate | 0.019 | 0.027 | 0.028 |
| Existing D3D11 backoff sleep | 0.040 | 12.597 | 12.488 |
| Window maintenance during the gap | 0.187 | 0.951 | 1.014 |
| Remaining gap | 0.158 | 0.447 | 0.452 |
| **Complete carrier cycle** | **2.224** | **15.638** | **15.570** |
| Application frame cadence (separate measurement) | 2.222 | 15.635 | 15.571 |

Rounding can affect column sums. The outer-loop sleep contributed zero to these
aligned cycles. Maintenance includes `find_window_info`, window-title updates
and VSync checks; it is not a measurement of `SetWindowText` alone.

For four high-detail tiles, the three runs measured 1.382-1.522 ms in command
execution, 0.150-0.158 ms in Present, and 12.367-12.597 ms in backoff sleep.
Reducing geometry detail left the sleep-dominated pattern intact.

### The backoff

In `platform/application/win32/win32_carrier.cpp`, the D3D11 branch calls
`sleep_for(microseconds(10))` when `can_execute()` is false and `frame_miss > 5`.
That requests **0.010 ms**, but the measured elapsed time includes the operating
system's eventual rescheduling of the thread. In the representative four-tile
high-detail run it averaged **12.562 ms per sleep call**. Every aligned cycle
contained at least one such sleep. It accounts for about **81%** of that run's
carrier cycle. In the representative single-tile run, only 1 of 359 cycles
contained a backoff sleep.

This points to the polling/backoff policy as the largest measured contributor
in this workload, rather than approximately 14 ms of hidden draw execution.
Longer producer work with four tiles plausibly makes it more likely that the
consumer reaches its empty-poll threshold. That explanation is an inference:
the trace does not record exactly when commands become ready during a sleep.
It therefore does not establish how much of every sleep was avoidable.

## Trace-disabled controls and limitations

After restoring the carrier source byte-for-byte and invoking the build, six
controls used the same settings with the trace environment variable unset:

| Case | Run 1 mean | Run 2 mean | Run 3 mean |
| --- | ---: | ---: | ---: |
| 1 tile, high | 2.0172 | 2.0600 | 2.5369 |
| 4 tiles, high | 14.2559 | 14.6993 | 9.7196 |

**Correction from the subsequent sleep A/B test:** copying back the original
file preserved an older timestamp, allowing the incremental build to skip the
carrier. These controls cannot be certified as a probe-free binary. The trace
was disabled, but its inactive checks may still have been compiled in. Treat
them as trace-disabled controls only. The [sleep A/B report](VILLAGE_D3D11_SLEEP_AB.md)
uses confirmed carrier recompilation for both compared variants.

The instrumented four-tile high runs were 15.3299-15.7866 ms. The control
variation and the lower control means show that these probes are **not
timing-neutral** for a polling-sensitive workload, or that run-to-run scheduling
variation matters, or both. Do not present the 81% breakdown as an exact
decomposition of every normal run. The controls still show a large
unexplained-by-draw-count-alone cadence variation; only the instrumented runs
directly identify the measured sleep location.

`execute_frame` is CPU wall time for command dispatch, updates, driver calls
and cleanup, including any blocking there. It is **not GPU execution time**.
Present may also block despite requested VSync-off. These measurements do not
prove that GPU costs or individual locks are negligible in other workloads.

Application `on_graphics` / `on_render` callback timings overlap the carrier
thread's timeline. Do not add them to this table or subtract them as if they
were sequential phases. The small difference between application cadence and
carrier cycle is expected from their separate boundaries and window filtering.

## Next decision

The first focused optimization experiment should address how the carrier waits
for a ready frame, for example notification-based wakeup instead of repeated
empty polling followed by a scheduler-dependent sleep. Measure latency and CPU
utilization together, including shutdown and multiple windows. Do not infer a
guaranteed FPS gain by simply subtracting the sleep duration.

No wait policy, lock, draw path or engine behavior was changed for this report.
All 15 final measurement/control processes exited with code 0 and empty motor
memory dumps. All nine instrumented runs reported successful trace writes with
no dropped records. No visual validation or GPU timing was performed.

## Reproduction and evidence

- [Temporary diagnostic patch, helper and runner](../suite_graphics/diagnostics/README.md).
- [All nine run summaries](results/village-d3d11-2026-10-18/summary.csv).
- [Six trace-disabled controls](results/village-d3d11-2026-10-18/control-summary.csv).
- [Environment](results/village-d3d11-2026-10-18/environment.json).
- [Representative one-tile trace](results/village-d3d11-2026-10-18/1-high-r3.trace.csv).
- [Representative four-tile high trace](results/village-d3d11-2026-10-18/4-high-r2.trace.csv).
- [Representative four-tile low trace](results/village-d3d11-2026-10-18/4-low-r3.trace.csv).

The results directory also retains the other six raw traces and all stdout /
stderr logs. `window_start_ns` / `window_end_ns` in the summary select the
complete cycles from each raw trace. Times use a common steady-clock epoch.
