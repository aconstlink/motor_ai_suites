# Scene Graph, Wire Lighting and HDR

Target: `12_scene_wire_lighting`. The low-level `11_lighting_scene` remains
unchanged as a comparison. This sample uses the same 12-object gallery, four
shared meshes and three directional lighting terms, but renders through Motor's
scene graph and supplies animation/material/light data through Wire slots.
The existing `gfx::hdr_postprocess_pipeline` handles HDR, Bloom, tone mapping
and FXAA after the scene traversal. No post-processing shader is duplicated here.

## Build and Run

```powershell
cmake --build build --config Release --target 12_scene_wire_lighting --parallel 4
```

Run from `build/bin/Release`:

```powershell
./12_scene_wire_lighting.exe
./12_scene_wire_lighting.exe --smoke
./12_scene_wire_lighting.exe --smoke --d3d-only
./12_scene_wire_lighting.exe --smoke --gl-only --still
```

Windows defaults to GL4 plus D3D11. Both render the same graph and shared MSL
object. Other platforms default to GL4. `--still` starts paused. Closing either
window closes the sample. The tool panel changes three light intensities,
ambient, material tint, exhibit-group height, camera angle and animation pause.
Second viewpoint offsets the second camera by 35 degrees; both cameras also
use their own window aspect ratio.

`HDR post processing` switches between the pipeline and direct rendering.
The direct path is an intentionally unprocessed comparison: HDR light values
can clip on the backbuffer. The expandable Post processing section exposes the
pipeline's own property sheets for brightpass, bloom, merge, tone_map and fxaa.
Light-intensity sliders now range from 0 to 12. The initial bright-pass threshold
is 2, with light intensities 6/3/5, so lit surfaces can exceed SDR white.

## Post-Processing Flow

```text
Wire + scene visitors
  -> HDR framebuffer 0 (forward color and depth together)
  -> bright pass
  -> bloom downsample / upsample
  -> merge HDR scene + bloom
  -> Reinhard tone mapping
  -> FXAA
  -> window backbuffer
  -> ImGui
```

One application-owned pipeline is configured and released through each window's
frontend. Backend resources are realized independently; CPU properties are shared.
The sample uses its own HDR scene state with depth writes and both clears enabled.
It therefore does not need the pipeline's separate Z-prepass/HDR-state pair.
A presentation state explicitly disables depth testing and blending before the
fullscreen passes, and supplies the current window viewport.

Internal framebuffers stay at the pipeline's default 1920x1080. Resizing a window
updates its camera aspect and presentation viewport, not those framebuffers.
This deliberately does not exercise `hdr_postprocess_pipeline::on_resize`, whose
current implementation does not preserve/reconfigure all downsample resolutions.
The final image is scaled to the window; FXAA operates at the internal resolution.
There is no additional output-gamma conversion in this sample.

## Graph and Data Flow

The root owns a sample-local `lighting_controls` component. Static geometry is
attached directly to the root. The five spinning objects are children of an
exhibit group with its own `trafo3d_component`. Every rendered leaf owns an
`object_controls` component, a `trafo3d_component` and an `msl_set_component`
containing one `msl_component`. All MSL components reference the same shader.

The controls components use `icomponent::create_output_slot`; their raw slot
pointers are borrowed. `icomponent` disconnects and releases these slots at
destruction. The application similarly borrows component pointers from the graph.

Transform path:

```text
object/group output<trafo>
  -> trafo3d_component input
  -> variable_update_visitor
  -> trafo_visitor (parent * local)
  -> msl_component world binding
```

Shader-variable path:

```text
shared lighting / individual material output slots
  -> msl_component shader input slots
  -> base render_data_set
  -> internal output bridge
  -> per-view render_data_set input bridge
  -> graphics variable set
```

Connections are established once during construction. The sample pre-creates
typed shader input slots, so compilation can subsequently populate the bridge
without requiring the application to reconnect them. Light outputs fan out to
all 12 objects. No shader variable is directly rewritten by application code
after its initial creation; runtime changes are published through Wire.

`on_graphics` updates outputs, cameras, compilation-dependent bindings and then
runs variable/transform visitors. `on_render` traverses the same graph with
`render_visitor(0, view_id, frontend, camera)`. View IDs 0 and 1 distinguish
camera data; they are not backend resource IDs. The scene component creates the
subsets, rather than the application maintaining per-window copies of all sets.
Compilation readiness is gated before the first visible scene traversal.

Graphics geometry, state and MSL configure/release remain explicit in the
application. The MSL object is marked managed because this application owns
that lifecycle. The graph stays alive until all last-frame releases have been
submitted and application shutdown is reached.

## Scope

- Single-pass, three explicitly named direction/energy pairs; no uniform arrays.
- World, view, projection and camera-position use existing MSL semantics.
- Light color times intensity uses ordinary named slots because the corresponding
  automatic light bindings are not present yet.
- This tests Wire slots and both internal variable bridges, not task-graph
  scheduling, animation tracks or the light collector/light-pass visitor.
- The two sample-local control components are deliberately small adapters, not
  new engine components. Light colors/intensities are not read from `gfx::light`.
- Normal transforms handle positive, nonuniform object scaling. The exhibit
  parent translates only; arbitrary parent rotation/scale would require updating
  the normal calculation from the composed world transform.
- No shadows or extra output gamma correction. It is a rendering integration
  sample using the existing gfx pipeline, not a performance benchmark.

## Automated Verification

Smoke runs six phases with 45 rendered frames per window: three/two/one/no
enabled lights, then changed material and group translation with a second
viewpoint, and finally paused animation with changed ambient/fill intensity.
The overall timeout is 90 seconds.
Phase 3 bypasses post processing; phases 4 and 5 re-enable it and change the
bright-pass threshold to 1.5 and 3 respectively.

After startup, assertions inspect CPU-side render subsets and verify:

- Composed world transforms against parent * local.
- World, view, projection and camera-position shader values.
- Material, normal matrix, ambient and all three direction/energy pairs.
- Distinct subset IDs for the two views.
- Stable active-set count: 12 base sets plus 12 per view.
- HDR framebuffer readiness on each backend, availability of all five stage
  property sheets and the expected bright-pass threshold.
- Both post-processed and direct rendering paths have executed.
- Empty Motor memory-manager dump at shutdown.

Smoke resolves subset IDs through an additional `render_update` before queuing
draws and performs name-based checks afterward. These extra checks are only
enabled with `--smoke`; they are not representative of normal rendering cost.
The checks do not read back GPU pixels or prove visual equivalence.

On 2026-10-07 the HDR-enabled Release build and all six phases passed for dual
GL4/D3D11, D3D11-only and GL4-only with `--still`. All pipeline shaders compiled.
All three runs exited 0 and ended with empty tracked-memory dumps.
Logs in `build/bin/Release`:

- `scene-wire-hdr-dual.log`
- `scene-wire-hdr-d3d.log`
- `scene-wire-hdr-gl-still.log`

GL4 reports `Could not find image []` twice and
`Could not find image [gfx.postprocess.fb.5.0]` during Bloom configuration.
The pipeline configures unused entries as well: its active Bloom chain only
renders levels 1 through 4, whereas level 5 uses a placeholder texture name.
The known `wglMakeCurrent(00)` shutdown diagnostic also remains. These diagnostics
are not suppressed by the sample. Visual inspection and GPU pixel readback were
not performed; passing CPU checks is not proof of correct final pixels.
No engine source or original motor_suites files were changed for this extension.
