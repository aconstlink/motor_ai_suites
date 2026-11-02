# Debug labels on 3D primitives

A small scene with a wire sphere, rotating box, ring, camera frustum,
directional arrows and coordinate axes. All geometry is submitted through
`motor::gfx::primitive_render_3d`; all object names use
`motor::gfx::text_render_2d`. ImGui is used only for controls.

No engine changes, custom text shaders, new font assets or dependencies.
This is a direct gfx integration test, not a scene-graph component/visitor.

## Build and run

```sh
cmake --build build --config Release --target 26_debug_labels --parallel 2
build/bin/Release/26_debug_labels.exe
build/bin/Release/26_debug_labels.exe --d3d-only
build/bin/Release/26_debug_labels.exe --dual
build/bin/Release/26_debug_labels.exe --still
build/bin/Release/26_debug_labels.exe --cpu-only
ctest --test-dir build -C Release -R "^graphics_debug_labels_cpu$" --output-on-failure
```

The default is one 1100x900 GL4 window. `--dual` opens a second D3D11 window;
each has its own camera, viewport and renderer data. D3D11 requires Windows.
`--still` stops object animation and automatic orbit. The controls toggle labels,
empty text frames, animation and camera orbit, and adjust point size, yaw and
distance. Closing either window closes the application.

The existing font from `motor/externals/ocornut/imgui/misc/fonts/Roboto-Medium.ttf`
is imported through motor's database/format layer. It is not loaded by ImGui.
CMake provides its source path, so the source checkout is required at runtime.
Roboto is by Christian Robertson, Apache License 2.0; see the bundled font
documentation and the [existing font test](21_font_renderer.md#font-assets).

## Data flow

1. Update the motor camera and the animated object positions in `on_graphics`.
2. Project each label's world anchor with `projection * view * vec4(anchor, 1)`.
3. Reject non-finite coordinates, `w <= 0`, and anchors outside the clip volume
   before perspective division. The CPU helper uses motor's [-1,1] Z projection.
4. Divide by W and offset the result by 12x18 client pixels converted to NDC.
5. Draw an anchor cross and leader line, then enqueue the label in two text
   groups: dark drop shadow and colored foreground. Text uses identity matrices.
6. Prepare every renderer once per window, including empty text frames. Upload
   through the frontend, draw the 3D scene with depth, and the labels without it.

These are debug overlay labels: geometry does not occlude them. Frustum rejection
of the anchor is not a visibility/occlusion test. Long strings can extend beyond
the viewport or overlap each other; no layout solver or text-bounds clipping is
hidden in the sample. Point size is the renderer's existing size parameter, not
a promise of pixel/DPI-independent glyph sizing.

## Verification

`--cpu-only` opens no windows. It checks projection with a motor camera in
landscape, portrait and square dimensions; near/far and behind-camera rejection;
orthographic projection; invalid floating-point input; and pixel offset conversion.
It also imports and validates the ASCII atlas and runs populated, empty and
restored text preparation. The final motor allocation report must be empty.

The CPU lifecycle check is limited to completion without a crash and cleanup:
the renderer does not expose its prepared glyph data or render-group counts.
It does not verify glyph placement, shader compilation, rasterization or GPU output.

The optional `--smoke` runtime uses three phases (labels, empty text, labels
restored), 45 executed frames per phase, an execute fence per window and a
30-second watchdog. Completion is not a pixel-correctness assertion.

Windows / Visual Studio 18 2026, 2026-11-01:

| Configuration | Build | Windowless CTest | Remaining motor allocations |
| --- | --- | --- | --- |
| Release | Passed | Passed, 0.31 s | 0 bytes |
| Debug | Passed | Passed, 0.45 s | 0 bytes |

The run covers 24 projection/offset assertions plus atlas validation and the
CPU text lifecycle. No visual/desktop test was run. GL4/D3D11 rendering, the
interactive controls and the optional graphical smoke sequence still need
runtime verification; Linux was not built in this change.

## Text renderer feedback

The existing separation into draw, CPU preparation, frontend upload and render
works well for this use case. A shared atlas, multiple text groups and explicit
view/projection settings are sufficient; the application only adds world-anchor
projection and a small overlay state.

The following findings come from the current source, not a GPU screenshot:

1. **Text advance appears twice.** `draw_text` adds `adv` to `tgi.pos` and also
   stores it in `tgi.adv_x`. The GLSL and HLSL vertex shaders transform `t1.xy`
   and then add `t2.w` again. With this sample's identity matrices, horizontal
   advance is doubled. Keep anchor position and glyph-relative offset separate,
   and apply the latter exactly once. The test deliberately uses whole strings
   through the public API rather than hiding this with per-character calls.
2. **Glyph offsets bypass the projection.** The shaders add glyph size, bearing
   and advance after transforming the anchor. Thus scaling/rotating an orthographic
   text projection does not scale/rotate the whole string. A resize changes glyph
   proportions when width and height change differently. A documented coordinate
   model (pixel-space text or fully transformed text geometry) would make labels
   considerably easier to position and size.
3. **Measurement and alignment.** Expose `measure_text` with advance and ink
   bounds plus baseline/ascent/descent; then add left/center/right anchoring.
   This avoids duplicating atlas lookup/layout rules in every label application.
4. **Alpha is discarded.** The public color is a vec4, but CPU glyph data stores
   only RGB and the fragment shader derives alpha exclusively from glyph coverage.
   Carry caller alpha through and multiply both RGB and coverage for the current
   premultiplied-alpha blend mode. This would enable distance fades and highlights.
5. **Layout and lifetime boundaries.** UTF-8 is currently iterated byte-wise,
   spaces use the question-mark advance, and there is no explicit newline layout.
   The geometry is allocated for 1000 glyphs per group without a corresponding
   draw limit/growth path. `release()` is empty, with no frontend release overload.
   The sample uses ASCII, stays well below capacity, and keeps renderers alive
   until window/backend teardown.

For this specific label use case, fix advance/coordinate handling first, then
add measurement/alignment and alpha. None of this requires putting scene-graph
knowledge into `text_render_2d`: projecting a world anchor belongs in a helper or
debug visitor above the 2D renderer.
