# Lighting gallery

A small, interactive rendering test with 12 objects, four geometry links per
shader and three colored directional lights. No external assets or engine changes.
This is a low-level graphics/application-layer sample, not a scene-graph benchmark.

## Run

```powershell
cmake --build build --config Release --target 11_lighting_scene --parallel 4
```

From `build/bin/Release`:

```powershell
./11_lighting_scene.exe
./11_lighting_scene.exe --gl-only
./11_lighting_scene.exe --d3d-only
./11_lighting_scene.exe --still
./11_lighting_scene.exe --smoke
```

Windows defaults to GL4 and D3D11 windows showing the same scene. Other platforms
default to GL4 and reject `--d3d-only`. Closing either window closes the sample.
`--still` starts paused; the primary window contains the ImGui controls.

## Scene and controls

- Cube geometry comes from Motor's cube/tri-mesh/flatten path. A second mesh
  subdivides each cube triangle into 144 triangles and projects vertices onto
  a sphere; the triangle cube generator currently does not apply `tess`.
- An analytic torus has 48 major and 20 minor segments with smooth normals.
- A 12x10 checkerboard floor has per-vertex tile colors, without textures.
- Three display objects, three plinths, three foreground objects, floor and two
  architectural pieces exercise shared meshes and different geometry-link IDs.
- An ellipsoid and scaled boxes test nonuniform scaling. Normals use an explicit
  inverse-transpose equivalent, R*inverse(S), for the sample's positive TRS scales.
- A warm key, blue fill and red rim light can be enabled and adjusted separately.
  Diffuse plus Blinn-Phong highlights show hard and smooth surface responses.
- Controls select single/multipass lighting, intensity per light, ambient,
  specular strength, camera orbit and pause. Animation rotates selected objects
  and changes the key direction. Motor cameras handle perspective and resize.

The four unique meshes contain 3900 triangles in total; mesh reuse means this
is not the scene's per-frame triangle submission count. No instancing is used.

## Lighting paths

Single pass uses three explicitly named direction/color uniform pairs and
unrolled lighting expressions, without uniform arrays or MSL extensions. Disabled
lights have zero intensity, but the shader still contains all three evaluations.
Each of the 12 objects is drawn once, with ambient included once.

Multipass first draws the entire scene with ambient and depth writes enabled.
It then accumulates each enabled light using ONE/ONE additive blending,
less-equal depth comparison and disabled depth writes. Filling scene depth first
prevents hidden surfaces from accumulating light into already visible objects.
With all three lights enabled this makes 48 draws per window; with two, one and
zero lights it makes 36, 24 and 12, respectively.

Both paths use the same material, normal transform, camera, light colors and
directions. Intended RGB is equivalent apart from target quantization/clamping;
alpha differs because the multipass path adds it and is not consumed here.
This sample writes linear lighting directly to the window target: there is no
HDR pipeline, tone mapping, gamma pass, shadows, AO or postprocessing. It is not
a final production-lighting reference or a GPU performance benchmark.

## Resource lifetime

Three shared MSL objects (ambient, one light, three lights) each link all four
geometries. Configure geometry before MSL. Each object/window has five stable
variable sets: ambient, combined, and one per individual light. Cached variables
are borrowed from those sets, which are owned by the MSL objects. All values and
the active mode are prepared in `on_graphics`, never rewritten between async
draws in `on_render`. The sample updates both active and inactive paths for easy
interactive switching; do not use its CPU cost as an optimized lighting baseline.

`on_last_frame` releases MSL before geometry/state resources. `on_render` is not
called for that window's final frame.
`on_shutdown` clears borrowed bindings before releasing the owning MSL references.
Render states are explicitly applied each frame. After the additive state is
popped there are no further scene draws, avoiding reliance on the previously
observed D3D11 nested depth-state restoration issue for scene rendering.

## Verification

The smoke test switches between both modes with three, two, one and zero enabled
lights, then repeats all lights with a different camera angle and stronger
specular response. Each of ten phases requires at least 45 frames per backend
and checks the exact number of submitted scene draws. It has a 90-second timeout.
Controls are disabled during smoke. VSync is requested on; no timing claims are
made from this test. Shader readiness and empty Motor memory dumps are separate
from pixel correctness; matching images need visual inspection or readback.

The first Release build against the current engine source was blocked by ongoing
D3D11 edits (`image_variable::var_set_idx` references and `continue` statements
outside loops). The sample was built against the previously compiled engine DLLs
using `/p:BuildProjectReferences=false`. It does not validate those unfinished
backend source changes. No engine files were edited for this sample.

Visual inspection was not completed because Computer Use approval timed out.
The manual inspection process was stopped; automated verification is recorded
separately below.

On 2026-10-07 the final 3900-triangle-mesh version passed all ten phases in dual
GL4/D3D11 mode and in D3D11-only mode, both with exit 0 and empty Motor memory
dumps. All three shader configurations compiled on both backends. Logs are
`build/bin/Release/lighting-scene-dual-smoke.log` and
`build/bin/Release/lighting-scene-d3d-smoke.log`. GL4 warns about unused normals
in the ambient-only shader, where the compiler removes lighting inputs; it also
reports the known `wglMakeCurrent(00)` shutdown diagnostic.
