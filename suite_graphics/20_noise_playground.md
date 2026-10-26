# Noise Playground

`20_noise_playground` exercises eight msl noise built-ins in both vertex and pixel
shaders. It opens one 1920x1080 GL4 window by default; `--d3d-only` selects D3D11.
All geometry and colors are procedural. No external assets or motor changes are
required.

## Run

```powershell
cmake --build build --config Release --target 20_noise_playground --parallel 4
build/bin/Release/20_noise_playground.exe
build/bin/Release/20_noise_playground.exe --compare --still
build/bin/Release/20_noise_playground.exe --demo
build/bin/Release/20_noise_playground.exe --smoke
build/bin/Release/20_noise_playground.exe --smoke --d3d-only
```

Options: `--gl-only`, `--d3d-only`, `--compare`, `--still`, `--smoke`,
`--demo`, `--strict-builtins`, `--help`. D3D11 requires Windows. VSync is requested.

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

## Known fbm Dependency Issue

On 2026-10-26, using `fbm_1d` without another noise built-in failed shader
compilation on both backends. The generator's `fbm_1d` dependency list emits the
noise overloads but omits their random/permutation helpers:

- GL4: undefined `_msl_bi_rand_1_msl_` and `_msl_internal_perm_4_msl_`.
- D3D11: undeclared `__bi_rand_1__` (first compiler diagnostic).

The playground locally requests these dependencies with
`0.0 * noise_1d(p) + fbm_1d(p, u_octaves)`. The fbm value itself is unchanged;
the explicit call causes msl to emit the missing helpers. No engine code is patched.
Disable the workaround to reproduce the actual bug:

```powershell
build/bin/Release/20_noise_playground.exe --smoke --strict-builtins
build/bin/Release/20_noise_playground.exe --smoke --strict-builtins --d3d-only
```

Both commands currently fail with exit 1. After an engine fix, they should pass
without changing the test. This workaround is not evidence that standalone fbm
dependency generation is correct.

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

Variables are updated in `on_graphics`; the frame's selected mode is retained for
`on_render`. Resources are configured/released through the first/last-frame callbacks.
