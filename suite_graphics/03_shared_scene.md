# Shared Scene Regression Test

## Motor Camera Update

The manual view/projection construction has been replaced with
`motor::gfx::generic_camera`. Release builds and dual-backend smoke runs
with dynamic sets and with `--eager` passed, including secondary-window
close/reopen. Both memory dumps were empty. The camera change has not been
visually reverified; the smoke tests do not compare pixels.

## Earlier Retest

Motor 5f8f05b plus the user's local GL4/D3D11 MSL release implementations:
Release build passes, and `--smoke` now completes the dual-window lifecycle
with exit 0 and no remaining Motor allocations. The previous access violation
was not reproduced in this run. Shutdown still logs `GetClientRect failed`
and `wglMakeCurrent(00)`. Pixel correctness was not inspected by the agent.
The earlier verification notes below are historical.

Three cubes share one geometry and one MSL object. Each cube has its own
variable set. Sets are drawn in order 3, 0, 2; set 1 is removed before use.
Animation is updated once in on_graphics, then rendered in both windows.
View and projection come from `motor::gfx::generic_camera`, using
`make_orthographic` and `look_at` from (0, 0, 6) toward the origin.
The fixed 4:3 framing is intentionally shared across windows, as are the
per-object variable sets. Object rotation/translation matrices are separate
from the camera and remain part of the regression's animation data.

## Run

Build target: `03_shared_scene` (Release or Debug).

- No arguments: OpenGL 4 and D3D11 windows on Windows; GL only elsewhere.
- `--gl-only` or `--d3d-only`: isolate one backend.
- `--eager`: create variable sets before initial configure. The sparse IDs
  remain; this only isolates creation timing, not sparse-ID handling.
- `--still`: freeze animation for visual comparison.
- `--smoke`: bounded lifecycle run (30-second timeout). In dual mode it
  closes, reopens, and closes the secondary window before exiting.

Example: `03_shared_scene.exe --smoke --eager`

The default path creates the sets after both shaders report ready, exercising
dynamic variable-set creation. The ImGui panel allows pausing animation and
toggling the D3D11 window during interactive use.

## Expected Result

Both windows show the same scene: an orange rotating cube on the left, a
green vertically moving cube in the middle, and a stationary blue cube on
the right. There must be no magenta cube, missing object, or cross-object
color/transform contamination. Reopening D3D11 must restore the same scene
while GL continues rendering.

Smoke mode checks shader readiness, recorded draw progress, window lifecycle,
and the final tracked-allocation count. It does not validate pixels or the
backend result of each recorded draw. Visual verification remains required.

## Verification (2026-10-04)

Motor was updated to origin/main at 29b2a89 at the user's request. No engine
source changes were made. This fixes the previous slot-sheet reference/pointer
build errors. The full Release build of 03_shared_scene succeeds.

- `--smoke --gl-only`: exit 0, shader ready, dynamic sets created, smoke
  completed, no tracked allocations remaining. The known wglMakeCurrent(00)
  error is logged during shutdown.
- `--smoke --d3d-only`: exit 0, shader ready, dynamic sets created, smoke
  completed, no tracked allocations remaining.
- `--smoke`: two attempts exit 1 without console output. The dual-window
  runtime failure is unresolved; lifecycle success has not been verified.

The user subsequently confirmed normal manual shutdown with exit 0 and the
corrected camera framing, but reported missing middle/right cubes on initial
startup that appeared after reopening the secondary window.

## Dynamic D3D11 Fix

The Motor submodule now contains a local, uncommitted fix in
platform/graphics/d3d/d3d11.cpp:

- Check existing variable-set IDs using the ID map, not the dense array size.
- Update all constant buffers matching the requested ID, independent of order.
- Bind matching constant buffers and images without assuming sorted IDs.

The test deliberately keeps draw order 3, 0, 2 and dynamic creation. It does
not hide the regression by reconfiguring the shader or sorting the draws.
After the fix, the full Release build and D3D11-only smoke pass with no tracked
allocations remaining. The dual-window smoke still exits 1 without output
through the agent's launch tool. Desktop capture approval timed out, so visual
verification of the fix remains pending. Successful smoke runs do not establish
pixel correctness.
