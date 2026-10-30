# Village 1800

A scene-graph, procedural geometry and GPU-particle integration sample. Four adjoining
tiles in a 2 x 2 layout contain 16 stylized German half-timber houses, four cobbled
squares and four dragons. This is an architectural sketch
inspired by buildings around 1800, not a historically reconstructed location.

The tiles face 0, 90, 180 and 270 degrees. Two squares retain their roofed well;
the other two have a bronze town-guardian statue with cloak, shield and staff on
a stepped stone pedestal. Both centerpieces are procedural geometry, not assets.

## Build and Run

For square or portrait recordings, use `--youtube-short square|vertical`.
The HDR targets use the same capture resolution. See
[Short Video Capture](../README.md#short-video-capture) for dimensions and examples.

Light colors use RGB `vec3`. The frame synchronizes component inputs and runs
`trafo_visitor` before publishing transformed light directions/positions through
wire. Surface shader inputs are synchronized again after that publication. The
moon shadow camera uses the same transformed direction as the lighting shader.
The local getters remain useful for authoring the moon's orbit angles.

API migration verification (2026-10-18): Release build and
`--dual --smoke --detail low --particles 256` passed on GL4 and D3D11,
including all 160 frames per backend, moon orbit/shadow subsets and wire checks.
Exit 0, empty memory dump. No visual comparison; known GL4 diagnostics remain.

```sh
cmake --build build --config Release --target 15_village --parallel 2
```

Run `build/bin/Release/15_village.exe` on Windows or `build/bin/15_village` with a
single-configuration build. OpenGL is the default. No external models, textures,
downloads, or additional dependencies are required.

```sh
15_village --gl-only
15_village --d3d-only
15_village --dual
15_village --detail ultra --particles 65536
15_village --detail low --particles 2048
15_village --gl-only --smoke
```

`--detail low|high|ultra` controls roof tiles, cobbles and vegetation; `high` is
the default. `--particles` accepts 256 through 65536 seeds **per tile**; the default
is 8192 per tile, or 32768 total. Capture and display still use one draw each.
`--still` freezes simulation time initially. D3D11 and `--dual` require Windows.
`--tiles 1|4` selects one or four tiles (default four); one tile retains its
original location in the four-tile layout, without changing the camera.
`--no-vsync` requests disabled vsync. `--total-particles N` overrides the
per-tile seed option with a fixed total (1024..262144, divisible by four).

## One vs. Four Tiles Benchmark

The benchmark stays entirely in this sample; no engine changes are needed.
`--benchmark` requests vsync off, freezes animation/moon motion at the initial
pose, hides the ImGui panel, and exits after a bounded measurement. It rejects
`--dual` and `--smoke`: run each backend separately. By default it uses 180 warmup
frames, 600 measured frames, and 32768 **total** particle seeds for either tile
count. Override these with `--warmup`, `--frames`, and `--total-particles`.

```sh
15_village --gl-only --benchmark --tiles 1 --detail high
15_village --gl-only --benchmark --tiles 4 --detail high
15_village --gl-only --benchmark --tiles 1 --detail low
15_village --gl-only --benchmark --tiles 4 --detail low
```

Use `--d3d-only` for the other backend. All cases keep the same 1280x800 window,
camera, 2048x2048 moon shadow map, HDR/bloom/FXAA pipeline and active effects.
The post pipeline retains its 1920x1080 internal targets. Low detail changes
geometry density but preserves the material-batch/draw-call count for a given
tile count. The one-tile layout uses its well; the four-tile layout retains two
wells and two statues. Shared mesh resources are still built/configured once;
initialization and shader compilation are excluded from the steady-state samples.

The `VILLAGE_BENCH` log line reports:

- Frame mean, median, p95 and `1000 / mean` FPS, measured between successful
  `on_frame_done` submissions after warmup. This is application frame cadence,
  not a GPU timestamp or a claim that the GPU has completed each frame.
- `graphics_ms`: average wall time inside `on_graphics`, including graph input
  synchronization, transforms, lights, bridges and postprocess property setup.
- `prepare_ms`: average wall time inside `on_render`, including readiness checks,
  visitors, render-data updates and command recording for all passes. Backend
  command execution is not directly timed by this value.
- Geometry triangles per color pass (shadow pass submits them again), total
  particle seeds and draw submissions excluding UI.

Callback timings can include waits/locks; they are not CPU utilization samples.
The unaccounted frame time must not simply be labeled GPU time or backend time.
The sample does not insert `glFinish`, GPU queries, or a new synchronization fence.
Smoke-only redundant subset updates and expensive validation run outside the
measured frames. Subset IDs are captured once during warmup; the final run checks
their values and draw counts. Failure, timeout, or a memory leak yields nonzero exit.

Run the full matrix on Windows with the supplied script:

```powershell
.\suite_graphics\run_village_benchmark.ps1 -Executable <build>\bin\Release\15_village.exe
```

It defaults to both backends, three repeats and reversed case order on alternating
repeats. Each process runs alone. Timestamped output includes stdout/stderr logs,
`environment.json` and `results.csv`. Keep other graphics/build workloads stopped
and do not minimize/resize the test windows. Driver overrides or the compositor
can still affect presentation despite requesting vsync off.

This comparison changes scene-node, variable-set and draw counts together. It
can identify object-count sensitivity, but cannot by itself prove that the draw
API, a particular lock or a particular container is the cause. Total particle
count is fixed, but spatial coverage, particle categories/overdraw and scene
pixel coverage still differ. See [recorded comparison](15_village_benchmark.md).

The ImGui panel controls camera yaw, distance and height, orbit, pause, particles,
dragon visibility, fire breath, moon orbit/yaw/elevation/speed, moon shadows/bias,
postprocessing and emission. Geometry density and seed count are startup options
so camera movement does not trigger mesh construction or buffer reallocation.
Closing either window closes the application.

The `Light colors / intensity` section provides an HDR-capable linear RGB color
editor, an intensity slider (0 to 10) and an individual reset for Moon, Sky fill
and Fire (shared settings for all four fires). Initial values preserve the existing appearance; zero intensity turns
off that light's contribution. The panel scrolls when it exceeds the window height.
These controls change illumination, not the separate emissive window/particle
colors or the shader's constant ambient term. The existing Emission control
continues to adjust emissive surfaces.

## Scene and Rendering

- Four houses per tile with timber framing, individual roof tiles, chimneys, shutters,
  luminous windows, doors, foundations and steps. All four facades have windows;
  added detail includes shutter hinges/rivets, flower boxes, exposed eave brackets,
  tiled dormers on both roof slopes, supported porch roofs, door straps, wall
  lanterns and hanging trade signs.
- Rounded tile ends and hollow half-round ridge caps replace the box-like roof
  detail. Windows have raised mouldings and leaded panes; doors have panel trim
  and ring knockers. Barrels have bulging staves, lid seams and rounded hoops;
  the well has radial masonry, a winding drum, rope coils and crank. Wagon
  wheels have iron tires and sculpted hubs.
- Beveled individual paving stones, a roofed well, a cart with spoked wheels,
  barrels, stacked wood, fences, trees and grass.
- A shared msl surface shader evaluates cool directional moonlight, sky fill,
  a flickering local fire light, modest specular highlights and distance fog.
- motor's HDR postprocess pipeline provides bloom, tone mapping and FXAA.
- Each tile has four houses, ground, props, vegetation, dragon and a selected
  centerpiece. **45 shared geometry objects** are referenced by **166 material
  leaves** across the four tiles. These are
  separate scene-graph instances and draws, not GPU-instanced draws. Geometry
  and shader objects are created once; components and variable sets are per leaf.
- Tile dimensions are 33 x 33 world units; the complete ground covers 66 x 66.
  Square bases meet without gaps or overlap at each quarter-turn orientation.
  Camera defaults and shadow coverage are expanded for the complete layout.

### Materials

Previously the sample used vertex color plus a single shared surface response.
It now explicitly tags generated triangles with one of **11 material IDs**:
plaster, timber, roof/brick, stone, iron/brass, emissive glass/fire, foliage,
water, dragon scales, wing membrane, and horn/claws.

Startup splits each logical mesh by material, remapping indices and preserving
every triangle, normal, color and procedural joint tag. Every resulting mesh
has its own geometry link. Each tile's instance gets its own msl component and
base variable set, referencing that shared link. `u_material`
contains specular weight, Blinn-Phong exponent and an emission multiplier.
All material batches share one shader; they do not compile a shader per material.

Vertex colors still provide base color and small per-tile/per-leaf variations.
Materials add different surface responses, rather than replacing those colors.
There are no texture assets, PBR, transparency/refraction or normal maps here.
Water and glowing window panes remain opaque stylized surfaces. The UI shows
the material list and draw count per material with the dragon enabled.

### Scene Graph and Wire

The root owns village and dragon groups with four rotated/translated tile groups
each, light nodes and a small controls component. Each village tile has the
seven original object groups and two centerpiece groups; only the selected
well or statue group has material leaves. Each dragon tile has one object group.
Each object group owns material
leaves. Every object group and mesh leaf has a `trafo3d_component`; each leaf also
has an `msl_set_component` containing a surface component (pass 0) and a shadow
component (pass 1). Surface components share the managed surface shader;
shadow components share the managed depth shader. Each has its own base sets.

The light nodes own two global directional lights (moon/fill) and four point
lights (one fire per tile) through `light_component`. Fire nodes have the same
rotation and translation as their village tile. `light_collector_visitor` collects them every
frame. Color times intensity, direction, position and radius flow through Wire
outputs into the components' shader input slots. Each tile's shader evaluates
moon, fill and its own fire in one color draw per mesh, not additive light passes.
It does not evaluate the other tiles' fire lights. motor's
`light_pass_render_visitor` supplies the moon's shadow bindings for that draw;
the other two lights continue to arrive through Wire.

The current `gfx::light` API only exposes color/intensity getters. Editable
overrides therefore live in the sample, initialized/reset from the collected
light objects. The collection step publishes each override's `color * intensity`
through the existing energy slots, without replacing lights, recompiling shaders
or modifying the engine. Direction, position and radius still come from the lights.

`on_graphics` processes shader compilation listeners, publishes controls and
runs `variable_update_visitor` followed by `trafo_visitor`. Per-window rendering
uses `light_pass_render_visitor(0, view_id, frontend, camera, moon, shadow_data)`, which selects the camera's
render-data subset and binds world/view/projection and camera position.
Each dragon's flight transform belongs to its parent group and is inherited by
all four material leaves. Its shader only performs local wing/tail deformation.
The four synchronized flight paths are offset by their tile parents. Hiding
dragons skips traversal of their shared group,
without destroying the node or its resources.

Transform feedback and particle display remain explicit GPU passes surrounding
the scene rendering. The particle capture uses the same local flight pose as the
dragon's component. Particle seeds carry a separate tile offset/yaw attribute;
the capture shader rotates/translates the complete local particle motion after
evaluating smoke, sparks, motes and fire breath. Chimneys and dragon flames thus
follow their rotated tile, still using one capture draw and one display draw.
Postprocessing remains motor's existing HDR pipeline.

At default detail, shared geometry contains **729,215 vertices**.
Rendering all four tiles submits **1,285,296 triangles per scene pass**, plus
particle points and generated billboards. All architectural detail is geometry;
no normal maps or hidden imported assets are involved.

The UI and startup log report the generated counts for the selected detail
level. Centerpiece geometry is shared by the two tiles using that variant;
all other meshes are shared by all four tiles.

## Moon Shadows

The existing directional key light is now the moon. Shadow rendering uses only
motor objects and APIs: `generic_camera`, a depth-only `framebuffer_object`
(2048 x 2048, `depth32`), `state_object`, `msl_object`, msl components and visitors.
There are no raw OpenGL/D3D calls or additional rendering libraries in the sample.

The moon orbits independently of the viewing camera, initially at 3 degrees per
second. `Moon yaw` sets its azimuth; `Moon elevation` sets its angle above the
horizon (10 to 80 degrees). `Moon speed` accepts -15 to +15 degrees per second,
including reverse motion. Disabling `Moon orbit` holds the current direction;
reenabling it continues there without a jump. `Pause` and `--still` also stop
automatic moon motion. Manual angle changes remain available while paused.
The shared `on_graphics` update sets the directional light and aims the motor
shadow camera from that same direction before collecting lights and rendering
either window. The shadow map is redrawn from the updated camera each frame.
This animates the light, not a visible moon mesh or a day/night cycle.

Before color rendering, `render_visitor(1, view_id, frontend, moon_camera)` draws
the village and visible dragon into the depth map. `single_shadow_data` carries
the light projection/view and `village_moon.depth` texture name to the color
visitor, which uses the `light_projection`, `light_view` and `light_shadow_map`
bindings. Both backends configure their own resources from the shared objects.

The orthographic camera covers 128 x 128 world units, still using a single
2048 x 2048 map. This reduces texel density compared with the original single
tile. Casters are placed inside
both APIs' clip volumes despite the shared GL-style projection. The depth shader
explicitly writes `clip.z / clip.w * 0.5 + 0.5`, matching the receiver calculation.
`rt_texture` supplies motor's render-target UV convention. A 3 x 3 PCF kernel and
constant plus angle-dependent receiver bias soften the shadow and reduce acne.
Outside the shadow volume, the moon remains unshadowed.

The moon's diffuse/specular terms alone are shadowed, not sky fill, fire lighting
or emission. Particles neither cast nor receive shadows. Shared vertex-stage
source keeps wing/tail deformation identical in the depth and color passes.
The depth texture is still cleared when shadows are disabled, keeping bindings
valid. This is one fixed shadow map, not cascades, contact shadows or VSM.

## Dragon

Each dragon references the same 13,296 vertices / 6,232 triangles: a faceted body, head,
horns, glowing eyes, teeth, four clawed legs, a tapered tail and scalloped wing
membranes with raised fingers. It follows an elliptical flight path above the
square, oriented along its tangent. Wing rotation and tail deformation run in
the vertex shader; the CPU updates only frame variables, not vertex buffers.
Tail normals use the inverse transpose of the deformation Jacobian.

The surface shader is shared with the village. Scene components create one
render-data subset per view on demand. For this sample only,
negative vertex-color alpha values encode wing/tail regions; positive alpha
continues to encode emission. No imported rig or general skeletal animation is
involved. Collected light data is evaluated directly in msl.

One eighth of the particle seeds forms the optional fire breath, reusing the
capture/display shaders and their existing two draws. The flame is an analytic
jet attached to the current mouth pose, not persistent emitted-particle physics.
Hiding the dragon also suppresses the flame. Emissive eyes and flame contribute
to bloom, but do not add shadow maps or illuminate surrounding objects.

## Draw-Call Display

The panel reports draw submissions per window, split into scene, moon shadow, capture,
particles and postprocessing. Capture and particle calls increment their counts.
The scene count derives from the 150 ready village material leaves plus the 16
optional dragon material leaves, each submitting one draw through the render visitor. The
postprocess count is **13**, derived from the current pipeline:
brightpass, four downsample passes, four upsample passes, merge, tone mapping,
FXAA and presentation. Update this constant if the pipeline changes.

With everything enabled the total is **347** (166 shadow + 166 color + 1 capture +
1 particle + 13 postprocess). Hiding the dragons removes 32 draws while shadows
are enabled; disabling shadows removes the shadow draws. Disabling particles removes two; disabling postprocessing
removes thirteen. Disabling only fire breath changes particle output, not the
number of draws. ImGui draws are excluded. This is not a backend/GPU instrument:
the numbers count submitted work, not necessarily successful hardware draws.

## Transform Feedback / Stream Output

1. The CPU builds immutable particle seeds once: chimney origins, fire origin,
   ambient positions and deterministic random parameters.
2. An msl vertex shader evaluates time-dependent smoke, sparks and drifting motes.
3. A geometry shader removes particles below an opacity threshold and writes
   position/radius and color/opacity into a `streamout_object`. There is no pixel
   stage in this capture pass.
4. The display shader reads that stream. Its geometry stage expands each point
   into a camera-facing quad; its pixel stage produces a soft radial alpha mask.
5. Depth testing stays enabled, depth writes are disabled and particles use
   source-alpha blending. HDR sparks contribute to bloom.

The draw uses `feed_from_streamout` and `use_streamout_count`; the CPU does not
read back generated positions or determine the filtered output count. Motion is
analytic from seed plus time, not a persistent particle simulation with collisions
or feedback-based integration. Smoke is unsorted and deliberately low-opacity;
there is no soft-particle depth fade, volumetric scattering or particle shadowing.

## Resource and Frame Lifetime

Geometry and shader objects are shared across windows; scene components own base
data and separate render-data subsets per camera. Capture and particle shaders
retain their explicit per-window sets. `on_graphics` prepares those variables
before per-window `on_render` calls. `on_first_frame` configures resources and
`on_last_frame` releases backend resources. CPU owners survive until shutdown.
The app owns the managed shader lifecycle; the graph owns its nodes/components.
Cached component pointers are borrowed. Shutdown releases the graph before the
app's shader reference. No engine changes are required.

Postprocess properties are queried only when available: some stages create them
after shader compilation. The sample uses the existing pipeline's default
1920x1080 internal targets and presents to the current window size. Disabling
postprocessing is a diagnostic direct-HDR-to-window path, not a separately
color-calibrated LDR rendering mode.

## Verification and Limits

`--smoke` checks generated indices and finite positions/unit normals, waits for
shader and postprocess readiness, submits 160 frames with both particle passes
and HDR on every selected backend, and exercises shadows/fire off, dragon off, then all
on again in four 40-frame phases. At each phase boundary it checks six collected
lights, the expected number of variable sets, distinct view-subset IDs and their
world/view/projection, camera, clock, material, light-energy and fire parameters.
World matrices are checked against the tile rotation/offset and local model
transform. The test checks two uses per centerpiece geometry and four uses per
other geometry, the selected centerpiece per tile, and independently calculated
world positions of the four fire lights.
It also checks shadow-pass subsets, the light matrices on visible leaves, shadow
settings and matching animation clocks between color and depth. It
also checks directional-light values in every color subset, orbit motion,
holding the shadow camera while orbit is disabled, and manual yaw/elevation
changes followed by resumed motion. With `--still`, it checks frozen automatic
motion while still exercising the manual angle changes. It
changes all three lights' color/intensity settings, including HDR RGB values and
zero intensity, then resets them. Expected energy is checked directly against the
editable settings in every view subset, not just against the intermediate slots. It
checks submission counters including the
final 347 draws per window for the default four tiles, then closes normally. Automated runs have a 180-second
application timeout. Exit status is nonzero for a failed smoke or a nonempty
motor memory dump. Graphics tests are not added to headless CTest.

A passing smoke test verifies initialization, submission and teardown, **not**
pixel accuracy or the exact GPU-generated particle count. It is not a performance
benchmark: vsync is enabled and the ImGui FPS counter is not a GPU timer.

Rotated-tile/statue verification on 2026-10-18 (MSVC Release):

- `--dual --smoke` passed 160 frames per backend, including rotated transforms,
  fire positions, two wells/two statues, geometry sharing, light/wire subsets,
  shadow toggles and draw counts. Exit 0, empty memory dump.
- Normal detail: 729,215 shared vertices, 1,285,296 triangles per scene pass,
  32,768 seeds. Geometry indices and finite positions/unit normals also passed
  for both new centerpiece meshes.
- `--dual --smoke --still --detail low --particles 256` also passed 160 frames
  per backend with frozen automatic motion, exit 0 and an empty memory dump.
  The final Release build reported no compiler warnings.
- No pixel inspection or GPU timing was performed. The known WGL shutdown
  message and GL4 initialization/unused-attribute diagnostics remain.

Four-tile verification before rotations/statues on 2026-10-18 (MSVC Release):

- `--dual --smoke --detail low --particles 256`: 629,848 submitted triangles
  per scene pass, 1,024 total particle seeds, 160 frames per backend.
- `--dual --smoke`: 1,292,580 submitted triangles per scene pass, 32,768 total
  particle seeds, 160 frames per backend.
- Both passed transform, shared-geometry, light/wire, subset, shadow-toggle and
  draw-count checks, exited with code 0 and empty motor memory dumps. The build
  reported no compiler warnings. Existing GL4 initialization/unused-attribute
  diagnostics and the WGL shutdown message remain. No pixel/UI inspection was
  performed. Neither runtime nor GPU speed was benchmarked.

The following historical runs used the original single-tile layout.

Local verification on 2026-10-14 (MSVC Release, NVIDIA RTX 2060):

| Run | Result |
| --- | --- |
| GL4, low, 256 seeds | 160 frames, scene/material/Wire/shadow subset and toggle/draw checks, exit 0, empty dump |
| GL4 + D3D11, high, 8192 seeds | 160 frames per window, scene/material/Wire/shadow subset and toggle/draw checks, exit 0, empty dump |
| D3D11, ultra, 65536 seeds | 160 frames, scene/material/Wire/shadow subset and toggle/draw checks, exit 0, empty dump |

Moon-orbit verification on 2026-10-15 (MSVC Release):

- `--dual --smoke`: high detail, 8192 seeds, 160 frames per backend; orbit,
  stop/resume, manual angles and light/shadow subset checks passed.
- `--dual --smoke --still --detail low --particles 256`: frozen automatic
  motion and manual angle changes passed on both backends.
- Both runs exited with code 0 and empty motor memory dumps. The targeted build
  produced no compiler warnings. The existing GL4 unused-normal and shutdown
  `wglMakeCurrent(00)` messages remain. These are data/submission checks, not
  a visual comparison of moving shadows.

Light-control verification on 2026-10-15: Release build and `--dual --smoke`
passed with 160 frames per backend. The test changes colors/intensities, turns
fill/fire contributions off via zero intensity and restores defaults, checking
every color subset against the control values. Exit 0, empty memory dump; the
existing WGL shutdown message remains. UI interaction was not automated.

The targeted shadow build reported no compiler warnings. These runs use motor's
original locking implementation, without the earlier local `datas` experiment.
No engine changes are required for this sample.
Automated screenshot access was not authorized, so visual correctness still
needs human inspection. Linux runtime behavior has not been tested for this sample.

The local GL4 run logged `Could not find image []` and
`Could not find image [gfx.postprocess.fb.5.0]` during pipeline initialization,
plus `wglMakeCurrent(00)` during shutdown. The pipeline source labels the latter
texture slot as unused. These messages were not suppressed or fixed in the
engine as part of this sample. D3D11 completed without those messages.
GL4 also reports unused normal attributes for the depth shader: its compiler
optimizes those away, while the shader intentionally reuses the surface geometry.

Source split:

- [Application](15_village.cpp): resources, lifecycle, camera, UI and draws.
- [Geometry](village/geometry.hpp): procedural mesh construction.
- [Dragon](village/dragon.hpp): body, wings, tail and procedural joint tags.
- [Shaders](village/shaders.hpp): shared surface/depth vertex stage, moon shadow,
  surface lighting, capture and particle display msl.

### Notification wakeup validation

The [2026-10-18 wakeup report](../docs/VILLAGE_D3D11_WAKEUP.md) compares the
current notification-based carrier against the earlier sleep/no-sleep results.
It includes process CPU, individual frame times, p99/outliers, multi-window
smokes and a separate idle-carrier test. No engine changes were made for it.
`MOTOR_VILLAGE_FRAME_CSV` optionally selects an individual-frame CSV output path;
the file is written only after benchmark timing has finished.

### App-thread sleep comparison

`--cpu-sleep-us 0..30` sets the existing `carrier::set_cpu_sleep` API before
threads start. In benchmark mode, `--sleep-probe` additionally calibrates the
same conditional `std::this_thread::sleep_for(get_cpu_sleep())` call 128 times
on the app thread **after** the frame/CPU measurement. This is a separate probe,
not instrumentation of the engine's internal sleep calls. With a frame CSV
path set, probe samples are saved to that path plus `.sleep.csv`.

The [CPU sleep comparison](../docs/VILLAGE_CPU_SLEEP.md) records the results for
0, 1, 10 and 30 microseconds, including frame-time percentiles and process CPU.
The test does not change timer resolution, thread priority or engine code.

### Update-wait and multiple windows

`--benchmark --dual` now measures the existing GL4+D3D11 two-window mode. Warmup
and sampling require successful scene submission to **both** windows. Each
window is checked for its expected draw count, and `VILLAGE_VIEW` reports its
own submission cadence. The frame CSV adds `window0_ms` and, in dual mode,
`window1_ms`; `prepare_ms` is the sum of both per-window recording durations.

In dual mode `VILLAGE_BENCH` reports the shared app-frame cadence and total
draws across the two windows, not twice the FPS. These remain CPU-side timings,
not GPU completion or presentation measurements. See the
[update-wait comparison](../docs/VILLAGE_UPDATE_WAIT.md) for measurements and
`diagnostics/run_village_update_wait.ps1` for reproduction.

### VAO lookup comparison

The [GL4 VAO lookup report](../docs/VILLAGE_VAO_LOOKUP.md) compares the linear
search against a temporary direct index cache with one and four tiles. At the
village's 43-45 VAO entries per render object, no consistent speedup was measured.
The report includes paired backend-frame timings, cache-hit coverage and raw
results. The application and engine remain unchanged after the experiment.
