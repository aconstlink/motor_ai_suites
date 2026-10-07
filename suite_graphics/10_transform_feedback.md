# Transform Feedback / Stream Output

`10_transform_feedback` captures a 50x50x50 lattice of cube centers into a GPU
buffer, then renders only the captured output as shaded cubes. Windows defaults
to one GL4 and one D3D11
window; other platforms default to GL4. No changes to Motor are required.

## Run

```powershell
cmake -S . -B build
cmake --build build --config Release --target 10_transform_feedback --parallel 4
build/bin/Release/10_transform_feedback.exe
build/bin/Release/10_transform_feedback.exe --smoke
```

Options: `--gl-only`, `--d3d-only`, `--still`, `--smoke`, `--help`.
Closing either test window closes the application.

## Modes

- **VS capture:** the vertex shader animates color and captures
  all 125,000 records, without a user geometry shader or pixel shader.
- **GS keep all:** the geometry shader emits one point for every input point.
- **GS keep none:** it emits nothing. The display must be empty, including after
  a frame that filled the buffer completely.
- **GS moving cut:** the geometry shader keeps only points satisfying
  `dot(plane.xyz, position.xyz) <= plane.w`. The plane rotates, tilts and translates
  in three dimensions.

Pause freezes the animation and enables convenient manual adjustment of angle,
tilt and offset. Camera yaw is independently adjustable. In the moving-cut mode,
the Plane checkbox shows/hides a depth-tested gold grid and a red normal arrow.
The arrow points toward the rejected half-space. The grid is a finite illustration
of the infinite plane; it shares the filter's normal and offset. Its tangent frame
also works when the normal points straight up or down.

Filtering removes whole cubes based on their centers. This is not triangle
clipping or a capped mesh cross-section: a retained cube can straddle the plane.
Captured position and color each occupy a vec4. The buffer is sized
for 125,000 records because these capture shaders never amplify their input.
The display geometry shader expands each captured point into six separate
four-vertex triangle strips: 24 emitted vertices and 12 triangles per cube,
up to 1,500,000 cube triangles per window. Directional face shading and depth testing
make the volume readable. It does not filter or synthesize missing point records.
The plane grid is a separate line draw, not part of the captured output.

Centers are spaced 0.42 units apart and cubes are 0.21 units wide, leaving a
0.21-unit gap between neighboring faces. The field grows rather than squeezing
more cubes into the old bounds. Camera framing, far plane, plane grid, normal
arrow and cut-offset controls scale with the field extent.

## Render Sequence

1. Configure input/plane geometry, streamout object, state and four MSL objects.
2. `use(output)` selects the capture target.
3. Render the VS-only writer or VS+GS writer. Neither has a pixel shader.
4. `unuse(streamout)` ends capture and exposes its output for reading.
5. Render the display shader with `feed_from_streamout = true` and
   `use_streamout_count = true`.
6. Optionally render the plane grid/normal with the same Motor camera matrix.

The display MSL links both `tf_input` and `tf_output`. Its actual vertex data and
draw count come from streamout, not from the original geometry. The inspected
backend paths use `glDrawTransformFeedback` and D3D11 `DrawAuto`, respectively.
This avoids the separate D3D11 query/readback path used when requesting only
the streamout count while feeding a different geometry.

All windows share the logical resources but have separate variable sets and
backend storage. Variables are prepared in `on_graphics`, not overwritten
between asynchronous draws. Release runs in `last_frame` and immediately returns.

## Verification

The 3D smoke test runs nine phases, each requiring 90 capture/display submissions
per window: VS all, GS all, X half-space, none, all again, Z half-space,
Y half-space (vertical normal), oblique half-space, VS all again.
The CPU reference counts are 125000, 125000, 62500, 0, 125000, 62500, 62500, 62500, 125000.
These counts are displayed/logged only and never control a GPU draw.

The 50x50x50 version built successfully and passed all nine dual-backend smoke phases
on 2026-10-06 with exit 0 and an empty Motor memory dump. All four MSL shaders
compiled for GL4 and D3D11. Visual verification of the cubes and plane is still
pending; UI access for this test was previously not approved and was not retried.
The known `wglMakeCurrent(00)` shutdown diagnostic remains. Timeout is 120 seconds.
This smoke checks readiness and command flow,
not the actual GPU output count or buffer contents. Pixel verification remains
manual; this is not a performance benchmark or a particle simulation.

The test uses named geometry-shader input indices (`in[i]`) to avoid the MSL
literal-index translation issue found in the geometry-shader sample.
