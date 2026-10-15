# Async Asset Loading: Sponza + Suzanne

`16_async_assets` loads external model data through Motor's own IO/database and
glTF module. Sponza is imported first. After it has been submitted with ready
shaders for 20 frames, Suzanne is requested and inserted into the live scene.
The frame loop polls futures without waiting for them.

## Build and Run

```sh
cmake --build build --config Release --target 16_async_assets --parallel 2
build/bin/Release/16_async_assets.exe --gl-only
build/bin/Release/16_async_assets.exe --d3d-only
```

D3D11 is Windows-only. For single-configuration builds, use the executable in
`build/bin`. Assets are bundled in `assets/async_loading`; CMake embeds that
source directory, so launching from Visual Studio does not require a particular
working directory. Use `--assets PATH` to override it.

The UI displays import state, time and primitive counts, and provides camera
yaw, distance, height and orbit controls. The initial camera looks down Sponza's
central hall towards Suzanne at `(0, 1.4, 0)`. Sponza's original node scale
`0.008` is preserved, not applied a second time.

No textures, lights, shadow maps or postprocessing are used. The sample replaces
the importer's flat-color shader source before first configure with a normal-color
diagnostic shader. It retains geometry links, variable sets and `base_color`
inputs. This is intentionally not a faithful material preview: normal direction
provides surface contrast, with a material tint and an orange tint for Suzanne.
Normals are shown in model space, which is sufficient for these unrotated,
uniformly scaled fixtures.

## Data Flow and Ownership

1. Construct `motor::io::database` and register default import modules.
2. Call `module_registry::import_from` for `Sponza.Sponza.gltf`.
3. Poll `future_item_t::wait_for(0)` from `on_graphics`; call `get()` only when ready.
4. Move the imported root into a placement group, collect MSL components and
   update variables/transforms using the existing scene APIs.
5. `render_visitor` configures geometry/shaders and renders the imported nodes.
6. Request Suzanne while the first model is already active; use the same path.

The importer borrows the database. Pending futures are therefore drained during
shutdown before releasing the registry/database. This can wait at shutdown; it
does not wait in the active frame loop. There is no claim of import cancellation.
Borrowed component/shader pointers remain valid while their scene root is owned.
MSL resources are released in `on_last_frame`. Imported geometry belongs to
configuration components; backend teardown releases its GPU buffers. The test
does not yet cover repeated asset unload/reload in a surviving window.

## Verification

```sh
build/bin/Release/16_async_assets.exe --gl-only --smoke
build/bin/Release/16_async_assets.exe --d3d-only --smoke
build/bin/Release/16_async_assets.exe --gl-only --close-loading
```

`--smoke` requires imported primitives (103 Sponza, 1 Suzanne for these fixtures),
successful GPU configuration and 90 submitted frames with both models ready.
It times out after 90 seconds and exits nonzero on failure or outstanding
Motor-tracked allocations. It is a lifecycle/submission test, not pixel validation.

`--close-loading` requests both imports and closes immediately, without consuming
either future in the frame loop. Shutdown checks both returned scenes and drains
them before database teardown. It does not require that both workers are still
executing at the instant shutdown begins.

The logged pending-poll count shows whether frames ran while an import was not
ready. No artificial delay is inserted, and zero is valid on sufficiently fast
loads. Import timings include polling latency and scene adoption, not just IO.

Verified on Windows/MSVC Release with Motor `33c6422` (2026-10-15): GL4 and D3D11
smoke tests, plus GL4 pending-import shutdown. Motor memory dump: 0 bytes.
The existing GL shutdown message `wglMakeCurrent(00)` still appears. No engine
changes were made. Linux and final image appearance were not verified here.

## Import Limitations Exposed

- Sponza's original materials have no names. The current importer constructs
  strings from `cgltf_mat.name` without a null fallback, causing an access
  violation. The test copy supplies stable `material_N` names.
- Sponza originally lists attributes as position/UV/normal/tangent. D3D11's
  input-layout setup rejects the imported shader/layout combination. The test
  copy lists position/normal first, followed by the remaining attributes.
  Attribute order is not a glTF semantic requirement; this accommodates the
  current backend. No accessors or binary geometry were changed.
- These are JSON `.gltf` files with external binary `.bin` buffers. Successful
  loading demonstrates binary buffer support, **not** standalone `.glb` support.

See [asset provenance and original notices](assets/async_loading/ATTRIBUTION.md).
