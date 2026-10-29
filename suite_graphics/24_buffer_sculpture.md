# Buffer Sculpture

`24_buffer_sculpture` animates 20,000 cubes on the CPU and uploads their records
through motor's existing `array_object` / `frontend::update` API. The sculpture
morphs between a sphere, a thick spiral and a moving wave surface. The default
cycle holds each formation for four seconds, then blends for four seconds.

Unlike `07_vertex_pulling_field`, the vertex shader has no animation time input:
position, quaternion orientation, size and color are evaluated on the CPU.
The shader pulls those values and the shared cube mesh from two buffers.
There are no engine changes, compute shaders or external assets.

## Run

```powershell
cmake --build build --config Release --target 24_buffer_sculpture -- /m:2
build/bin/Release/24_buffer_sculpture.exe
build/bin/Release/24_buffer_sculpture.exe --d3d-only
build/bin/Release/24_buffer_sculpture.exe --dual
build/bin/Release/24_buffer_sculpture.exe --count 50000 --no-ui
build/bin/Release/24_buffer_sculpture.exe --dual --smoke
ctest --test-dir build -C Release -R graphics_buffer_sculpture_cpu --output-on-failure
```

Default: one gl4 window at 1920x1080, VSync requested on. `--d3d-only` selects
d3d11; `--dual` opens two 840x720 windows. d3d11 is Windows-only. `--no-vsync`
requests uncapped presentation; `--still` starts paused; `--no-ui` hides the
sample UI for recording. Closing either window closes the application.

The UI controls active cube count, formation, palette, cube size, animation
speed, camera yaw/elevation/span, pause and camera orbit. Cameras use
`motor::gfx::generic_camera`; no hand-written projection/view matrices.

## Buffer Contract

- Shared static mesh: 24 vertices, each containing position and normal (two float4s).
- Dynamic object record: position/scale, quaternion and color (three float4s, 48 bytes).
- One indexed scene draw per window, plus separate ImGui draws when enabled.
- This is batched vertex pulling, not hardware instancing. motor still requires
  an unused dummy VB and an index buffer sized for the capacity.
- `--count` sets capacity from 1 to 100,000. Active count changes do not resize it.
  Only the active prefix is animated; the tail remains initialized, and growing
  the active count refreshes all newly active records before drawing.
- Each update uploads the **whole object buffer**, not only the active prefix.
  At 20,000 cubes that is 960,000 bytes per update per backend, and 240,000
  submitted triangles per scene draw. Dummy VB + IB use 8,640,000 bytes per backend.
- `on_graphics` updates the shared CPU data after motor's synchronization step.
  Each window tracks its own uploaded revision, so the first window cannot
  consume an update needed by the second. Data is not changed in `on_render`.
- Pausing stops object uploads once the current revision is delivered. Changes
  to count, palette, formation or size trigger one new revision while paused.
  Camera-only changes do not require an object-buffer upload.

The UI reports CPU buffer-fill duration, a smoothed application frame interval,
and bytes/update calls queued per backend. These are **not GPU timings or
measured transfer bandwidth**. Initial configure uploads are not counted.
The two window counters are displayed from their latest render callback;
the secondary window's displayed counter can be one application frame behind.

## Verification

The default CTest is windowless. It checks finite records and normalized
quaternions at capacities 1, 17, 20,000 and 100,000, morph boundary continuity,
manual formations/palettes, changing CPU rotations and the float4 buffer stride.

`--smoke` waits for shader readiness and submits seven phases on every selected
backend: one object, quarter capacity, full-capacity spiral/waves, a paused
small count, a paused larger count, and full-capacity animation resumed.
It requires repeated uploads during animation and exactly one new upload per
paused phase. It exits automatically, with a 60-second internal timeout.
Normal cleanup releases the shader, geometry, both arrays and render states.
The program returns failure for an incomplete smoke or nonzero tracked memory.

The smoke checks lifecycle, submissions and update cadence, **not rendered
pixels or GPU readback**. Visual framing, colors and matching gl4/d3d11 output
need a separate visual check. Desktop automation is not part of this test.

### Checked on 2026-10-29

Windows / MSVC, motor `5a80485` (unchanged):

- Release and Debug targets built successfully.
- CPU CTest passed in Release and Debug. The existing render-state CPU CTest
  also passed in Release.
- Dual-backend smoke passed at 20,000 capacity in Release and Debug, at one
  object in Release, and at 100,000 capacity in Release with VSync requested off. Both GLSL and HLSL
  compiled successfully; all seven submission/update phases passed.
- Paused phases submitted exactly one object-buffer update per backend;
  animation phases submitted at least twelve. All successful smoke runs exited
  with code 0 and an empty motor memory dump.
- No visual verification or GPU readback was performed. No performance claim
  is derived from the short smoke runs.

The first shader attempt used `cross`, which the current msl generators leave
untranslated. The test now expands the quaternion cross products using swizzles
and the component-wise `'` operator. No engine change is required. GL's warning
about the unused dummy position attribute is expected for vertex pulling.
