# Village GL4 VAO lookup comparison

## Result: 2026-10-19

The linear VAO lookup is **not a measurable bottleneck in this village workload**.
Unlike the synthetic 4096-link test, the village has at most 43 or 45 live VAO
entries in a render object's list. Adding tiles mainly repeats draws of shared
geometry; it does not multiply the geometry-link list by four.

Windows, i7-6700, RTX 2060, Release / MSVC 14.51.36231. Engine revision
`b9a9632afe6f99e9635639b6817f92e05811abd2` with the temporary diagnostic patch.
Hardware, binary hashes and measurements are in
[results/village-vao-2026-10-19](results/village-vao-2026-10-19/).

Each run alternated the lookup at real backend frame boundaries. It used the
existing village benchmark: GL4, high detail, 1280 x 800, VSync off, frozen scene,
32768 total particles, moon shadows, transform feedback and the full HDR/bloom/
tone-mapping/FXAA pipeline. No village application or shader code was changed.
Three runs per tile count, 180 warmup frames followed by 800 measured frames.
Every run yielded 400 linear and 400 direct backend-frame samples.

| Tiles | Draws/frame | Maximum VAO-list length | Linear backend mean | Direct backend mean |
| ---: | ---: | ---: | ---: | ---: |
| 1 | 101 | 43 | 1.097 ms | 1.098 ms |
| 4 | 347 | 45 | 2.118 ms | 2.120 ms |

The table averages the three equally sized runs. Paired linear-minus-direct
differences per run were:

| Tiles | Run | Mean difference | Median paired difference | Direct-cache hit rate |
| ---: | ---: | ---: | ---: | ---: |
| 1 | 1 | +0.0031 ms | -0.0120 ms | 99.01% |
| 1 | 2 | +0.0031 ms | +0.0094 ms | 99.01% |
| 1 | 3 | -0.0104 ms | -0.0096 ms | 99.01% |
| 4 | 1 | +0.0109 ms | -0.0430 ms | 99.71% |
| 4 | 2 | -0.0421 ms | -0.0223 ms | 99.71% |
| 4 | 3 | +0.0257 ms | -0.0041 ms | 99.71% |

The sign changes between runs; there is no consistent speedup. Averages this
close should not be interpreted as the direct path being slower either.
App-frame means ranged from 2.46-2.67 ms for one tile and 4.43-5.06 ms for four.
Those frame means mix both modes and are not a separate linear/direct FPS test.

## What was measured

The temporary GL4 instrumentation measures CPU wall time from `begin_frame`
entry through `end_frame`, including existing flushes and backend work. It
excludes presentation after `end_frame`, does not measure GPU completion, and
adds no `glFinish`. Two clock reads and shared per-draw diagnostic counters are
present in both modes. Per-frame output is deferred until backend destruction.
Startup/teardown frames with at most 20 draws are excluded; the first 180 active
frames are discarded. Every recorded frame's draw count matched the application:
101 or 347. No per-draw timestamp queries are used.

The direct path uses a temporary `geo_idx -> geo_to_vaos index` vector within
each render object, checks the current geometry ID, and falls back to the normal
search on a miss. Both modes populate the same mapping. This avoids assuming
that lazy VAO creation follows geometry-link order, which is not guaranteed in
the village. One draw per direct frame still falls back; this is compatible with
the alternating transform-feedback geometry and is not hidden from the counts.

This is a diagnostic cache, not a fully tested production change. Reconfiguration,
release, shader changes, and multiple-window behavior were not tested here.
The engine files were restored byte-for-byte afterward and the normal village
target rebuilt. `motor_suites` was not touched.

All six runs exited successfully with empty motor memory dumps. Logs contain
startup `Could not find image []` / `Could not find image [gfx.postprocess.fb.5.0]`
messages and the WGL shutdown message. These are not silently treated as a clean
graphics-error check. A final short run after restoring the stock engine also
passed with an empty memory dump and produced the same startup/shutdown messages.
No image/pixel validation was performed.

## Reproduce

Use a compatible, clean motor checkout. Do not combine this patch with the
synthetic test's separate `vao_lookup.patch`.

```powershell
git -C motor apply --check ../suite_graphics/diagnostics/village_vao_lookup.patch
git -C motor apply ../suite_graphics/diagnostics/village_vao_lookup.patch
cmake --build build --config Release --target 15_village --parallel 2
& .\suite_graphics\diagnostics\run_village_vao.ps1 `
    -Executable .\build\bin\Release\15_village.exe `
    -OutputDirectory .\village-vao-results
git -C motor apply --reverse --check ../suite_graphics/diagnostics/village_vao_lookup.patch
git -C motor apply --reverse ../suite_graphics/diagnostics/village_vao_lookup.patch
cmake --build build --config Release --target 15_village --parallel 2
```

The patch includes `diagnostics/village_vao_probe.hpp` from the suites. Its
`MOTOR_VILLAGE_VAO=paired` mode alternates by backend frame, not by a hardcoded
draw count. `linear` and `direct` are also available. Only one GL4 window was
measured. The runner saves app frame CSVs, backend samples, summaries and logs.

For this demo, prioritize other costs before optimizing this lookup. The
[synthetic test](../suite_graphics/19_vao_lookup.md) still shows why a direct
mapping matters for render objects with thousands of linked geometries.
