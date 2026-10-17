# Debug Primitives

`17_debug_primitives` exercises `gfx::primitive_render_3d` and
`gfx::tri_render_3d`. The scene is procedural and needs no assets, custom shaders,
scene graph or physics engine. Circles, range rings, arrows and camera volumes
use the public primitive renderer API. A scene visitor or Jolt debug-draw adapter
can reuse those calls; neither integration is part of this sample.

## Primitive API

- `draw_wire_circle(center, right, up, radius, color, segments)` creates an outline
  in the plane spanned by orthonormal `right` and `up` vectors. Camera right/up
  vectors produce a billboard. An overload accepts an orientation matrix instead.
- `draw_wire_sphere(center, radius, color, segments)` draws three world-axis-aligned
  great circles. It is a debug volume, not a tessellated sphere.
- `draw_arrow(origin, direction, length, color, head_length, head_radius)` normalizes
  the direction and draws a shaft plus two head lines. Head dimensions are in
  world units; defaults are 0.35 and 0.18. The head length is clamped to the shaft
  length. A zero head dimension reduces the arrow to a line.
- `draw_frustum(camera, color)` draws perspective or orthographic camera volumes.

Wire shapes use the existing line batch. Circles default to 32 segments;
fewer than three segments or non-positive radii emit no lines. Arrows with zero
direction or non-positive length emit no lines. The existing filled `draw_circle`
API and its behavior are unchanged.

## Build and Run

```sh
cmake --build build --config Release --target 17_debug_primitives --parallel 2
build/bin/Release/17_debug_primitives.exe --gl-only
build/bin/Release/17_debug_primitives.exe --d3d-only
build/bin/Release/17_debug_primitives.exe --dual --smoke
```

OpenGL is the default. D3D11 and dual-backend mode require Windows. On a
single-configuration build the executable is normally under `build/bin`.
`--still` disables automatic animation. No graphics test is registered as an
unattended CTest: the application requires a desktop and a graphics context.

## Display and Controls

- Yellow: camera-facing point-light circle with a world-space radius. Optional
  three-axis range rings distinguish the 3D volume from the billboard symbol.
- Pale yellow: directional arrows; their length is symbolic, not a light range.
- Cyan: perspective camera near/far rectangles and connecting edges.
- Pink: orthographic camera box.
- Red/green/blue: world axes; a floor, wall and orange filled disk exercise the
  triangle renderer and make line occlusion visible.
- `Depth test` switches lines between occluded and always-visible rendering.
  Opaque triangles keep depth testing and writing enabled.
- Radius, segment count, camera far plane, viewing distance/yaw, orbit, pause and
  individual debug categories can be changed at runtime. Resize each window to
  test independent aspect ratios. `Empty frame` clears both primitive batches.

The sample holds separate renderer instances per window so camera uniforms are
not overwritten while another backend still references them. Shape emission and
CPU preparation happen in `on_graphics`; configuration, upload and rendering use
the corresponding frontend in `on_first_frame` and `on_render`.

## Smoke Coverage

The smoke run waits for 24 frontend fence completions per window in each phase:
normal geometry, increased segment count/range rings with depth disabled, empty
batches, restored smaller batches, and degenerate/short-arrow calls. A
45-second watchdog fails stalled runs. Window closure before completion fails
the smoke run. A fence confirms queue progress, not GPU completion or correct
pixels. Camera-volume corners are additionally projected back through their
camera matrices and checked against expected clip-space corners. Corners use
`near_far_plane_half_dims()` for perspective and orthographic cameras. Cameras
start default-constructed; phase 3 also switches from orthographic to perspective.
Final memory
accounting is checked after teardown.

This is not a pixel regression test. Check that both APIs display the same
shapes/colors, the camera boxes face their targets, the point circle faces the
viewer, and the wall hides lines only with depth testing enabled.

## Existing Engine Limitations

- `primitive_render_3d::draw_circle()` currently emits a filled disk and ignores
  `border_color`. Wire circles here use `draw_wire_circle()`.
  The orange filled disk calls `tri_render_3d::draw_circle()` directly.
- Primitive renderers expose no frontend-specific release operation; their
  `release()` methods currently do not release backend objects. Instances remain
  alive through window/backend shutdown. This test does not claim to validate
  releasing/recreating a renderer within a still-running backend.

The sample itself does not modify engine sources. Companion engine fixes replace
the entire perspective matrix, use vertical FOV and width/height for perspective
half-dimensions, keep orthographic half-dimensions constant, and draw frustums
along local -Z using the camera's near/far values.

## Verification (2026-10-18)

Engine revision: see the motor submodule, plus local camera fixes and primitive
API additions. Windows / Visual Studio 2026 / Release.

- Build: successful; no warnings emitted by the new test source.
- `--dual --smoke`: exit 0; all five phases completed in both backends.
- Perspective and orthographic corner checks: passed.
- Default-constructed cameras and orthographic-to-perspective switching: passed.
- CTest `math_transformation_camera` and `math_transformation_projection`: passed,
  including replacement of identity, arbitrary-filled and orthographic matrices.
- Both backends reported successful compilation of the primitive shaders.
- Final motor memory dump: 0 bytes, no entries.
- The existing `wglMakeCurrent(00)` shutdown message still appears.
- No desktop capture or pixel comparison was performed. Linux was not tested.
