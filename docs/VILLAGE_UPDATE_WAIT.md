# village: update-thread waiting, one and two windows

2026-10-19. Validation of the user's app wait/notification changes, including
the audio residual correction and GL4/D3D11 completion notifications.
**No engine files were changed.** Engine diff contents were checked before/after.

## Method

- Release, MSVC 14.51.36231, i7-6700 (8 logical processors), RTX 2060.
- Four high-detail tiles, HDR/post, shadows, frozen benchmark pose, no UI.
- VSync requested off, `get_cpu_sleep() == 0` in all runs.
- Window 1280x800, post target 1920x1080, shadow map 2048x2048.
- 32768 particles and 347 draws per window; dual mode submits 694 draws per app frame.
- Three repetitions of D3D11-only, GL4-only and GL4+D3D11. Each has 120 warmup
  and 900 measured frames; mode order rotates between repetitions.
- Additional 6000-frame runs for D3D11-only and dual mode.
- Actual compilation of `app.cpp` and `win32_carrier.cpp` is recorded in the build log.

The test alone was extended to accept `--benchmark --dual`. Sampling starts only
after both windows submit their complete scene. Per-window frame intervals and
draw counts are validated. `prepare_ms` sums per-window recording durations.

Frame time is CPU-side `on_frame_done` cadence. Per-window intervals are between
successful scene-recording completions, not GPU completions or display presents.
The existing app readiness gate waits for all required windows before preparing
the next graphics frame; the dual FPS is therefore approximately the FPS of
each window, not the sum of their rates.

CPU is whole-process user + kernel time divided by wall time and eight logical
processors. It includes all threads, not only the update thread. CSV output is
outside the measured interval. No GPU timing or visual/pixel comparison was done.

## Short runs

Representative row is the middle mean frame time of each three-run group.
CPU and percentiles come from that same run.

| Mode | Mean frame | FPS | Process CPU | p95 | p99 |
| --- | ---: | ---: | ---: | ---: | ---: |
| Previous D3D11, no update wait, sleep 0 | 3.2468 ms | 307.99 | 26.74% | 4.6345 ms | 5.2739 ms |
| Current D3D11 | 3.4845 ms | 286.98 | 17.00% | 4.8954 ms | 6.1530 ms |
| Current GL4 | 5.0175 ms | 199.30 | 12.46% | 6.7640 ms | 7.5100 ms |
| Current GL4+D3D11 | 9.5030 ms | 105.23 | 16.72% | 13.6370 ms | 17.0631 ms |

The historical D3D11 baseline is from the
[CPU sleep comparison](VILLAGE_CPU_SLEEP.md), not a newly rebuilt old engine.
It has the same workload/settings but precedes the per-window timing extension.
There is no matching historical GL4 or dual-mode baseline in this experiment.

| Current mode | Mean frame range | Process CPU range |
| --- | ---: | ---: |
| D3D11 | 3.3901-3.5816 ms | 17.00-18.42% |
| GL4 | 4.3258-5.4572 ms | 12.41-17.01% |
| GL4+D3D11 | 8.0832-9.6200 ms | 15.61-16.94% |

D3D11 shows substantially lower process CPU, about 36% in the representative
comparison, with about 7% higher mean frame time. This is not an isolated,
equal-throughput per-thread profile; background scheduling and measurement
variation remain relevant. In particular, GL4/dual repeat variation is large.
No result supports blaming a specific wait, driver call or GPU stage for that
variation without further instrumentation.

## Longer frame-time checks

| Mode, 6000 frames | Mean | FPS | CPU | p95 | p99 | Maximum |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| D3D11 | 3.3710 ms | 296.65 | 18.05% | 4.4810 ms | 5.1754 ms | 23.2975 ms |
| GL4+D3D11 | 7.7826 ms | 128.49 | 17.62% | 10.2044 ms | 12.0539 ms | 17.0144 ms |

D3D11 had 7 frames above 8.33 ms and 2 above 16.67 ms. Dual mode had 1708 above
8.33 ms and 1 above 16.67 ms. Neither long run exceeded 33.33 ms. Across the
short dual runs, however, the worst app-frame interval was 41.0903 ms, with two
intervals above 33.33 ms. The longer run does not invalidate those outliers.

Per-window cadence in the long dual run:

| Window | Samples | Draws/frame | Mean | p95 | p99 | Maximum |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| GL4 | 6000 | 347 | 7.7826 ms | 10.0582 ms | 11.9690 ms | 15.7637 ms |
| D3D11 | 6000 | 347 | 7.7826 ms | 10.1934 ms | 12.0307 ms | 17.0135 ms |

Both windows continue to receive frames; neither stalls or starves in these
checks. Dual mode renders the scene twice, uses two different backends and the
shared carrier, and follows the app's all-window readiness gate. Its frame time
is not a pure measurement of the overhead of one additional window.

## Validation and conclusion

All eleven benchmark processes exited with code 0, passed final scene/subset/
draw-count checks and reported empty motor memory dumps. The existing dual
window lifecycle smoke (including reopening) and dual village smoke also pass.
GL image/unused-attribute initialization and graphics teardown diagnostics are
not fixed by this work; passing smoke checks does not imply diagnostic-free logs.

The current waiting approach lowers CPU usage without the approximately 15.5 ms
throttle seen with positive `get_cpu_sleep()` values. There are still occasional
frame-time spikes. Input/audio/physics latency was not measured; these results
establish rendering progress and CPU/cadence behavior, not real-time guarantees.

## Reproduction

```powershell
cmake --build <build> --config Release --target 15_village 01_window_lifecycle --parallel 2
.\suite_graphics\diagnostics\run_village_update_wait.ps1 -Executable <exe> -OutputDirectory <results>
.\suite_graphics\diagnostics\run_village_update_wait.ps1 -Executable <exe> -OutputDirectory <results> -Frames 6000 -Repeats 1 -Modes d3d11,dual -Label update-wait-long
```

- [Short-run summaries](results/village-update-wait-2026-10-19/update-wait.csv).
- [Short-run per-window summaries](results/village-update-wait-2026-10-19/update-wait.views.csv).
- [Long-run summaries](results/village-update-wait-2026-10-19/update-wait-long.csv).
- [Long-run per-window summaries](results/village-update-wait-2026-10-19/update-wait-long.views.csv).
- [Build and hardware metadata](results/village-update-wait-2026-10-19/environment.json).

Individual frame CSVs are retained alongside these summaries.
No engine rollback, patches or commits were
made for the comparison.
