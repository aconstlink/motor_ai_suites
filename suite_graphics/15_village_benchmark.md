# Village: One vs. Four Tiles

For the later frame preparation/execution split, see the
[2026-10-22 D3D11 async rendering report](../docs/D3D11_ASYNC_RENDERING.md):
updated cube/village timings and the reproduced shutdown hang, followed by
eight successful runs with the corrected shutdown condition.

Measured on 2026-10-18. This is a scene workload comparison, not a pure draw-call
microbenchmark. The engine was not modified.

## Setup

- Windows, MSVC Release; Intel Core i7-6700 (4 cores / 8 threads), RTX 2060.
- Driver reported by Windows: `32.0.16.1088`.
- AI suites base commit `a7c8efe`; motor commit `0bb59ef`, clean submodule.
  The benchmark additions were local changes to the suite at measurement time.
- One window/backend at a time, requested vsync off, 1280x800 window.
- Fixed camera and frozen initial pose; one tile keeps its original position
  in the four-tile layout. No ImGui draw calls during measurement.
- HDR/bloom/FXAA enabled at the existing 1920x1080 internal resolution; moon
  shadows at 2048x2048; 32768 total particle seeds in every case.
- 120 warmup frames, 360 measured frames, three separate runs per case.
  Case order reverses for the second repeat. No concurrent build or other test.

Reproduce:

```powershell
.\suite_graphics\run_village_benchmark.ps1 `
    -Executable <build>\bin\Release\15_village.exe `
    -Warmup 120 -Frames 360 -Repeats 3
```

Raw data: [24 runs](../docs/results/village-2026-10-18.csv) and
[environment](../docs/results/village-2026-10-18-environment.json).
See [sample documentation](15_village.md#one-vs-four-tiles-benchmark) for options
and timing boundaries.

## Workload

| Tiles | Detail | Triangles per geometry pass | Draws including shadow, particles and post |
| ---: | --- | ---: | ---: |
| 1 | high | 323,145 | 101 |
| 1 | low | 157,462 | 101 |
| 4 | high | 1,285,296 | 347 |
| 4 | low | 622,564 | 347 |

Both color and shadow passes draw the geometry. Low detail roughly halves the
triangle count while keeping each layout's number of material leaves/draws.
Four tiles use two wells and two statues; they are not four identical copies of
the single well tile. Each mesh's light calculations remain in one color draw.

## Frame Results

For each configuration, the representative run is the middle of the three runs
sorted by **mean frame time**. Other columns come from that same run; these are
not pooled percentiles. FPS is `1000 / mean frame time`.

| Backend | Tiles | Detail | Mean ms | Median ms | p95 ms | FPS | Range of run means, ms |
| --- | ---: | --- | ---: | ---: | ---: | ---: | --- |
| GL4 | 1 | high | 2.4735 | 2.3506 | 3.6179 | 404.28 | 2.3790-2.9549 |
| GL4 | 4 | high | 5.4740 | 5.1797 | 7.7716 | 182.68 | 5.4592-5.8283 |
| GL4 | 1 | low | 2.5606 | 2.4676 | 3.6479 | 390.54 | 2.4622-2.6028 |
| GL4 | 4 | low | 5.4765 | 5.2479 | 7.8528 | 182.60 | 5.0248-5.7968 |
| D3D11 | 1 | high | 2.0433 | 2.0038 | 2.8523 | 489.40 | 2.0031-2.1084 |
| D3D11 | 4 | high | 15.3793 | 15.5328 | 17.4822 | 65.02 | 15.2268-15.5150 |
| D3D11 | 1 | low | 2.3593 | 1.9086 | 4.9481 | 423.86 | 1.9160-2.5657 |
| D3D11 | 4 | low | 15.5606 | 15.5549 | 16.9795 | 64.27 | 15.2410-15.9534 |

Frame time here is the interval between successful application submissions at
`on_frame_done`, after readiness and warmup. It includes the effect of pipeline
backpressure/presentation on frame cadence; it is not a GPU completion timestamp.

## Callback Timings

Average callback wall times from the same representative runs:

| Backend | Tiles | Detail | on_graphics ms | on_render preparation ms |
| --- | ---: | --- | ---: | ---: |
| GL4 | 1 | high | 0.2634 | 0.2656 |
| GL4 | 4 | high | 1.0962 | 1.2066 |
| GL4 | 1 | low | 0.2799 | 0.2872 |
| GL4 | 4 | low | 1.0527 | 1.1919 |
| D3D11 | 1 | high | 0.3069 | 0.3110 |
| D3D11 | 4 | high | 1.4886 | 1.4673 |
| D3D11 | 1 | low | 0.4116 | 0.3828 |
| D3D11 | 4 | low | 1.2514 | 1.1821 |

These include waits inside callbacks and therefore are not CPU-utilization
measurements. Backend execution, GPU work and waits outside callbacks are not
separately measured. Subtracting these columns from frame time does **not** yield
GPU time.

## Interpretation

- The tested workload is strongly sensitive to tile/object count. High-detail
  mean frame time grows about 2.2x on GL4 and 7.5x on D3D11 for four tiles.
- Reducing triangle density does not materially improve the four-tile runs on
  either backend. Raw triangle count is therefore unlikely to be the dominant
  limit in these measurements.
- Scene update and recording costs visibly grow with object count. On GL4, their
  combined time rises from about 0.53 ms to 2.30 ms in the representative high
  runs. They explain part, not all, of the frame-time increase.
- D3D11's roughly 15.4 ms frame time is **not** explained by its approximately
  3 ms of measured callback time. In these original measurements, the remaining
  interval was unclassified. A
  backend/driver or synchronization/presentation bottleneck remains possible;
  this test does not prove a specific lock or draw implementation is responsible.
- More scene nodes, render-data sets, lights, draw submissions and pixel coverage
  change together. Particle counts are fixed, but their category distribution
  and spatial overlap are not identical. Low-detail geometry changes screen
  coverage somewhat too. This is not a controlled isolation of one engine cost.
- Three short runs expose variance, not statistical certainty. In particular,
  the slower single-tile low-detail mean is not evidence that fewer triangles
  cause a slowdown. No GPU timings or utilization trace were captured.

The follow-up [D3D11 execution/waiting measurements](../docs/VILLAGE_D3D11_FRAME_TIMING.md)
now locate the largest measured interval in the existing carrier backoff sleep:
roughly 12.5 ms per four-tile cycle in the instrumented runs, compared with
roughly 1.5 ms for backend command execution. That report includes uninstrumented
controls and the important polling-related measurement caveat. No engine
optimization was applied.

## Validation

All 24 benchmark processes exited with code 0, passed final subset/transform/
light/draw-count checks and reported empty motor memory dumps. Release built
without compiler warnings. Both `--tiles 1` and `--tiles 4` also passed the normal
dual-backend low-detail smoke test, with 160 frames per backend and empty dumps.
The existing GL4 initialization/unused-attribute and WGL shutdown diagnostics
remain. No visual/pixel verification was performed during these measurements.
