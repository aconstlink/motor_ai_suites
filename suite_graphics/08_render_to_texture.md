# Render to texture

`08_render_to_texture` renders three animated cubes with a Motor camera into an
RGBA8 framebuffer with a depth32 target, then presents its color target through
an MSL post-processing shader. No external assets or scene graph are required.
The same CPU-side graphics objects are configured independently on GL4 and D3D11.

## Build and run

From the repository root:

```powershell
cmake -S . -B build
cmake --build build --config Release --target 08_render_to_texture --parallel 4
```

Run from `build/bin/Release`:

```powershell
.\08_render_to_texture.exe
.\08_render_to_texture.exe --still
.\08_render_to_texture.exe --still --explicit-state
.\08_render_to_texture.exe --smoke
.\08_render_to_texture.exe --smoke --explicit-state
.\08_render_to_texture.exe --smoke --gl-only
.\08_render_to_texture.exe --smoke --d3d-only
```

Windows defaults to GL4 plus D3D11. Other platforms default to GL4; D3D11 is
rejected there. `--still` starts paused. The controls are in the primary window.
Closing the primary window quits; closing the secondary leaves the application
running, and the checkbox can open a new secondary window.

## Controls and expected image

- Resolution: 320x240, 640x480, or 1280x960. This reconfigures the existing
  framebuffer, without recompiling shaders or changing the texture variable.
- Effect: original, greyscale, or inverted. This updates a shader variable.
- Pause: stop cube animation for comparisons.
- D3D11 window: close/reopen the secondary backend while keeping CPU objects alive.
- Explicit cube state: reapply depth and viewport state before the cube draws.
  Leave this disabled to test nested state restoration; enable it as a reference.

In original mode the corner markers must be red at top left, green at top right,
blue at bottom left, and yellow at bottom right. A white bar exists only at the
top. These markers are rendered INTO the offscreen target, not added afterward.
The presentation shader uses `rt_texture` to handle backend target orientation.

The orange cube is nearer the camera and submitted FIRST. It must cover the
green and blue cubes where they overlap, irrespective of submission order.
Face-dependent colors also expose incorrect self-occlusion.

The output quad fills the window. Resizing the window should not crop the image
or leave an old offscreen viewport active. The offscreen scene remains 4:3 and
is deliberately stretched if the window has a different aspect ratio.

## Rendering and lifetime

1. Configure geometry, states and framebuffer before configuring MSL objects.
2. Wait for resource readiness separately for each backend.
3. Bind the framebuffer and push its clear/depth/viewport state.
4. Draw the orientation background with depth disabled; pop back to scene state.
5. Draw the cubes with distinct variable sets.
6. Pop scene state, unbind the framebuffer, then draw the post quad to the window.
7. Pop the post state before the application's ImGui pass.

`on_graphics` updates shared world matrices and the effect once per frame.
The framebuffer description changes there only after the previous revision is
ready on active windows. Each window tracks the submitted target revision so a
resize is configured on every backend, not consumed by the first `on_render`.
The three viewport sizes are immutable entries in one state object; another
three entries provide the no-clear explicit-state reference.

CPU resources and borrowed variable pointers remain alive through the window
release callbacks. `last_frame` queues backend releases; MSL CPU ownership is
released in `on_shutdown`. This follows the existing suite's application lifetime.

## Smoke coverage

The smoke test waits for at least 90 submitted frames per active window in each
drawing phase. It runs 640x480/original, 320x240/greyscale, 1280x960/inverted,
closes and reopens the secondary window in dual mode, and returns to
640x480/original. It checks readiness/results, target revisions, lifecycle counts,
a 60-second timeout and the final Motor memory dump.

A successful smoke exit proves the control/resource path completed; it does NOT
prove pixel correctness, GPU completion timing or the absence of driver leaks.
There is no automatic framebuffer readback or image comparison. Ordinary window
resizing and interactive controls still deserve a manual check.

## Finding: D3D11 nested state restoration

Observed on 2026-10-06: the default nested-state path has correct occlusion on
GL4, but on D3D11 the later green/blue cubes paint over the nearer orange cube.
With `--explicit-state`, the D3D11 reference image has the correct occlusion and
matches the GL4 scene. Marker orientation is correct on both backends.

In `motor/platform/graphics/d3d/d3d11.cpp`,
`handle_render_state(size_t, size_t)` pops into `old`, changes `old.rss` to the
restore description, then passes `old` to `handle_render_state(..., true)`.
However, that overload binds `incoming_states.depth_stencil_state`, which still
belongs to the removed entry. For this test it rebinds the depth-disabled
background state rather than the underlying depth-enabled scene state.
See the depth pop branch around line 1615 and its caller around line 1773.

The rasterizer and blend branches use the same removed-entry handles and merit
inspection too; those are code-review observations, not isolated pixel tests here.
The engine has NOT been modified. The default keeps this regression visible;
the optional explicit state is a diagnostic reference, not a backend fix.

## Verification

Release build and dual-backend smoke runs with and without the explicit-state
reference completed with exit code 0 and an empty Motor memory dump. The GL4-only
and D3D11-only smoke runs also completed with exit code 0 and an empty dump.

Static original-mode images were inspected on both backends, including the
D3D11 explicit-state comparison. Image equivalence during every resize/effect
phase has not been verified. Known `GetClientRect failed` and
`wglMakeCurrent(00)` messages can still appear during window teardown.
