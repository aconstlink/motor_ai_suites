# Frustum Intersections

`27_frustum_intersections` exercises motor's frustum/sphere, frustum/AABB,
frustum/OBB, frustum/ray and frustum/point tests with `generic_camera` and
`primitive_render_3d`. No external models, custom backend code or engine edits
are needed. Requires the motor version containing these intersection functions
and `primitive_render_3d::draw_frustum(math::frustum3f)`. For d3d11, it also
requires the clip-depth conversion in the engine's 3D triangle and line shaders.

## Two Views, One Culling Decision

The first view renders through the **same camera whose matrices produce the
tested frustum**. The second view observes the scene and that frustum from above.
The test classifies 63 objects once per window, then both views consume the
same visible-index list. There is no independent observer culling and no
outside-object ghost mode. Rejected objects are not submitted in either view.

The additional **Scene bounds** mode uses a real motor scene graph rather than
the flat primitive list. See [Scene-Graph Bounds](#scene-graph-bounds) below.

The ground/grid and the observer's camera/frustum guides are debug context,
not members of the culled object list. Hardware clipping and occlusion naturally
still affect the final pixels of each view differently.

- Green: fully inside; for points, the boundary also counts as inside.
- Orange: intersecting or potentially intersecting.
- Outside: omitted entirely, including bounds and markers.
- Rays: draw the origin, first surface hit (white), and a short continuation.
  The mathematical ray is infinite, not a finite line segment. A ray starting
  inside the frustum reports its exit; its origin can be outside and still hit.

The three volume tests are conservative six-plane tests, not exact SAT or
closest-feature overlap tests. `outside` is a definite rejection, but objects
near corners can pass without actually overlapping. The test retains them.
Sphere tests require the normalized outward planes produced by motor's frustum.

## Run

For large scenes, see [Culling Stress Mode](#culling-stress-mode): 10k to 200k
scene nodes, three culling strategies and individual cube draw calls.

```powershell
cmake --build build --config Release --target 27_frustum_intersections --parallel 2
.\build\bin\Release\27_frustum_intersections.exe --gl-only
.\build\bin\Release\27_frustum_intersections.exe --d3d-only
.\build\bin\Release\27_frustum_intersections.exe --dual
```

Default: one gl4 window, 1280 x 800. `--dual` opens separate gl4/d3d11 windows,
each with both camera views. d3d11 requires Windows.

The initially collapsed controls expose test selection, perspective/orthographic
projection, pause, demo, bounds, animation speed, camera aim, field of view and
far plane. Geometry is filled and shaded on the CPU for readability, with optional
wire bounds; this is an intersection/debug-renderer test, not a lighting benchmark.

## Automatic Demo and Shorts

```powershell
.\build\bin\Release\27_frustum_intersections.exe --gl-only --demo --youtube-short vertical
.\build\bin\Release\27_frustum_intersections.exe --d3d-only --demo --youtube-short square
```

`--demo` cycles every eight seconds through mixed volumes, spheres, AABBs, OBBs,
rays, points and scene bounds. After each 56-second cycle it switches between perspective and
orthographic culling. Objects move, OBBs rotate, and the culling camera sweeps
the field. `--still` freezes time; `--no-vsync` disables the requested VSync.

Landscape places the views side by side; square and vertical stack them.
Viewport and scissor rectangles are updated on resize. The culling projection
uses the actual first viewport aspect, not the full-window aspect. The observer
fits the field and all eight frustum corners in its own viewport.

Both backends receive the original `camera.mat_proj()` for solid primitives,
wire bounds and observer guides. Culling uses the same camera convention,
with NDC depth in `[-1,+1]`. The engine's 3D primitive HLSL shaders convert the
final clip-space depth once with `z' = (z + w) / 2`; gl4 leaves it unchanged.
The test does not modify projection matrices for d3d11. Applying the former
test-side conversion as well would convert depth twice.

Shorts use the shared 1080 x 1080 / 1080 x 1920 presets. In capture demo mode,
controls are hidden while camera headings and counts remain; `--no-ui` hides all
sample ImGui overlays. Recording itself is external, for example through OBS.

## Verification

Verified against motor `2f026fe`, including the clip-depth conversion in both
3D primitive HLSL shaders. Debug and Release builds succeeded, and all five
registered windowless tests passed in each configuration with the test-side
projection conversion removed. These checks verify CPU logic and build
compatibility; they do not execute or visually validate the HLSL shaders.

```powershell
.\build\bin\Release\27_frustum_intersections.exe --cpu-only
ctest --test-dir build -C Release -R "frustum_intersections" --output-on-failure
```

Windowless checks cover:

- All six rejection planes, internal/external tangency and enclosing volumes.
- The documented conservative sphere/corner false positive.
- Point boundaries and ray entry/exit, non-unit directions, parallel misses,
  backwards rays, corner tangency and zero direction.
- Rotated OBB support-radius results against an eight-corner plane reference.
- Animated visibility lists for every test mode and both projection types.
- Fully empty visibility lists and restoration without stale object IDs.
- Landscape, square, vertical and small-window layouts and observer framing.
- The shared motor camera's `[-1,+1]` near/far depth convention for perspective
  and orthographic projections. This CPU check does not execute HLSL or validate
  the engine shader conversion.
- Shorts CLI parsing, including invalid and missing format arguments.

At runtime the two views' submitted object IDs are compared to the shared visible
list. `--smoke` optionally runs fourteen bounded submission phases (seven modes in
each projection), waits for execute fences from every window, then closes.
It verifies command execution, not pixel correctness or shader compilation success.
Graphics smoke mode opens windows and is **not** part of the registered CTest tests.

No visual verification was performed when adding this test. For a manual check,
watch an object cross a side or far plane: its classification may turn orange,
then it must disappear in both views. Resizing must keep the observer's frustum
and the first view's actual projection consistent on both backends.

## Scene-Graph Bounds

Requires motor `1facae7` or newer, including `bounds_component` and the bounds
bubble-up in `trafo_visitor`. No engine modifications are part of this test.

```powershell
cmake --build build --config Release --target 27_frustum_intersections 28_scene_bounds --parallel 2
.\build\bin\Release\27_frustum_intersections.exe --gl-only --scene-bounds
.\build\bin\Release\27_frustum_intersections.exe --d3d-only --scene-bounds
.\build\bin\Release\27_frustum_intersections.exe --scene-bounds --demo --youtube-short vertical
ctest --test-dir build -C Release -R "scene_bounds_bubble_up|frustum_intersections" --output-on-failure
```

The graph contains a root, three clusters, nine rows and 63 box leaves. Each leaf
has local bounds and a transform component. Animated translations, rotations and
nonuniform scales are applied to the nodes. The engine's `trafo_visitor` computes
world bounds and propagates their union back up the tree. Rendered box corners
come from the leaf's computed transform, not from its world AABB.

The culling path checks the root, clusters, rows and then leaf world AABBs.
A rejected group skips its descendants. Both cameras still render exactly the
same accepted leaf list. The existing six intersection modes are unchanged.

- **Bounds:** pale world-AABB outlines on visible leaves in both views.
- **Group bounds:** observer-only gold cluster boxes and blue row boxes.
- **Root bounds:** optional pink observer-only outline of the whole subtree.
- Rejected group outlines turn gray. These are debug guides, not ghost geometry;
  rejected leaf geometry and leaf outlines remain absent in both views.

Use the **Test** dropdown to select **Scene bounds**. The controls also show leaf
test and rejected-group counts. `--scene-bounds --demo` stays on this mode while
animating; ordinary `--demo` includes it in the seven-mode cycle. Shorts layouts
and GL4/d3d11 selection work as before. Empty bounds are never drawn as boxes.

`28_scene_bounds` is a separate windowless regression test using the same graph
fixture, real components and the real visitor. It checks:

- AABB unions, negative coordinates, empty unions and the returned reference.
- Off-center bounds, nonuniform/mirrored scales and move construction.
- World bounds against independently transformed eight-corner references.
- Nested aggregation, repeated traversals and component reuse.
- Hierarchical culling against a flat leaf-AABB reference in both projections.
- Shrinking bounds, changed local bounds, late attachment and removed children.
- Empty subtrees, a 24-level hierarchy and visitor reuse on a standalone leaf.

The tests cover rigid/affine object transforms, not skinning or morph targets.
Groups are containers here; unbounded leaves carry no renderable geometry.

Verified on Windows against motor `1facae7`: both executables build in Debug and
Release. All six selected CTests pass in each configuration. The bounds executable
reports **2,543 checks, zero failures**, with no remaining memory-manager entries.
No graphics windows were opened and no visual/backend rendering verification was
performed for this addition. Linux CI registration is included but was not run
locally.

### Engine Finding

At motor `1facae7`, instantiating `node::remove_component<bounds_component_t>()`
fails to compile: `node.h` calls `motor::release(comp)` with a raw component
pointer, while the available overloads require `mtr_safe`. This test does not
change engine code or instantiate that broken API. Child detachment/removal and
late bounds attachment are tested instead; component removal needs a follow-up
regression once the engine API is fixed.

## Culling Stress Mode

`--stress` selects a separate large-scene path in this executable. The original
intersection and scene-bounds modes are unchanged. No engine changes are needed.

```powershell
27_frustum_intersections --stress --objects 10000 --gl-only --no-vsync
27_frustum_intersections --stress --objects 100000 --d3d-only --no-vsync
27_frustum_intersections --stress --objects 200000 --culling hierarchy --demo --youtube-short vertical
27_frustum_intersections --stress --objects 10000 --animate-percent 25 --dual
27_frustum_intersections --stress --cpu-only --objects 100000
```

Start with **10k in Release**. The UI offers 10k, 50k, 100k and 200k objects;
`--objects` accepts any count from 1 to 200000. Larger scenes intentionally put
pressure on node allocation, variable sets and individual draws. No-culling at
200k means **400k cube draws per window** across the two views, not instancing.
It can be very slow and memory-intensive. This is not a promised real-time limit.

### Scene and Culling

Every cube is a real scene leaf with a `trafo3d_component` and `bounds_component`.
Spatial chunks contain up to 64 leaves (4 x 4 x 4); sectors contain up to 16
chunks. The real `trafo_visitor` computes world transforms and bubbles bounds up
to the root. Test-side records borrow component pointers and child ranges from
this fixed graph; they do not replace the engine's bounds calculations.

Choose `--culling none|flat|hierarchy` or use the UI:

- **No culling:** retain all objects without any frustum tests.
- **Per-object AABB:** test every leaf's world AABB.
- **Hierarchical AABB:** test root, sectors and chunks; skip outside subtrees.
  Fully contained groups append their leaf ranges without further plane tests.

All modes use the same deterministic field and camera path. The culling camera
moves through the field; the observer fits the scene and culling frustum to its
own viewport. Both views consume the exact same ordered visible-ID list. The
observer does not independently cull objects. Guides and optional bounds are
debug context and are not part of the culled geometry count.

Objects are static by default: bounds are calculated once, while the camera
continues moving. `--animate-percent` or the UI animates a prefix of the objects
with vertical motion. In that mode the full transform/bounds visitor runs again;
the displayed time includes both pose changes and traversal. Returning to zero
restores the original poses and updates bounds once more. No dirty-subtree
optimization is implied.

### Rendering and Timings

One shared cube mesh and one msl shader per window render the retained objects
with simple directional shading. There are no shadows, textures or postprocessing.
Rendering uses explicit per-object frontend commands, **not** the scene's
`render_visitor` or msl component bridges. The stress path isolates hierarchical
bounds/culling plus individual graphics submissions; it is not the full scene
material/lighting pipeline.

Each view has independent draw-variable slots containing MVP and color. Slots
grow to the largest retained count and are reused afterwards, including across
mode/count changes. They are not allocated anew each frame. Initial growth and
backend linking are warm-up costs; wait before comparing steady-state values.
Memory can remain at the previous high-water mark until the application exits.

The controls report:

- Group/leaf tests, rejected/fully accepted groups and objects submitted per view.
- Cube draw commands across both views, excluding debug guides and ImGui.
- Scene construction, animated transforms/bounds, culling, draw-variable filling,
  debug-geometry preparation and frontend command-recording times.
- Rolling median/p95 of the last 240 `on_graphics` intervals, measured on the CPU.
  These are not GPU timings or display-present intervals. Async phases overlap,
  so the individual timings must not be added to predict the frame interval.

Use one backend at a time, Release, `--no-vsync`, the same object count and
camera pose, and disabled bounds overlays for comparisons. Pause the camera and
set animation to zero to compare modes on identical data.

- Enable **Group bounds**, then choose **Limited (256)** or **All**. This displays
  chunk-group bounds in the observer, including rejected chunks in gray.
- Enable **Leaf bounds**, then choose **Limited (128)** or **All visible**. This
  displays retained objects' bounds in both views, never rejected leaf objects.
- Limited selections are evenly distributed across the complete chunk or visible
  leaf index range, including its first and last entry. The UI reports shown/total
  counts. Culling itself always uses the complete hierarchy.
- **All** can add substantial debug-geometry work and is intended for inspection,
  not clean performance measurements. The default remains **Limited**.

**Cube draws (both views)** counts cube render commands per window across the two
views: 6,086 retained objects mean 12,172 cube draws when the shader is ready.
Debug bounds, guides and ImGui are excluded; this is not a total draw-call counter
or a count of objects that actually produce visible pixels.

`--demo` provides a clean capture presentation with controls hidden in square
or vertical format; the camera already moves automatically. It does not cycle
into the expensive no-culling mode. `--no-ui` hides sample overlays entirely.

### Stress Verification

```powershell
ctest --test-dir build -C Release -R "graphics_culling_stress" --output-on-failure
27_frustum_intersections --stress --cpu-only --objects 200000
```

Windowless checks compare exact visible-ID lists across flat and hierarchical
culling at multiple camera poses, viewport aspects and animation states. They
also check partial chunks/sectors, child-bound containment, full-root acceptance,
empty visibility, restoration and no-culling ordering. Both all/limited group and
leaf selections are checked for correct counts, full index-range coverage and
absence of duplicate or out-of-range entries, including empty inputs and 200k entries.
The CPU run logs median
and p95 for 30 warmed-up culling iterations; this excludes rendering and upload.

Optional `--stress --smoke` opens graphics windows and runs all three modes at
1025 objects unless a count is explicitly supplied. It checks shader readiness
and execute fences, not pixel correctness. It is not registered as an automatic
CTest test. No visual tests are required for the windowless regression suite.

Verified on Windows on 2026-11-05: Debug and Release builds succeeded, with all
six selected stress/frustum CTests passing in each configuration. A separate
Release `--cpu-only --objects 200000` run passed all checks and ended with zero
memory-manager entries. No graphics windows were opened: runtime shader
compilation and the rendered appearance on gl4/d3d11 remain to be checked manually.

That single local CPU run measured the following fixed-camera case (5 warm-up
iterations, 30 measured iterations, no rendering or uploads):

| Mode | Retained IDs | Leaf tests | Group tests | Median | p95 |
| --- | ---: | ---: | ---: | ---: | ---: |
| No culling | 200,000 | 0 | 0 | 0.281 ms | 0.328 ms |
| Per-object AABB | 4,866 | 200,000 | 0 | 8.673 ms | 9.341 ms |
| Hierarchical AABB | 4,866 | 9,088 | 1,589 | 0.373 ms | 0.577 ms |

Flat and hierarchical paths returned identical ordered IDs. No-culling measures
only filling the full ID list, not the substantial cost of drawing that list.
These are diagnostic CPU measurements for this layout and pose, not end-to-end
frame times or a general performance guarantee.
