# village: app-thread CPU sleep comparison

2026-10-18. Tests of the existing `carrier::set_cpu_sleep` / `get_cpu_sleep`
API with 0, 1, 10 and 30 microseconds. **No engine changes.**

## Setup

- Release / MSVC 14.51, i7-6700 (8 logical processors), RTX 2060.
- One D3D11 window, VSync off, four village tiles, high detail, HDR/post and
  shadows enabled, fixed benchmark pose, 32768 total particles, 347 draws.
- 1280x800 window, 1920x1080 post target, 2048x2048 shadow map.
- Three runs per setting, each with 120 warmup and 900 measured frames.
- Order: `0,1,10,30`, then `30,10,1,0`, then `10,0,30,1`.
- Sleep is set before the application starts its threads. No timer-resolution,
  thread-priority, backend, synchronization or driver changes were made.

CPU is process user + kernel time, across all threads, measured only during the
frame window. Percentages are normalized over all eight logical processors.
Frame times are `on_frame_done` intervals, not GPU or display-presentation times.

## Results

Representative row is the middle mean frame time of each three-run group.
Other values in the row come from the same run, not independently chosen medians.

| Requested sleep | Mean frame | FPS | Process CPU | Frame p95 | Frame p99 | Mean sleep probe |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| 0 us | 3.2468 ms | 307.99 | 26.74% | 4.6345 ms | 5.2739 ms | no sleep |
| 1 us | 15.5496 ms | 64.31 | 3.21% | 16.7409 ms | 17.7330 ms | 15.5112 ms |
| 10 us | 15.5738 ms | 64.21 | 3.39% | 16.7986 ms | 17.5932 ms | 15.5574 ms |
| 30 us | 15.5552 ms | 64.29 | 4.02% | 16.8231 ms | 17.5697 ms | 15.5116 ms |

All-repetition ranges and frame outliers:

| Sleep | Mean frame range | Process CPU range | Mean probe range | Frames >16.67 ms | Worst frame |
| --- | ---: | ---: | ---: | ---: | ---: |
| 0 us | 3.2287-3.2767 ms | 26.16-28.57% | no sleep | 0 / 2700 | 6.1795 ms |
| 1 us | 15.5491-15.5526 ms | 3.21-3.67% | 15.4313-15.5263 ms | 177 / 2700 | 21.5506 ms |
| 10 us | 15.5487-15.5829 ms | 3.25-5.45% | 15.4815-15.5574 ms | 183 / 2700 | 31.5870 ms |
| 30 us | 15.5421-15.6169 ms | 3.48-4.14% | 15.4771-15.5116 ms | 188 / 2700 | 35.0905 ms |

There is no convincing useful distinction between the three positive settings
on this machine. They sharply reduce CPU usage but throttle frame preparation
to approximately 64 FPS. One 30-us frame exceeded 33.33 ms. These short runs do
not exclude rarer spikes in any mode, including 0 us.

## What the sleep probe measures

After frame and CPU measurement finish, `--sleep-probe` runs 128 samples on the
same app thread. Each sample times the same conditional call used in the engine:

```cpp
auto const requested = motor::application::carrier::get_cpu_sleep() ;
if( requested > std::chrono::microseconds(0) )
    std::this_thread::sleep_for(requested) ;
```

This is **a separate calibration, not a trace of the engine's internal sleeps**.
The app thread is no longer producing frames during this phase; its load and
the carrier's load therefore differ from the benchmark phase. Exact internal
sleep durations would require additional tracing/instrumentation. Nevertheless,
the measured call durations and the actual benchmark frame cadence agree on
the coarse timing effect. At 0 us the call is skipped, matching the update loop;
the recorded 0.0250-0.0328 us means are only clock/branch measurement overhead.

All allocations are outside the timed sleep call, and probe CSV output happens
after the samples. Probe time and CSV I/O are outside the frame/CPU measurement.
The shared sleep setting is also consulted by the carrier's `micro == 0` guard;
this experiment tests the existing API, not an exclusively update-thread switch.

## Why microseconds turn into milliseconds

Inspection of the installed MSVC 14.51.36231 `include/thread` implementation:
`sleep_for` calls `sleep_until`, which rounds a positive remaining duration up
using `chrono::ceil<chrono::milliseconds>` before `_Thrd_sleep_for`.
Thus a positive remaining sub-millisecond duration is not submitted to that
primitive as a microsecond delay. Timer resolution and rescheduling add further
delay; about 15.5 ms was observed here. This is not a universal sleep duration
for Windows, other standard libraries, or a different timer environment.

## Conclusion

The existing setting works as a coarse throttle, but **not as a gentle 1-30 us
pause** on this build/machine. It delays the whole app loop, including servicing
input/audio/physics deadlines, not just graphics. Fixed-step catch-up does not
make those callbacks arrive earlier. Input latency was not measured directly.

For the stated goal of responsive independent updates, retain 0 us for now.
Lower CPU usage here comes together with fewer frames and delayed app updates;
it is not an equal-throughput measurement of the cost of polling alone. A
deadline/event-based wait remains a separate future change, not part of this test.

## Reproduction and evidence

```powershell
cmake --build <build> --config Release --target 15_village --parallel 2
.\suite_graphics\diagnostics\run_village_cpu_sleep.ps1 -Executable <path-to-15_village.exe> -OutputDirectory <results>
```

- [All run summaries](results/village-cpu-sleep-2026-10-18/summary.csv).
- [Environment and engine-state check](results/village-cpu-sleep-2026-10-18/environment.json).
- 900 individual frame samples and 128 sleep samples per run are
  retained in the same directory. Percentiles use nearest rank.

All twelve runs exited with code 0, passed final scene/draw checks and reported
empty motor memory dumps. No screenshot verification was performed. The runner
uses bounded process lifetimes and restores its environment variables. The
engine diff was verified unchanged before/after this experiment.
