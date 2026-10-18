# village: D3D11 backoff sleep A/B test

2026-10-18. Follow-up to the [carrier timing investigation](VILLAGE_D3D11_FRAME_TIMING.md).
Only the existing D3D11 `frame_miss > 5` backoff sleep was temporarily disabled.
The engine source was subsequently restored byte-for-byte and rebuilt. This is
not a permanent engine optimization.

Follow-up: [notification wakeup and frame-pacing validation](VILLAGE_D3D11_WAKEUP.md)
measures the user's subsequently implemented wakeup path without changing the
engine. The values below remain the historical sleep/no-sleep baseline.

## Method

- Release, i7-6700 (4 cores / 8 logical processors), RTX 2060.
- Same village workload: one D3D11 window, VSync requested off, 1280x800 window,
  high geometry detail, HDR/post and shadows enabled, 32768 total particles.
- 1 and 4 tiles, three repetitions each with sleep and without sleep; 120 warmup
  frames and 900 measured frames. Case order reversed in the second repetition.
- The accepted comparison uses the no-sleep batch followed by three restored-
  sleep runs per tile count, with carrier recompilation confirmed in both logs.
- No carrier timing probes from the previous experiment were enabled. No changes
  to locks, timer resolution, graphics commands or other sleeps.

The test uses `GetProcessTimes` at the measurement boundaries to collect total
user + kernel CPU time for **all threads of the process**, excluding startup,
warmup and shutdown. It does not measure GPU time or isolate the carrier thread.
Only two CPU-counter reads are made per benchmark window. Frame boundaries and
CPU boundaries differ by a small amount of measurement/bookkeeping work.

`core_equivalents = process CPU time / wall time`.
`machine_percent = 100 * core_equivalents / logical processor count`.
Thus 12.5% here means one fully occupied logical processor, not 12.5% of one
core. This time-based metric need not exactly match Task Manager's display.

## Results

Representative run is the middle mean frame time of each three-run case.
CPU values are from that same run, not separately selected medians.

| Tiles | Sleep | Mean frame | FPS | Process CPU | Core equivalents |
| --- | --- | ---: | ---: | ---: | ---: |
| 1 | Original (restored) | 2.2436 ms | 445.71 | 26.41% | 2.11 |
| 1 | Disabled | 2.2419 ms | 446.05 | 24.59% | 1.97 |
| 4 | Original (restored) | 15.2250 ms | 65.68 | 13.78% | 1.10 |
| 4 | Disabled | 4.2466 ms | 235.48 | 25.50% | 2.04 |

Across all repetitions:

| Tiles | Sleep | Mean frame range | Process CPU range |
| --- | --- | ---: | ---: |
| 1 | Original | 2.1901-2.7556 ms | 21.50-27.84% |
| 1 | Disabled | 2.1209-2.3641 ms | 24.59-29.37% |
| 4 | Original | 15.1229-15.4768 ms | 13.78-15.86% |
| 4 | Disabled | 4.0445-4.9791 ms | 19.83-25.50% |

For four tiles, the representative throughput is **3.59x** higher. The process
uses more CPU per second, but delivers many more frames. The single-tile ranges
overlap; no convincing performance or CPU improvement is established there.

This strongly supports the previous trace's finding: the backoff policy, rather
than backend draw execution alone, caused a large part of the four-tile slowdown.
It does not establish that removing the sleep permanently is the right fix.
The no-sleep path keeps polling and performing carrier-loop maintenance, so
CPU/energy usage and behavior with idle or multiple windows still need attention.
An appropriate frame-ready wakeup remains the next design discussion.

The fixed-frame runs have different durations and were not interleaved between
engine builds. Scheduling/background activity can affect the short runs, as
shown by their ranges. These results are specific to this machine and workload,
not a universal speedup prediction. No visual or GPU timing validation was made.

### Build validation

An initial restore preserved the source's old modification timestamp, so the
incremental build skipped the carrier and still ran the no-sleep binary. That
control and the earlier baseline (whose binary provenance was not sufficiently
verified) are excluded. The source timestamp was then updated and the build log
confirmed that `win32_carrier.cpp` was compiled. The six restored-sleep results
above come only from that verified build. Both accepted build logs are retained.
All twelve accepted processes exited with code 0, passed the benchmark checks
and reported empty motor memory dumps.

## Evidence and reproduction

- [No-sleep results](results/village-d3d11-sleep-2026-10-18/no-sleep.csv).
- [Original-sleep results after verified rebuild](results/village-d3d11-sleep-2026-10-18/restored.csv).
- [Environment](results/village-d3d11-sleep-2026-10-18/environment.json).
- Logs for every run are retained in the same results directory.

From AI suites, using an unmodified carrier and the same Release build each time:

```powershell
cmake --build <build> --config Release --target 15_village --parallel 2
.\suite_graphics\diagnostics\run_village_sleep_comparison.ps1 -Mode sleep -Executable <exe>
git -C motor apply --check ../suite_graphics/diagnostics/village_no_backoff_sleep.patch
git -C motor apply ../suite_graphics/diagnostics/village_no_backoff_sleep.patch
cmake --build <build> --config Release --target 15_village --parallel 2
.\suite_graphics\diagnostics\run_village_sleep_comparison.ps1 -Mode no-sleep -Executable <exe>
git -C motor apply --reverse ../suite_graphics/diagnostics/village_no_backoff_sleep.patch
cmake --build <build> --config Release --target 15_village --parallel 2
.\suite_graphics\diagnostics\run_village_sleep_comparison.ps1 -Mode restored -Repeats 3 -Executable <exe>
```

The runner labels the mode; it does not change the engine or detect which build
is running. Restore the temporary patch even after a failed run. Do not combine
this patch with the carrier trace patch for the comparison above.
Verify that each changed variant really recompiles `win32_carrier.cpp`; restoring
an older file by copying it back can otherwise leave a stale executable/DLL.
