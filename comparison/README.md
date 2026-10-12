# Archived engine comparisons

This directory retains the measurements from 2026-10-12 for Motor, raylib, bgfx,
Cinder and ezEngine. The external adapters, shaders, runners and CMake/FetchContent
integration were removed because their build overhead did not fit the suite's
workflow. That broad integration remains removed. A new, separately opt-in
[native raylib benchmark](../suite_raylib/README.md) now provides a single directly
linked comparison target. It does not alter the historical results below.

## Retained results

- [Results and source-level analysis](RESULTS.md): hardware, timings, validation and concrete draw-path differences.
- [Profiling checklist](RESULTS.md#profiling-checklist): pending controlled experiments for Motor.
- [Raw run summaries](results/2026-10-12.csv): all 63 measured runs, unchanged.
- [Source revisions](dependencies.json): historical provenance only, not an active dependency manifest.

Motor's own [05_scene_benchmark](../suite_graphics/05_scene_benchmark.md) remains
available. Use the archived baseline to guide profiling, not as a general engine
ranking or a promise of a particular speedup.

## Workload

- Windows, optimized Release builds; ezEngine used Shipping.
- 1280 x 720, orthographic camera at (0, 0, 30), looking along -Z.
- 100, 1,000 or 5,000 cubes, 24 vertices / 36 indices each.
- One shared mesh and shader, one indexed draw per cube; no instancing.
- Per-object animated world transform and constant unlit RGB color.
- Depth read/write, LEQUAL; no face culling, blending, textures, shadows, MSAA or postprocessing.
- Motor used its direct path with no lights, Scene Graph or Wire evaluation.
- VSync and application frame limit requested off; no forced GPU finish.
- 120 warmup frames, 300 measured intervals, three fresh-process repetitions per case.
- Sequential runs, reversed order on the second repetition; no concurrent builds.
- Visual captures were separate from timing runs.

Hardware, driver and revision details are in [Results](RESULTS.md). No new benchmark
run was performed when the build integration was removed.

## What the numbers mean

`frame_median_ms` and `frame_p95_ms` measure the interval between CPU frame starts.
They include framework scheduling, driver submission and present/backpressure
waits. **They are not GPU timestamps, input latency or measured display FPS.**
bgfx retained its normal multithreaded rendering path.

`update_median_ms` and `submit_median_ms` are diagnostic and **not directly equivalent
across adapters**: Cinder presents after the draw callback, raylib presents inside
EndDrawing, bgfx calls frame(), and Motor records frontend work separately from
backend execution. Motor's update path also includes its own transform/variable
machinery. Do not sum these phases into an assumed GPU time.

Known implementation differences:

- raylib used DrawMesh, separate attribute buffers and 16-bit indices, including
  its built-in matrix handling. Other adapters used 32-bit indices except
  ezEngine's small-mesh resource, which chooses its index width.
- Cinder used VboMesh/Batch and separate world/view/projection uniforms. bgfx used
  its predefined model-view-projection uniform and copied submission data internally.
  Uniform locations/handles were cached, not looked up by name per cube.
- ezEngine used RendererCore's mesh/shader/constant-buffer path, not an editor
  scene or world. Its position-only mesh omitted unused normal data. Automatic
  instancing was not involved; its frame housekeeping remained enabled.
- External adapters shared a directly evaluated matrix formula. Motor used its
  existing math classes, callbacks and variable sets. This was an API-use comparison,
  not an isolated backend microbenchmark.
- All adapters reported expected draws. bgfx also validated its actual draw count.
  GPU draws were not independently captured and counted for every API.
- Builds were pinned development snapshots, not necessarily stable releases.
  Results cannot be generalized to all versions, features, scenes or hardware.
- For even sample counts Motor averaged the two central values for its median;
  external adapters reported nearest-rank p50. The CSV preserves these statistics.

## Next steps for Motor

Start with the [profiling checklist](RESULTS.md#profiling-checklist): measure GL error
query overhead, backend data guards, redundant bindings and frame handoff separately.
Change one factor at a time and recheck rendering correctness. The source analysis
identifies candidates, not measured causes; a backend rewrite is not a prerequisite.

## Upstream sources

- [raylib](https://github.com/raysan5/raylib)
- [bgfx.cmake](https://github.com/bkaradzic/bgfx.cmake) / [bgfx](https://github.com/bkaradzic/bgfx)
- [Cinder](https://github.com/cinder/Cinder)
- [ezEngine](https://github.com/ezEngine/ezEngine)

Lighting, streaming and multi-window behavior were not compared by this baseline.
