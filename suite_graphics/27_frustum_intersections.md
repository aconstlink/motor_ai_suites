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
rays and points. After each 48-second cycle it switches between perspective and
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
list. `--smoke` optionally runs twelve bounded submission phases (six modes in
each projection), waits for execute fences from every window, then closes.
It verifies command execution, not pixel correctness or shader compilation success.
Graphics smoke mode opens windows and is **not** part of the registered CTest tests.

No visual verification was performed when adding this test. For a manual check,
watch an object cross a side or far plane: its classification may turn orange,
then it must disappear in both views. Resizing must keep the observer's frustum
and the first view's actual projection consistent on both backends.
