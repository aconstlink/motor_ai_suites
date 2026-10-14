# Datas locks: GL4 and D3D11

Measured on 2026-10-15 with Motor `306e7c5` (`unlocking-datas`). This compares
`USE_DATAS_LOCKS=1` and `USE_DATAS_LOCKS=0` on the same revision. It supersedes the
[earlier GL4-only experiment](GL4_OWNER_THREAD_EXPERIMENT.md) for this revision;
it does not replace the historical measurements in that report.

## Frame times

All times are milliseconds. Each result is the median of three separate process
runs' median frame intervals. Lower is better. The p95 columns similarly show
the median of the three per-run p95 values, not a pooled percentile.

| Backend | Workload | Locks on | Locks off | Reduction | p95 on | p95 off |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| OpenGL 4 | Direct, unlit | 9.096 | 6.281 | 30.9% | 18.572 | 11.854 |
| OpenGL 4 | Scene, two light passes | 21.210 | 17.152 | 19.1% | 31.041 | 24.447 |
| D3D11 | Direct, unlit | 3.801 | 3.269 | 14.0% | 10.565 | 5.561 |
| D3D11 | Scene, two light passes | 16.147 | 15.845 | 1.9% | 30.609 | 20.798 |

Both workloads use 1,000 objects. Direct mode records 1,000 draws and uses 1,000
variable sets. The multipass scene workload records 2,000 draws and uses 3,000
variable sets. These are application counts, not GPU-capture measurements.

### Variation between runs

| Backend / workload | Locks on: range of run medians | Locks off: range of run medians |
| --- | ---: | ---: |
| GL4 / direct | 8.035-9.991 | 5.362-6.579 |
| GL4 / scene | 20.316-22.789 | 16.520-17.505 |
| D3D11 / direct | 3.728-4.688 | 3.049-3.451 |
| D3D11 / scene | 16.120-17.712 | 15.703-15.870 |

The unlocked variant was faster in all twelve paired runs. GL4 and the direct
D3D11 workload show the clearest improvements. The D3D11 scene median changed
only slightly; do not treat its 1.9% difference as a reliable general speedup.
Its lower p95 is encouraging but also needs longer, controlled measurements.

This switch changes both locking and the container footprint because lock
members are conditionally compiled out. It does not remove OID/status locks,
variable-set locks or queue synchronization. These results therefore do not
measure a fully asynchronous frame-snapshot renderer.

## Conditions and reproduction

- Windows, Intel Core i7-6700, NVIDIA RTX 2060, optimized MSVC Release build.
- Requested window size: 1280 x 720; VSync explicitly requested off.
- 120 warmup frames followed by 300 measured frames per process.
- Three repetitions per backend, workload and lock setting: 24 benchmark runs.
- Sequential execution; no builds running during measurement. Locked ran first
  in rounds one and three, unlocked first in round two.
- Desktop load, CPU affinity and clock speeds were not controlled. Client size
  and DPI scaling were not independently reverified in this series.
- Frame intervals are CPU-side frame-cadence measurements, not GPU timestamps.
  No new pixel validation or 5,000-object measurement was performed.

Build `05_scene_benchmark` in Release for each setting of `USE_DATAS_LOCKS` in
`motor/platform/graphics/datas.hpp`. Rebuild affected targets after changing the
setting; do not mix incompatible container definitions within a build.

```powershell
./05_scene_benchmark.exe --backend gl --path direct --objects 1000 --lights 0 --warmup 120 --frames 300
./05_scene_benchmark.exe --backend gl --path scene --objects 1000 --lights 2 --warmup 120 --frames 300
./05_scene_benchmark.exe --backend d3d --path direct --objects 1000 --lights 0 --warmup 120 --frames 300
./05_scene_benchmark.exe --backend d3d --path scene --objects 1000 --lights 2 --warmup 120 --frames 300
```

The recorded series used separately built platform DLLs for the two settings.
The original source setting and unlocked DLL were restored afterward. No Motor
source changes or submodule-pointer update are part of this documentation.

Raw records: [all process outcomes](results/datas-locks-20261015/results.csv),
[individual measurements](results/datas-locks-20261015/measurements.csv), and
[summary](results/datas-locks-20261015/summary.csv). CSV numeric formatting follows
the original German locale, including decimal commas in quoted fields.

## Functional coverage

Each case ran once with locks and once without locks:

| Application and options | Coverage | Result, both variants |
| --- | --- | --- |
| `13_live_recompile --smoke --dual` | Initial failure, recovery, shader/layout changes, later failure and recovery | Passed |
| `04_reconfigure --smoke` | Configure/reconfigure, with and without release, texture changes | Passed |
| `01_window_lifecycle --smoke` | Secondary window close/reopen | Passed |
| `13_live_recompile --d3d-only --close-on-submit` | Early close after compilation submission | Passed |
| `13_live_recompile --gl-only --close-on-submit` | Early close after compilation submission | Passed |

All 34 processes (10 functional, 24 benchmark) exited with code 0, no `[FAIL]`
records and empty Motor memory dumps. The known WGL shutdown diagnostic
`wglMakeCurrent(00)` remains. Empty dumps do not prove the absence of driver/GPU
resource leaks, and these tests do not prove race freedom. Early close does not
guarantee that compilation was still active at shutdown. Generic concurrent
container tests were not run against unlocked `datas`: ownership assumptions
must be checked separately before removing synchronization in other callers.

## Historical framework reference

These are **not fresh side-by-side measurements**. The external framework values
below are from 2026-10-12 and are included only for orientation beside Motor's
new direct/unlit results. Different adapters, sampling and frame policies limit
comparability; no relative speedup against another engine is inferred here.

| Framework / path | Backend | 1,000-object median, ms | Series |
| --- | --- | ---: | --- |
| Motor, direct, locks on | GL4 | 9.096 | 2026-10-15 |
| Motor, direct, locks off | GL4 | 6.281 | 2026-10-15 |
| Motor, direct, locks on | D3D11 | 3.801 | 2026-10-15 |
| Motor, direct, locks off | D3D11 | 3.269 | 2026-10-15 |
| raylib, three matrices | OpenGL | 4.809 | Historical glFinish comparison |
| raylib, combined MVP | OpenGL | 4.005 | Historical glFinish comparison |
| bgfx | OpenGL | 0.825 | Historical adapter comparison |
| bgfx | D3D11 | 0.412 | Historical adapter comparison |
| Cinder | OpenGL | 0.782 | Historical adapter comparison |
| ezEngine | D3D11 | 0.516 | Historical adapter comparison |

Sources and qualifications: [raylib/glFinish series](../suite_raylib/GLFINISH.md)
and [archived adapter results](../comparison/RESULTS.md). The archived workloads
request VSync off and issue one draw per object without instancing. Cinder's
`Batch` groups rendering resources; it does not combine this workload into one
draw. The bgfx draw count was checked through its statistics; other archived
counts describe application calls, not an independent GPU capture. The adapters
also differ in transform handling; ezEngine used a position-only variant.
No comparable external two-light scene measurement is available in this series.
