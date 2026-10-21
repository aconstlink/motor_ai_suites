# Async rendering handle regression

Date: 2026-10-20. Engine: `origin/async_rendering`, commit `24e117b`.
Windows / MSVC / Release / GL4. No engine source changes.

## Build

Tests 02, 04 and 08 built successfully. Test 13 initially failed to compile
because its fence callbacks still accepted no arguments. The test was adapted
to accept `frontend::fence_phase` and increment its counters only in the
`execute` phase, then built successfully.

## Runs

| Command | Exit | Timeout | Final motor memory dump |
| --- | --- | --- | --- |
| `02_dynamic_geometry --smoke --geometry-lifecycle` | 0 | No | Empty |
| `04_reconfigure --smoke --gl-only` | 0 | No | Empty |
| `08_render_to_texture --smoke --gl-only` | 0 | No | Empty |
| `13_live_recompile --smoke --gl-only` | 0 | No | Empty |
| `13_live_recompile --smoke --gl-only --close-on-submit` | 0 | No | Empty |

The live-recompile smoke intentionally compiles invalid shaders and verifies
recovery. Their compiler error messages are expected.

## Review findings despite passing smoke checks

1. `platform/graphics/gl4/gl4.cpp:1149`: the `return true` left inside
   `release_render_data(checked_handle_t)` exits the entire function.
   `_renders.invalidate(rd.oid())` at line 1152 is unreachable. Render data,
   VAOs and variable references are not cleared at that release point.
   Reconfigure can reuse the still-valid named entry and append geometry
   links instead of rebuilding from cleared data.
2. `gl4.cpp:1106` calls release even for a newly created render object.
   The new invalid-handle branch at line 1125 logs
   `Unknown render object id -1` for this normal initial-configure path.
   These diagnostics appear in all four regular smoke logs.

The final empty memory dumps do not rule out resources being retained until
backend destruction, and the tests do not inspect internal VAO-cache sizes.

## Limits

These are lifecycle/status smoke tests, not pixel verification or a benchmark.
No Debug build was run: checked-handle assertions and GL error polling are
disabled by `NDEBUG` in these runs. The existing `wglMakeCurrent(00)` shutdown
diagnostic remains. Close-on-submit exercises enqueue/teardown timing, but
does not guarantee overlap with the compiler thread.

Process exits are stored beside this report. Generated console logs are not retained.
