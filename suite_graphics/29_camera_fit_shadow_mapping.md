# Sponza Camera-Fit Shadow Mapping

Based on the rendering flow of Alex Link's
`motor_suites/techniques/02_camera_fit_shadow_mapping.cpp`, now displaying the
Sponza glTF already bundled with `16_async_assets`. This is a shadow-mapping application, not a GI
implementation. The original RSM experiment's position/normal targets and UI
entries have been removed; no indirect lighting is evaluated.

## Run

```powershell
.\build\bin\Release\29_camera_fit_shadow_mapping.exe
.\build\bin\Release\29_camera_fit_shadow_mapping.exe --d3d-only
.\build\bin\Release\29_camera_fit_shadow_mapping.exe --demo --youtube-short vertical
.\build\bin\Release\29_camera_fit_shadow_mapping.exe --demo --youtube-short square --d3d-only
```

One window opens, using gl4 by default. `--demo` hides the ImGui controls for a
clean recording. A smooth 40-second camera loop travels through the central
courtyard, turning towards the arcades at either end. It rises from eye level
to about 6.6 scene units, then descends smoothly. The view tilts up from below
and down into the courtyard from above to show more of the sunlit architecture.
Sponza itself is static.
The sun slowly sweeps sideways over an 80-second loop; its shadow camera is
refitted to the scene each frame. Camera and sun animation can be paused separately.
Its direction towards the source is `normalize(0.8 + 0.2*sin(phase), 1.0, 0.6)`,
giving an oblique light near 45 degrees elevation. Hemispheric ambient fill uses
`(0.24, 0.28, 0.36)` above and `(0.10, 0.09, 0.08)` below to keep shaded arcades readable.
Square capture uses a 1080 x 1080 client area; vertical capture
uses 1080 x 1920. Both are borderless, following the other suite demos.

The camera stays inside the courtyard rather than framing the exterior building
bounds. Portrait output expands the vertical field of view to preserve useful
horizontal coverage. Window resizing updates camera aspect and presentation viewport.
HDR render targets retain the resolution selected at startup and are scaled to
the new client size. Restart at the desired capture format for native resolution.

## Rendering

1. Load the existing texture-free Sponza glTF through motor's IO database and
   importer, preserving its 0.008 root scale and child transforms.
2. Add a shadow-casting directional light to the scene graph.
3. In `on_graphics`, synchronize component inputs **before** traversing world
   transforms and aggregated bounds. Then update the camera path. Static Sponza
   does not schedule an imported animation chain or wait for an animation merger.
4. Collect the shadow-casting directional light. Transform the scene AABB
   into light-view space, enclose it with a new AABB, and fit an orthographic
   light camera including a small margin and near/far padding.
5. Render light-space depth into a depth-only 2048 x 2048 framebuffer using
   `shadow_depth_pass`. No RSM shader is loaded and no RSM color targets are allocated.
   Then draw an opaque shadowed lighting pass through the scene
   visitors and render-data subsets, clearing and writing its own camera depth.
   There is no separate camera depth prepass: its differently grouped matrix
   multiplications could disagree with lighting-pass depth through rounding,
   rejecting visible fragments as the camera moved. The single sun pass does
   not need additive blending.
6. Present through motor's HDR pipeline, including bloom and FXAA.

The sample's lighting shader uses a 3 x 3 PCF comparison, adjustable depth bias,
diffuse/specular lighting and a small hemispheric ambient term. Ambient is an
artistic fill, not GI. The normal transform assumes the same scene conventions
as the original test; this sample does not introduce an inverse-transpose normal
matrix binding.

## Controls

- Courtyard camera or fitted light-camera selection.
- Pause camera, animate sun; light intensity, shininess, specular strength and bias.
- Scene bounds/debug primitives and the fitted sun-camera frustum.
- Mutually exclusive lighting, normal and material views; shadow/HDR render-target previews.
- Existing HDR postprocessing properties.

Leave the courtyard camera enabled for the recording. Switching it off shows
the fitted sun camera for inspecting shadow coverage.

## Verification

```powershell
.\build\bin\Release\29_camera_fit_shadow_mapping.exe --cpu-only
ctest --test-dir build -C Release -R "graphics_shadow_camera_fit_cpu|graphics_capture_29_" --output-on-failure
```

Windowless checks cover translated/flat AABBs, several light directions including
a vertical direction, landscape/square/portrait projections, the Sponza glTF import,
its scaled scene dimensions and the fitted light camera. The camera loop is
sampled every quarter second for finite matrices, positions within the scene
bounds, a vertical excursion exceeding four scene units and a continuous loop seam.
The entire sun loop is also sampled to verify
that the refitted light camera encloses the scene at every sampled direction.
These checks do not prove that the path is
collision-free against every triangle. They also
generate GLSL/HLSL for the used sample shaders; on Windows, generated HLSL is
compiled with `D3DCompile` without creating a device or window. GLSL generation
alone is not an OpenGL driver compilation test.

Shader checks also reject duplicate msl config names across the sample files.
The normal visualization uses its own config and writes every HDR color output.
Selecting it disables the lighting pass, avoiding two competing opaque passes
on the same surfaces.

No visual test or GPU-runtime comparison was performed for this addition.
Actual shadow appearance and postprocessing must still be checked interactively.
No motor source changes are required.

Debug and Release builds and all five windowless CTests passed on Windows,
including the generated HLSL compilation checks.

## Assets

Sponza is reused directly from `assets/async_loading/Sponza`, without copying or
downloading it again. It remains texture-free; the sample uses diffuse/specular
lighting, ambient fill and shadow mapping, not PBR materials.
See [model credits and terms](assets/async_loading/ATTRIBUTION.md).
The shadow shaders derive from Alex Link's motor-suites test. Its small original
Blender export remains with those shader assets but is no longer the default scene.
