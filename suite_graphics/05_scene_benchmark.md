# Scene and light-pass benchmark

This is a scaling benchmark, not a pass/fail performance threshold. It uses the
motor revision pinned by this repository. No engine changes are required.

Latest frame-time comparison: [gl4/d3d11 async rendering, 2026-10-22](../docs/D3D11_ASYNC_RENDERING.md#latest-alternating-backend-comparison).
The [scene preparation breakdown](#scene-preparation-breakdown-2026-10-22) below
separates visitor lookups, bridges, status checks and command recording.
The [datas locks on/off measurements, 2026-10-15](../docs/DATAS_LOCK_COMPARISON.md)
remain a historical controlled comparison, not the latest overall timings.

## Run

Build `05_scene_benchmark` in Release. From `build/bin/Release`:

```powershell
./05_scene_benchmark.exe --backend gl --objects 1000 --lights 2
./05_scene_benchmark.exe --backend d3d --objects 1000 --lights 2
./05_scene_benchmark.exe --backend dual --objects 1000 --lights 2
```

The application closes after 120 warmup frames and 300 measured frames. It has
a 300-second watchdog; closing a window early invalidates the measurement.
`--warmup` and `--frames` override these counts. `--lights 0` uses an unlit shader.
Object counts are capped at 5000 and lights at 8 to avoid accidental oversized runs.
No ImGui overlay is drawn. VSync is requested off, but a driver/compositor can
still limit presentation. Keep other workloads closed and do not minimize windows.
The window title identifies the backend, render path, lighting mode, object count
and light count, for example
`motor benchmark | D3D11 | scene | singlepass | 5000 objects | 2 lights`.
It is set once at window creation, without per-frame title updates.

For the full Windows matrix (fresh process per case):

```powershell
./suite_graphics/run_scene_benchmark.ps1
# Include the shared-scene, dual-backend case:
./suite_graphics/run_scene_benchmark.ps1 -Backends gl,d3d,dual -Repeats 3
# Short functional run, not a reliable performance measurement:
./suite_graphics/run_scene_benchmark.ps1 -Objects 100 -Lights 0,2 -Warmup 5 -Frames 10
```

The runner creates a timestamped folder under `build/benchmark-results`, preserving
stdout/stderr, CPU/GPU/driver information and a CSV. It stops on failure and keeps
earlier results. Do not compare Debug timings with Release. GL is the only mode
accepted on non-Windows systems; the matrix runner itself is Windows PowerShell.

## Workload

### Direct, unlit comparison

The same executable also supports `--path direct --lights 0`. This path creates
no scene nodes, components, visitors or wire bridges. It retains the same cube,
unlit msl shader, grid, camera, animation, render state and window lifecycle.
Each object owns one variable set through the shared msl object. Borrowed world
variable pointers are cached and updated in `on_graphics`; `on_render` simply
records one draw per object. Camera and color values remain constant.

```powershell
./suite_graphics/run_scene_benchmark.ps1 -RenderPath direct -Lights 0 -Objects 1000 -Backends gl,d3d -Warmup 120 -Frames 300
./suite_graphics/run_scene_benchmark.ps1 -RenderPath scene -Lights 0 -Objects 1000 -Backends gl,d3d -Warmup 120 -Frames 300
```

The runner records `render_path` in its CSV and environment file. Direct mode
rejects nonzero light counts. Its `sync` time is zero; `trafo` includes matrix
construction and writing world variables; `prepare` measures draw recording.

This is a practical lower-level baseline, NOT an isolated measurement of visitor
overhead: direct mode uses N sets versus 2N for the unlit scene path, avoids
bridge propagation and initializes all sets before configuration. The scene
path creates its derived sets dynamically. Constant camera/material values are
not rewritten in direct mode. Differences can therefore affect backend work too.

### Scene path

- 100 / 1000 / 5000 scene leaves, each with a transform and msl-set component.
- One shared cube (24 vertices, 36 indices) and one shared msl object per process.
- One base variable set per object; one derived render-data set per object/pass.
- Unlit baseline or 1 / 2 / 4 / 8 directional-light traversals using motor visitors.
- Per-object colors are copied through the existing bridges. Deterministic rotation
  updates all transforms with a fixed angular step, independently of frame rate.
- Fixed orthographic camera, 1280x720 framebuffer. Cubes shrink with object count
  to keep the grid visible; this is NOT a constant-pixel-cost GPU benchmark.
- Light passes use additive blending and less-equal depth testing with depth writes.
  There is no extra base/depth pass, shadow map, postprocessing or instancing.
- Dual mode renders the same scene/camera into GL4 and D3D11 windows. Identical
  camera/light data intentionally allow both windows to reuse the same subsets.

### Two or three lights in one shader

`--path scene --lighting singlepass --lights 2` (or `3`) evaluates all directional
lights in one pixel shader and submits one draw per object/window. The default
`--lighting multipass` retains the existing additive pass per light.

The single-pass shader uses ordinary named `vec3_t light_dir_0`, `light_dir_1`
and optionally `light_dir_2` variables with explicitly unrolled diffuse terms.
No msl arrays, defines, shader-language changes or backend changes are required.
Directions are initialized in each base variable set and reach the one derived
render-data set through the existing scene bridges. A regular render visitor
handles the single traversal; the multipass reference uses the light visitor.

Both modes use the same direction-generation function, object colors (scaled
by `1 / light_count`), geometry, camera and deterministic animation. Singlepass
sums the diffuse terms in the shader and disables additive framebuffer blending;
multipass accumulates each contribution into the framebuffer. Their intended RGB
results match apart from intermediate target rounding. Alpha is not equivalent:
the reference adds alpha per pass whereas singlepass writes alpha once. Alpha
is not consumed by this test. There are no shadows, specular or ambient terms.

For N objects and L lights, multipass uses N*(1+L) sets and N*L draws per window;
singlepass uses 2*N sets and N draws per window. This compares the complete
workflows, including fewer traversals, bridge updates, sets and buffer uploads,
not merely a shader-loop cost. Singlepass currently accepts only 2 or 3 lights
and only the scene path. Direct/unlit behavior is unchanged.

```powershell
./suite_graphics/run_scene_benchmark.ps1 -RenderPath scene -Lighting multipass -Lights 2,3 -Objects 1000 -Backends gl,d3d -Warmup 120 -Frames 300 -Repeats 2
./suite_graphics/run_scene_benchmark.ps1 -RenderPath scene -Lighting singlepass -Lights 2,3 -Objects 1000 -Backends gl,d3d -Warmup 120 -Frames 300 -Repeats 2
```

Lighting mode is included in the title, startup log, result filenames, environment
metadata and CSV. The `BENCH` line appends a `lighting` column after the existing
timing fields; older result files remain unchanged. The runner rejects missing
or mismatched modes, so an old executable cannot silently produce comparison data.

## Measurements and limits

### Single-pass comparison (2026-10-07)

Release, i7-6700 / RTX 2060, current local engine build, 1000 objects, 120 warmup
frames and 300 samples per run. Each backend/light/mode combination ran twice
in separate processes. Values below are the range of the two per-run frame
medians in milliseconds, not the range of individual frames.

| Backend | Lights | Multipass | Singlepass |
| --- | ---: | ---: | ---: |
| D3D11 | 2 | 15.867 - 16.299 | 15.677 - 15.737 |
| D3D11 | 3 | 29.420 - 29.865 | 15.673 - 15.776 |
| GL4 | 2 | 19.573 - 20.645 | 11.009 - 11.271 |
| GL4 | 3 | 28.722 - 30.208 | 11.070 - 11.595 |

D3D11 two-light frame cadence barely changed, although render preparation fell
from 3.570-3.894 ms to 1.823-1.878 ms. Three-light preparation fell from
5.652-6.045 ms to 1.974-2.120 ms. These callback measurements cannot explain the
remaining frame interval or distinguish scheduling/presentation from GPU cost.
Do not interpret the approximately 16 ms interval alone as proof of a VSync cap.

All 16 measured runs and two additional dual-backend smoke cases (100 objects,
2/3 lights, 5 warmup/10 samples) completed successfully with the expected set
counts and empty motor memory dumps. Both generated shader variants compiled
on GL4 and D3D11. The invalid direct/singlepass combination was rejected. The
known WGL shutdown diagnostic remains. Pixel equivalence has not been visually
or through readback verified; successful command flow is not proof of matching
images. No engine source changes were made for this feature.

Logs/CSVs are under `build/benchmark-results`:

- `20261007-111138-550`: dual-backend singlepass smoke.
- `20261007-111201-737`: multipass reference, two repetitions.
- `20261007-111352-907`: singlepass comparison, two repetitions.

### Metric definitions

`BENCH,...` is a machine-readable log line; all durations are milliseconds:

- `frame`: interval between `on_graphics` starts (application frame cadence).
  It includes previous-frame work, scheduling, synchronization and presentation
  effects, but is NOT GPU elapsed time or the display's actual FPS.
- `sync`: variable-update visitor plus small callback bookkeeping.
- `trafo`: deterministic transform animation plus transformation visitor.
- `prepare0/1`: render traversal, bridge/subset updates and command recording per
  window. Backend execution is asynchronous and not timed by these values.
- Median and nearest-rank p95 are reported after warmup. Individual metric
  medians need not add up to median frame time. A single run is not a conclusion.
- Draw count is the EXPECTED number of object/pass submissions, not hardware
  instrumentation. The observed variable-set count is checked against
  `objects * (1 + max(1, lights))` for scene/multipass, `2 * objects` for
  scene/singlepass, and `objects` for direct. Compilation/configuration failure cannot
  produce a valid row merely by running the warmup loop.

The test does not measure GPU timestamps, allocation event counts, VRAM or peak
RAM. motor's memory dump checks tracked allocations after shutdown only; it is
not a measurement of runtime allocation churn. No changes to the engine's
profiling/memory observer are made. A successful run does not prove pixel
correctness; visually verify at least one small scene on each backend.

The benchmark is intentionally not registered as a normal CTest: it requires a
real graphics session and results depend on hardware, driver and window system.

## Async rendering follow-up (2026-10-22)

The [gl4/d3d11 async rendering report](../docs/D3D11_ASYNC_RENDERING.md) records the
latest 5000-cube direct/scene results, retained historical references, frame-time
outliers, and the separate village shutdown investigation. It includes per-run
CSV data and distinguishes the performance build from shutdown-fix validation.

## Scene preparation breakdown (2026-10-22)

Profiled the existing gl4 scene path on motor `100ba18`: 5000 cubes, no lights,
one window, Release, VSync off, 120 warmup and 400 measured frames. The earlier
alternating-backend series measured 10.48 ms median preparation. Uninstrumented
control runs before/after this profile measured **11.42-12.18 ms**; the historical
10.48 ms is not an exact budget that the following values must reconstruct.

| Preparation work | Approximate mean per frame, ms |
| --- | ---: |
| Base input bridge: slots to base variables | 1.07 |
| Output bridge: base variables to output slots | 0.99 |
| Subset input bridge: exchange and variable writes | 1.49 |
| Component queries in the render visitor | 2.00 |
| Frontend command construction and recording | 2.01 |
| Shader status/decode checks | 0.80 |
| Compilation listener and base-binding check | 0.97 |
| Subset existence/creation check | 0.17 |
| Writing cached camera variables | 0.24 |

These are rounded averages of two sparse runs' projected, approximately
timer-corrected **means**, not independent savings available by deleting code.
Every eighth node was sampled, rotating selection between frames; each cube
category contains 250000 timed operations per run. An empty scope estimates
timer-pair cost. Spans are disjoint; bridge rows include their variable copies,
so those copies must not be counted again as separate variable-set costs.

Sparse-probe preparation medians were 11.84/12.12 ms. A full-probe check reached
15.22 ms, versus 11.33 ms with inner timers disabled. Instrumentation is therefore
not free. Calibration cannot remove cache, scheduling or branch effects. Other
tree traversal, msl hash lookups and bookkeeping are not individually isolated;
the residual also includes measurement effects. Do not sum these estimates to
claim an exact reconstruction of an uninstrumented frame median.

The hot path performs approximately 20000 component queries (linear scans using
`dynamic_cast`), 5000 shader-status checks and 5000 compilation-listener checks
per frame. All cubes share one msl shader. Each frontend draw records two
commands, totaling 10000 plus outer state push/pop. No subset creations or base
rebindings occurred during the measured interval.

The bridge chain totals about **3.55 ms**. Its hot path uses cached pointers;
`data_variable::set/get` directly access values. These costs are not evidence
of variable-set allocation or mutex overhead. Status and listener checks do
acquire locks, but this profile does not isolate lock waiting from other work.

Useful next candidates are component lookup and repeated shared-shader status
queries within a safe frame phase, followed by redundant bridge transfers.
Camera values passed through the bridges are subsequently overwritten by the
camera update. Command construction deserves its own follow-up before changing
the command API. No optimization was implemented for this measurement.

Temporary probes were removed, source files restored byte-for-byte, the motor
working tree checked clean, and original binaries rebuilt and tested. Detailed
[measurements](../docs/results/scene-prepare-2026-10-22/breakdown-summary.csv),
[controls](../docs/results/scene-prepare-2026-10-22/controls.csv) and
[methodology](../docs/results/scene-prepare-2026-10-22/methodology.txt) were already
recorded with the profiling run. Raw frame samples and probe source remain in
`C:/Users/acons/Documents/motor help/scene-profile-20261022`.

## Initial verification (2026-10-05)

Windows Release, motor `cbed5cc`, i7-6700 (4 cores / 8 threads), RTX 2060,
driver 32.0.15.9621. Other desktop applications were open; these are exploratory
local measurements, not isolated or repeated performance claims.

- 100 objects, 0 / 2 lights: GL4, D3D11 and dual mode passed (5 warmup / 10 samples).
- 1000 objects, 0 / 2 lights: all three modes passed (5 warmup / 20 samples).
- 1000 objects, 8 lights: GL4 and D3D11 passed (5 warmup / 20 samples).
- 1000 objects, 2 lights: GL4 and D3D11 passed a longer run (120 warmup / 300 samples).
  Both backends were visually checked: the cube grid rendered with lighting.
- 5000 objects, 8 lights, GL4: hit the application watchdog after 300 seconds
  (requested 30 warmup / 60 samples). No valid timing row was produced. Shutdown
  completed with no tracked allocations remaining. The runner stopped there;
  the corresponding D3D11 and dual cases were not run.

Longer-run timings in milliseconds (one run per backend):

| Backend | Frame median | Frame p95 | Sync median | Transform median | Prepare median |
| --- | ---: | ---: | ---: | ---: | ---: |
| GL4 | 100.761 | 143.149 | 0.189 | 0.906 | 4.586 |
| D3D11 | 59.529 | 74.094 | 0.189 | 1.356 | 4.083 |

These runs produced the expected 3000 variable sets and 2000 expected draws per
frame. Logs and CSV are in `build/benchmark-results/20261005-224034-011`.
The frame/preparation gap needs profiling of backend execution, synchronization
and presentation; these timers cannot identify its cause or measure GPU cost.
The full default matrix has not been completed.

All completed verification runs reported an empty motor memory-manager dump.
GL shutdown still logs the known `wglMakeCurrent(00)` warning; no engine fixes
were made as part of this benchmark.

## Direct-path comparison (2026-10-05)

1000 objects, no lights, 120 warmup / 300 measured frames, one run per case:

| Backend / path | Sets | Frame median (ms) | Frame p95 (ms) | Prepare median (ms) |
| --- | ---: | ---: | ---: | ---: |
| GL4 / direct | 1000 | 17.385 | 27.321 | 0.079 |
| GL4 / scene | 2000 | 29.839 | 45.078 | 1.879 |
| D3D11 / direct | 1000 | 8.989 | 15.901 | 0.113 |
| D3D11 / scene | 2000 | 30.484 | 39.539 | 1.954 |

Direct dual mode also passed (26.750 ms median frame interval). Tracked memory
was clean after all these runs. Results are in the `20261005-225624-838` (direct)
and `20261005-225715-384` (scene) benchmark-results folders.

The GL4 scene result differs substantially from the earlier short unlit run.
Do not treat single-run timings as stable engine ratings. The comparison removes
scene traversal AND changes variable-set count/update behavior, so it does not
attribute the entire difference to visitors. Engine sources were not changed.

The direct path also completed 5000 objects on both backends (30 warmup / 300
samples, folder `20261005-225838-081`). Median frame intervals were 296.286 ms
(GL4) and 194.972 ms (D3D11), versus 0.476 / 0.639 ms for draw recording.
Both outputs were visually checked and both shutdown memory dumps were empty.
At that revision, scaling was poor even without scene traversal; those callback
timers did not identify a specific lower-level bottleneck. These are historical
single-run measurements, not the current performance of the direct path. See
the [latest comparison](../docs/D3D11_ASYNC_RENDERING.md#latest-alternating-backend-comparison)
for the subsequent improvements.
