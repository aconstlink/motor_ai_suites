# Scene Light Components

`14_light_components` exercises motor's actual `light_component`,
`light_collector_visitor` and `light_pass_render_visitor`. It does not change the
engine or replace the light visitor with sample-local rendering logic.

## Run

```sh
cmake --build build --config Release --target 14_light_components --parallel 2
14_light_components --gl-only
14_light_components --d3d-only
14_light_components --dual
14_light_components --dual --smoke
```

Windows defaults to two windows, GL4 and D3D11, sharing the same scene and shader
objects. Other platforms default to GL4. `--still` freezes light motion for visual
comparison. ImGui toggles each light, pauses motion, and reverses traversal order.
Graphics smoke runs require a working desktop and are not added to headless CTest.

## Scene and Data Flow

- Six colored boxes and a floor use two msl components: ambient/base and lighting.
- A group owns one light component with a directional light and a point light.
- A child leaf owns another component with a second point light.
- A soft warm directional light (intensity 0.32), an orange point light (1.4),
  and a blue point light (1.8) provide distinct colors and strengths.
- Two small unlit cubes in the corresponding light colors mark point positions.
- Light components own the lights. The collector and application borrow them.

Every graphics update clears and reuses the collector, traverses the scene, and
flattens the collected components through `for_each_light`. The test checks that
exactly two components and three distinct expected lights were collected.
Light positions/directions are world-space values; this test does not assume that
the transform visitor transforms lights attached to nodes.

The base pass writes ambient color and depth once. Each enabled collected light
then traverses the scene with the engine light-pass visitor. Lighting accumulates
with ONE + ONE blending, depth writes disabled and LESS_EQUAL depth testing.
Base and light vertex transforms are identical. This sample intentionally has no
shadow maps, HDR pipeline, tone mapping or antialiasing.

The light render ID is `window_index * 3 + active_light_index`. Each object gets
its own component-managed variable subset for that ID; no light pass overwrites
another queued pass. Reversing the light order deliberately reuses the same IDs
for different light types. Original material values are propagated by motor's
base/subset bridges. The application does not manually supply light uniforms.

## Current API Boundary

The visitor supplies `light_direction`, `light_position`, `light_color`,
`light_intensity` and the named boolean `is_point_light`. The shader multiplies
both diffuse and specular contributions by the light RGB color and intensity.
Base sets initialize color and intensity to zero; the visitor must populate each
render subset. No sample-local code writes the per-light uniforms.

`point_light::get_radius()` is not yet forwarded. The shader still uses a fixed
six-unit squared falloff for points.
Point radius is initialized to six for consistency, but is not read by the shader.

## Smoke Coverage

After configuration settles, five phases each wait for at least 30 backend fence
callbacks per window: all lights, directional only, points only, none, then all
lights in reverse order. Assertions inspect the actual component-created subsets:

- Expected active-light counts and collector contents, without accumulation.
- Point/directional boolean and the corresponding position/direction values.
- RGB color and intensity match each source light, including after subset reuse
  and reversed light order; nonfinite values fail validation.
- World and view matrices and unique subset IDs across lights/windows per object.
- Recovery from zero lights and reuse of IDs by a different light type.

The test has a 60-second application timeout, logs via `motor::log::test`, and
returns failure for smoke assertions or a nonempty motor memory dump. A fence
confirms CPU command execution, not pixel correctness. Illumination, occlusion
and visible attenuation still require visual verification.

## Verification (2026-10-13)

Windows/MSVC Release, motor `1418eff`:

| Run | Result |
| --- | --- |
| `--gl-only --smoke` | All five phases passed, exit 0, empty memory dump |
| `--d3d-only --smoke` | All five phases passed, exit 0, empty memory dump |
| `--dual --smoke` | All five phases passed, exit 0, empty memory dump |

GL4 reports an unused normal attribute in the ambient-only shader (expected
compiler optimization) and the already known `wglMakeCurrent(00)` shutdown
diagnostic. The engine was not changed. Linux was not tested. Screenshot-based
visual verification was unavailable because desktop access to this new executable
was not approved; the interactive run was subsequently closed.
