# Village 1800

A scene-graph, procedural geometry and GPU-particle integration sample. Four stylized German
half-timber houses surround a cobbled square. This is an architectural sketch
inspired by buildings around 1800, not a historically reconstructed location.

## Build and Run

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
the default. `--particles` accepts 256 through 65536 seeds; the default is 8192.
`--still` freezes simulation time initially. D3D11 and `--dual` require Windows.

The ImGui panel controls camera yaw, distance and height, orbit, pause, particles,
dragon visibility, fire breath, moon orbit/yaw/elevation/speed, moon shadows/bias,
postprocessing and emission. Geometry density and seed count are startup options
so camera movement does not trigger mesh construction or buffer reallocation.
Closing either window closes the application.

## Scene and Rendering

- Four houses with timber framing, individual roof tiles, chimneys, shutters,
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
- Geometry belongs to eight logical groups: four houses, ground, props,
  vegetation and dragon. It is split into **41 material batches** (37 village,
  four dragon). Each material batch is a scene leaf; there is no instancing.

### Materials

Previously the sample used vertex color plus a single shared surface response.
It now explicitly tags generated triangles with one of **11 material IDs**:
plaster, timber, roof/brick, stone, iron/brass, emissive glass/fire, foliage,
water, dragon scales, wing membrane, and horn/claws.

Startup splits each logical mesh by material, remapping indices and preserving
every triangle, normal, color and procedural joint tag. Every resulting mesh
has its own geometry link, msl component and base variable set. `u_material`
contains specular weight, Blinn-Phong exponent and an emission multiplier.
All material batches share one shader; they do not compile a shader per material.

Vertex colors still provide base color and small per-tile/per-leaf variations.
Materials add different surface responses, rather than replacing those colors.
There are no texture assets, PBR, transparency/refraction or normal maps here.
Water and glowing window panes remain opaque stylized surfaces. The UI shows
the material list and draw count per material with the dragon enabled.

### Scene Graph and Wire

The root owns a village group containing seven object groups, a dragon group,
a light group and a small controls component. Each object group owns material
leaves. Every object group and mesh leaf has a `trafo3d_component`; each leaf also
has an `msl_set_component` containing a surface component (pass 0) and a shadow
component (pass 1). Surface components share the managed surface shader;
shadow components share the managed depth shader. Each has its own base sets.

The light group owns two directional lights (moon/fill) and one point light
(fire) through `light_component`. `light_collector_visitor` collects them every
frame. Color times intensity, direction, position and radius flow through Wire
outputs into the components' shader input slots. The shader evaluates all three
lights in one color draw per mesh, not three additive light passes. motor's
`light_pass_render_visitor` supplies the moon's shadow bindings for that draw;
the other two lights continue to arrive through Wire.

`on_graphics` processes shader compilation listeners, publishes controls and
runs `variable_update_visitor` followed by `trafo_visitor`. Per-window rendering
uses `light_pass_render_visitor(0, view_id, frontend, camera, moon, shadow_data)`, which selects the camera's
render-data subset and binds world/view/projection and camera position.
The dragon's flight transform belongs to its parent group and is inherited by
all four material leaves. Its shader only performs local wing/tail deformation.
Hiding it skips traversal of its group,
without destroying the node or its resources.

Transform feedback and particle display remain explicit GPU passes surrounding
the scene rendering. The particle capture uses the same flight pose as the
dragon's component. Postprocessing remains motor's existing HDR pipeline.

The default scene contains **725,333 vertices and 323,145 triangles**, plus the
particle points and generated billboards. All architectural detail is geometry;
no normal maps or hidden imported assets are involved.

| Detail | Vertices | Triangles |
| --- | ---: | ---: |
| low | 347,416 | 157,462 |
| high (default) | 725,333 | 323,145 |
| ultra | 1,515,487 | 675,559 |

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

The orthographic camera covers 64 x 64 world units. Casters are placed inside
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

The dragon contributes 13,296 vertices / 6,232 triangles: a faceted body, head,
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
The scene count derives from the 37 ready village material leaves plus the four
optional dragon material leaves, each submitting one draw through the render visitor. The
postprocess count is **13**, derived from the current pipeline:
brightpass, four downsample passes, four upsample passes, merge, tone mapping,
FXAA and presentation. Update this constant if the pipeline changes.

With everything enabled the total is **97** (41 shadow + 41 color + 1 capture +
1 particle + 13 postprocess). Hiding the dragon removes eight draws while shadows
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
on again in four 40-frame phases. At each phase boundary it checks three collected
lights, the expected number of variable sets, distinct view-subset IDs and their
world/view/projection, camera, clock, material, light-energy and fire parameters.
It also checks shadow-pass subsets, the light matrices on visible leaves, shadow
settings and matching animation clocks between color and depth. It
also checks directional-light values in every color subset, orbit motion,
holding the shadow camera while orbit is disabled, and manual yaw/elevation
changes followed by resumed motion. With `--still`, it checks frozen automatic
motion while still exercising the manual angle changes. It
checks submission counters including the
final 97 draws per window, then closes normally. It has a 75-second
application timeout. Exit status is nonzero for a failed smoke or a nonempty
motor memory dump. Graphics tests are not added to headless CTest.

A passing smoke test verifies initialization, submission and teardown, **not**
pixel accuracy or the exact GPU-generated particle count. It is not a performance
benchmark: vsync is enabled and the ImGui FPS counter is not a GPU timer.

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
