# village: notification wakeup and frame pacing

2026-10-18. Validation of the user's current Win32/concurrent/render-engine
notification changes. No engine files were edited for this experiment.

## Functional checks

- `concurrent_work_notify`: five consecutive passes. Eight delayed graph nodes
  progress using notifications only, without periodic scheduler polling. Also
  checks independent listener registration/unregistration, the render-engine
  move constructor, and publishing ready state before invoking its callback
  outside the render-engine lock.
- `graphics_carrier_wakeup`: five consecutive passes. Runs the actual platform
  carrier loop without an app update thread, windows or rendering. After the
  startup task, a producer waits 500 ms and schedules another task. After that
  completes, it waits another 100 ms and closes the idle carrier. There is no
  periodic wakeup fallback in the test.
- Task schedule-to-execution latency in that idle-carrier test: **0.0694-0.5678 ms**.
  Close-to-carrier-return latency: **0.0251-0.1782 ms**. These include scheduling
  and dispatch overhead; they are not isolated `SetEvent` syscall timings.
- `01_window_lifecycle --smoke`: passed with GL4 and D3D11, including opening,
  closing and reopening the secondary window.
- `15_village --dual --smoke --detail low --particles 256 --tiles 4`: passed.
- All executed checks and benchmarks exited with code 0 and empty motor memory
  dumps. This is targeted validation, not an exhaustive synchronization proof.

The graphics smokes still emit diagnostics: `GetClientRect failed` during D3D11
window teardown, `wglMakeCurrent(00)` at GL shutdown, and GL image/unused-attribute
messages during village initialization. Passing the smoke does not mean these
diagnostics are fixed. No screenshot/pixel comparison was performed.

## Benchmark method

Same machine and workload as the [sleep A/B test](VILLAGE_D3D11_SLEEP_AB.md):
i7-6700, 8 logical processors, RTX 2060, Release, VSync requested off, one D3D11
window, 1280x800 window, 1920x1080 post-processing, 2048x2048 shadows, high detail,
32768 total particles, frozen benchmark pose, no GUI. One tile has 101 draws;
four tiles have 347. Three 900-frame runs per size, following 120 warmup frames.
The tile order is reversed in repetition two. A separate four-tile run measures
9000 frames (about 31 seconds), also after 120 warmup frames.

Frame time is the existing `on_frame_done` cadence, not a GPU timer or display
presentation interval. Individual frame/callback times are saved **after** both
measurement windows finish, so CSV I/O does not enter the measured interval.
CPU is process user + kernel time over the measurement window, normalized over
all eight logical processors. 25% means roughly two occupied logical processors.

The old-sleep and no-sleep rows below are retained measurements, not newly rerun
engine variants. The current build includes both the notification changes and
the user's increased `frame_miss` threshold of 1000. Therefore this is not an
isolated A/B attribution to `SetEvent` alone. No backend timing instrumentation
or engine sleep patch was enabled. Carrier recompilation was confirmed.

## Comparison

Representative run: middle mean frame time of three runs; all other columns
come from that same run. FPS is reciprocal mean frame time.

| Tiles | Version | Mean frame | FPS | p95 | Process CPU |
| --- | --- | ---: | ---: | ---: | ---: |
| 1 | Previous original sleep | 2.2436 ms | 445.71 | 3.7001 ms | 26.41% |
| 1 | Previous no-sleep experiment | 2.2419 ms | 446.05 | 3.7819 ms | 24.59% |
| 1 | Current notification build | 2.0645 ms | 484.38 | 2.9443 ms | 28.59% |
| 4 | Previous original sleep | 15.2250 ms | 65.68 | 17.3592 ms | 13.78% |
| 4 | Previous no-sleep experiment | 4.2466 ms | 235.48 | 6.9913 ms | 25.50% |
| 4 | Current notification build | 3.3418 ms | 299.24 | 4.9478 ms | 25.07% |

Current three-run ranges:

| Tiles | Mean frame | p95 | p99 | Process CPU |
| --- | ---: | ---: | ---: | ---: |
| 1 | 2.0632-2.1115 ms | 2.8499-3.4150 ms | 3.3727-4.5145 ms | 27.96-30.82% |
| 4 | 3.2985-3.3485 ms | 4.9085-4.9478 ms | 5.5168-5.7364 ms | 25.07-27.17% |

The four-tile representative throughput is about **4.56x** the previous original
sleep result. Compared with the previous no-sleep experiment, frame time is about
21% lower at similar process CPU usage. The three-run one-tile ranges overlap
earlier measurements; avoid treating the small difference as a robust speedup.
Uncapped CPU usage has **not** substantially decreased. The application is doing
more frames per second, so this does not measure equal-throughput power savings.
An equal-FPS cap and a separate idle CPU measurement would be needed for that.

## Longer frame-time check

Four tiles, 9000 measured frames:

| Metric | Value |
| --- | ---: |
| Mean / median | 3.4308 / 3.1332 ms |
| FPS | 291.48 |
| p95 / p99 | 5.2449 / 6.5385 ms |
| Maximum | 38.6023 ms |
| Frames above 8.33 ms | 31 / 9000 (0.344%) |
| Frames above 16.67 ms | 6 / 9000 (0.067%) |
| Frames above 33.33 ms | 1 / 9000 (0.011%) |
| Process CPU / core equivalents | 25.91% / 2.073 |

p99 uses the nearest-rank definition. p95 uses the existing benchmark's sorted
sample convention. The largest spikes were 38.60, 29.63 and 22.25 ms. There is no
longer a persistent approximately 15 ms cadence, but isolated stutters remain.
Their cause is **not established**: no scheduler trace, GPU timer or per-wait
instrumentation was collected. They must not be labelled lost wakeups solely
from frame times. Short runs alone missed these rarer spikes.

## Reproduction and evidence

```powershell
cmake --build <build> --config Release --target 09_work_notify 18_carrier_wakeup 01_window_lifecycle 15_village --parallel 2
ctest --test-dir <build> -C Release -R "^(concurrent_work_notify|graphics_carrier_wakeup)$" --output-on-failure --repeat until-fail:5
.\suite_graphics\diagnostics\run_village_wakeup.ps1 -Executable <exe> -OutputDirectory <results>
.\suite_graphics\diagnostics\run_village_wakeup.ps1 -Executable <exe> -OutputDirectory <results> -Mode wakeup-long -Tiles 4 -Repeats 1 -Frames 9000
```

The carrier test is Windows-only. The generic notification test is not tied to
Win32, but was only built/run on Windows in this check. Both have CTest timeouts.
The benchmark runner has bounded process lifetime and restores environment
variables. `MOTOR_VILLAGE_FRAME_CSV` is optional for normal village runs.

- [Short-run summary](results/village-wakeup-2026-10-18/wakeup.csv).
- [Long-run summary](results/village-wakeup-2026-10-18/wakeup-long.csv).
- [Long-run individual frame times](results/village-wakeup-2026-10-18/wakeup-long-4-r1.frames.csv).
- [Idle-carrier test log](results/village-wakeup-2026-10-18/carrier-ctest.log).
- [Notification test log](results/village-wakeup-2026-10-18/notify-ctest.log).
- [Build/state metadata](results/village-wakeup-2026-10-18/environment.json).

All per-run frame CSVs, stdout/stderr and final build logs are retained in the
same results folder. Engine diff contents were compared before and after;
the user's existing seven modified engine files remained unchanged.
