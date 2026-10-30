# Noise Playground

`20_noise_playground` exercises eight msl noise built-ins in both vertex and pixel
shaders. It opens one 1920x1080 GL4 window by default; `--d3d-only` selects D3D11.
All geometry and colors are procedural. No external assets or motor changes are
required.

## Run

For square or portrait recordings, use `--youtube-short square|vertical`.
Combine with `--demo` for the animated sequence. See
[Short Video Capture](../README.md#short-video-capture) for dimensions and examples.

```powershell
cmake --build build --config Release --target 20_noise_playground --parallel 4
build/bin/Release/20_noise_playground.exe
build/bin/Release/20_noise_playground.exe --compare --still
build/bin/Release/20_noise_playground.exe --demo
build/bin/Release/20_noise_playground.exe --smoke
build/bin/Release/20_noise_playground.exe --smoke --d3d-only
```

Options: `--gl-only`, `--d3d-only`, `--compare`, `--still`, `--smoke`,
`--demo`, `--help`. D3D11 requires Windows. VSync is requested.

## Animated Demo

Enable **Demo** in the panel or start with `--demo`. The sequence shows each of
the eight landscape variants for one second, followed by one second of the 2D
comparison, then repeats. These are direct cuts, not crossfades. Frequency,
amplitude, animation speed and IQ jitter/smoothness vary continuously. Orbit,
grayscale and pause remain available; automatic controls are disabled while Demo
is active. Disabling Demo leaves the current settings available for manual editing.

The sequence starts after shader readiness. Its one-second cadence is independent
of the noise speed; Pause freezes the sequence as well as the animation. `--demo`
takes precedence over `--compare`. Use `--smoke --demo` (optionally `--d3d-only`)
to check one timed cycle and submissions for every view. Combining this timed
smoke with `--still` is rejected because it would never advance.

## Views and Controls

- **Landscape:** a 192x192-cell grid (73,728 triangles) displaced in the vertex
  shader. Three noise samples per vertex estimate height and finite-difference
  normals. Color depends on elevation and slope, with simple directional shading.
- **2D comparison:** eight labeled quads evaluate noise per pixel. Colored and
  grayscale modes use the same scalar field. This is a visual comparison, not a
  performance benchmark or an assertion that the functions share a distribution.
- Frequency, amplitude, animation speed, camera yaw, pause and orbit are adjustable.
- Octaves affects `perlin_1d` and `fbm_1d`; jitter/smoothness affects `iqnoise`.
- 2D inputs translate over time. 3D inputs additionally move through their third
  dimension. Pause freezes both time and automatic orbit; camera yaw remains editable.

The tested calls are `noise_1d(vec2)`, `perlin_1d(vec2, int)`, `perlin_2d(vec2)`,
`perlin_3d(vec3)`, `snoise_2d(vec2)`, `snoise_3d(vec3)`,
`iqnoise(vec2, float, float)` and `fbm_1d(vec3, int)`.
The names are the engine's built-in names, not inferred input dimensions:
`perlin_1d` takes a vec2 here. Its coordinates are multiplied by eight for a more
readable comparison. Signed Perlin/Simplex outputs are mapped with `0.5 + 0.5*n`;
display colors clamp to [0,1], while terrain displacement uses the unclamped value.

## fbm Dependency Regression

On 2026-10-26, using `fbm_1d` without another noise built-in failed shader
compilation on both backends. The generator's `fbm_1d` dependency list emits the
noise overloads but omits their random/permutation helpers:

- GL4: undefined `_msl_bi_rand_1_msl_` and `_msl_internal_perm_4_msl_`.
- D3D11: undeclared `__bi_rand_1__` (first compiler diagnostic).

The original playground worked around this with
`0.0 * noise_1d(p) + fbm_1d(p, u_octaves)`. That workaround and its
`--strict-builtins` switch have been removed. The fbm shader now calls only
`fbm_1d(p, u_octaves)` in both vertex and pixel stages. The standard smoke runs
therefore check standalone fbm dependency generation:

```powershell
build/bin/Release/20_noise_playground.exe --smoke
build/bin/Release/20_noise_playground.exe --smoke --d3d-only
```

On the original engine revision, both commands fail with exit 1. Engine fix
`f3051c4`, verified on 2026-10-27, adds `rand_1d_1`, `rand_1d_2` and `int_perm_4d_4`
before the noise overloads in the GLSL4 and HLSL5 `fbm_1d` dependency lists.
The fbm implementation and public API are unchanged. This test requires motor
`f3051c4` or a later revision containing that fix; older revisions are expected
to fail rather than silently use a workaround.

## Verification

The smoke waits for all eight shaders to become ready, renders the comparison,
then each landscape variant for 30 submissions. It also changes grayscale and
octave settings. A compilation failure exits unsuccessfully; a 90-second timeout
catches stalled readiness. Logging uses `motor::log::global_t::test`.

Release verification on 2026-10-26:
- GL4 and D3D11: all nine phases passed, exit 0, empty motor memory dumps.
- Timed `--smoke --demo`: complete nine-second cycles passed on GL4 and D3D11,
  all nine views submitted, exit 0 and empty memory dumps. The original GL4
  smoke also passed after adding Demo mode. Demo UI interaction is not verified.
- Strict mode: both backends reproduced the fbm failure, exit 1, empty memory dumps.
- UI screenshot access was not approved. Visual framing, UI interaction and pixel
  correctness still require manual verification. No pixel-equivalence, numerical
  noise-reference or GPU-timing checks are claimed by the smoke test.
- Linux was not built or run for this addition.

Release verification on 2026-10-27, motor `96fd381` plus the generator fix
subsequently committed as `f3051c4`:
- GL4 strict mode reproduced the missing-helper failure before the fix.
- GL4 and D3D11 strict mode passed all nine smoke phases after the fix, exit 0,
  with empty motor memory dumps.
- GL4 and D3D11 also passed the default mode with an explicit `noise_1d` call alongside
  fbm, checking that shared helpers do not cause duplicate-definition errors.
- This checks generated-shader compilation and smoke submissions, not numerical
  reference values or visual pixel correctness.

After removing the workaround on 2026-10-27, the standard `--smoke` and
`--smoke --d3d-only` commands both passed all nine phases with motor `f3051c4`,
exit 0 and empty memory dumps. No extra flag is needed to test standalone fbm.

Variables are updated in `on_graphics`; the frame's selected mode is retained for
`on_render`. Resources are configured/released through the first/last-frame callbacks.
