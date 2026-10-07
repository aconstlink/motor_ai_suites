# Scene Graph and Wire Lighting

Target: `12_scene_wire_lighting`. The low-level `11_lighting_scene` remains
unchanged as a comparison. This sample uses the same 12-object gallery, four
shared meshes and three directional lighting terms, but renders through Motor's
scene graph and supplies animation/material/light data through Wire slots.

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
- No shadows, HDR, tone mapping, output gamma correction or AA. It is a rendering
  integration sample, not a production pipeline or performance benchmark.

## Automated Verification

Smoke runs six phases with 45 rendered frames per window: three/two/one/no
enabled lights, then changed material and group translation with a second
viewpoint, and finally paused animation with changed ambient/fill intensity.
The overall timeout is 90 seconds.

After startup, assertions inspect CPU-side render subsets and verify:

- Composed world transforms against parent * local.
- World, view, projection and camera-position shader values.
- Material, normal matrix, ambient and all three direction/energy pairs.
- Distinct subset IDs for the two views.
- Stable active-set count: 12 base sets plus 12 per view.
- Empty Motor memory-manager dump at shutdown.

Smoke resolves subset IDs through an additional `render_update` before queuing
draws and performs name-based checks afterward. These extra checks are only
enabled with `--smoke`; they are not representative of normal rendering cost.
The checks do not read back GPU pixels or prove visual equivalence.

On 2026-10-07 the Release build and all six phases passed for dual GL4/D3D11,
D3D11-only and GL4-only with `--still`. All three runs exited 0 and ended with
empty tracked-memory dumps. Logs in `build/bin/Release`:

- `scene-wire-dual.log`
- `scene-wire-d3d.log`
- `scene-wire-gl-still.log`

GL4 still reports the known `wglMakeCurrent(00)` shutdown diagnostic. Visual
inspection was not performed. No engine source or original motor_suites files
were changed.
