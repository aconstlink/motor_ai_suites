# Font renderer

Based on `motor_suites/suite_gfx/00_text_renderer.cpp`. All visible text is drawn
by `motor::gfx::text_render_2d`; there are no ImGui labels or custom text shaders.
No engine changes or additional dependencies are needed.

## Build and run

```sh
cmake --build build --config Release --target 21_font_renderer --parallel 4
build/bin/Release/21_font_renderer.exe
build/bin/Release/21_font_renderer.exe --d3d-only
build/bin/Release/21_font_renderer.exe --smoke
build/bin/Release/21_font_renderer.exe --smoke --d3d-only
```

The default is one 1280x720 GL4 window. Every four seconds the application moves
to the next phase; `--still` keeps the first layout and disables movement.
`--smoke` advances after 45 submitted frames per phase, exits after five phases,
and has a 30-second application watchdog. D3D11 is Windows-only.

## Coverage

- Database/format import of Roboto and Cousine into one 512x512-page glyph atlas.
- Explicit lookup checks for ASCII 33..126 in both font faces, including `?`.
- Multiple font sizes, RGB colors, changing counters and animated positions.
- Three text groups, including only group 2 active and all groups empty.
- Growing from empty to 900 glyphs in group 1, then shrinking to normal text.
- Missing ASCII control character falling back to `?`, plus empty strings.
- Global and per-group view/projection setters; aspect adjustment on resize.
- Final motor allocation report must be empty for a successful exit.

The deliberately empty phase shows only the background. Afterward, all text
must return without stale glyphs from preceding phases. Look for independent
group contents, intact rows and consistent glyph proportions when resizing.

The smoke log reports **submitted frames**, not pixel correctness. The renderer
does not expose its internal shader readiness or graphics objects. Visual
inspection and checking backend diagnostics remain necessary; this is not an
automated screenshot comparison or a general Unicode test.

## Verification

Windows Release, 2026-10-27:

| Backend | Atlas checks | Five smoke phases | Exit | Remaining motor allocations |
| --- | --- | --- | --- | --- |
| GL4 | Passed, 188 glyphs / one page | Completed | 0 | 0 bytes |
| D3D11 | Passed, 188 glyphs / one page | Completed | 0 | 0 bytes |

Both backends reported successful shader compilation. No shader/backend errors
appeared in these runs. Pixel appearance, interactive resizing and Linux have
not been verified.

## Current renderer limits

- `draw_text` iterates bytes, not decoded UTF-8 code points. This test uses ASCII.
- Geometry has capacity for 1000 glyphs per group. The dense phase stays below it.
- Text color currently retains RGB only; user-supplied alpha is not tested.
- `release()` is empty and there is no frontend release overload. The sample
  retains the renderer until application shutdown and relies on window/backend
  teardown for its GPU resources. It does not claim runtime unload support.

## Font assets

The test reads the font files already bundled in the motor submodule's
`externals/ocornut/imgui/misc/fonts` directory. It does not use ImGui to load or
render them and does not duplicate the font files. CMake supplies the absolute
source path, so the source checkout is required at runtime.

- Roboto Medium: Christian Robertson, Apache License 2.0.
- Cousine Regular: Steve Matteson; digitized data copyright 2010 Google
  Corporation, SIL Open Font License 1.1.

See the bundled `motor/externals/ocornut/imgui/docs/FONTS.md` for attribution
and license references.
