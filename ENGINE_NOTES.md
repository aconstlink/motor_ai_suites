# First integration observations

Tested on 2026-09-28 with motor `ff2211c`, Visual Studio 2026 / MSVC 19.51,
Windows x64, Release. No motor source changes were made.

## Verified

- Fresh recursive submodule checkout and CMake generation succeeded.
- The `00_triangle` target built successfully.
- OpenGL-only bounded run exited with code 0.
- Corrected dual-backend bounded run also exited with code 0.
- OpenGL and D3D11 rendered the same triangle and gradient in separate windows;
  both windows were visually inspected and both shader compilations succeeded.
- Closing the OpenGL window ended the dual-window application with code 0.
- Motor's final memory-manager report contained no entries in these runs.

These are smoke tests, not a GPU leak audit or pixel-exact regression test.
Debug builds and other platforms are not tested yet. Runtime window recreation
is covered by the subsequent lifecycle sample below.

## Findings

### Scalar vector construction in MSL

The original expression `in.pos.xy * 0.5 + vec2_t( 0.5 )` compiled on OpenGL,
but the D3D11 vertex shader failed with HLSL error X3014 (incorrect number of
arguments to a numeric-type constructor). The sample now uses
`vec2_t( 0.5, 0.5 )`, which works on both backends. This records a portability
limitation encountered by the sample, not a diagnosis of the generator.
The engine author confirmed that `as_vec2`, `as_vec3`, etc. are the existing
helpers for scalar-to-vector construction; no generator change is requested.

After shader compilation failed, render calls repeatedly logged an invalid
render-object ID; the application still exited with code 0. A process exit
code alone therefore cannot establish rendering correctness.

### Shutdown diagnostics

OpenGL-only and corrected dual-backend frame-limited runs logged
`[WGL Context] : wglMakeCurrent(00)` at shutdown. The initial failed dual-backend
run also logged a D3D11 resize failure while closing. The corrected dual run
closed manually without either message. These observations do not yet
establish the underlying cause.

### Integration footprint

The platform/application dependency chain also builds tool, scene, format
and audio code for this small sample. This affects the first-build cost;
no dependency restructuring was attempted.

The current engine requires C++20, so the application explicitly requests it
instead of carrying over the older C++17 setting from `motor_suites`.

Window titles supplied through `window_info` were replaced by backend/timing
titles during the test. The backend labels still allowed comparison.

## Runtime window lifecycle sample

`01_window_lifecycle` adds runtime D3D11 creation/destruction while the original
OpenGL window continues rendering. Neither motor nor `00_triangle` was changed.

- Release build succeeded.
- The automated `--smoke` run opened and closed D3D11 twice, observed successful
  shader readiness twice, and exited with code 0 and `smoke test passed`.
- The ImGui checkbox opened D3D11 during an interactive run.
- Changing the color through the ImGui picker produced matching turquoise
  triangles in OpenGL and D3D11, verified visually.
- Closing D3D11 through its native close button left OpenGL running and reset
  the checkbox. Reopening displayed the same chosen color, verified visually.
- The interactive run ended with code 0 and no Motor memory-manager entries.

D3D11 logged `D3D11 context resize failed` during secondary-window teardown,
including in the successful smoke test. Automatic application shutdown also
logged the previously observed WGL message. These remain engine follow-ups;
the sample does not attempt to repair either backend or hide those diagnostics.

UI automation could operate the color picker, but synthetic numeric typing
did not change its numeric fields in an earlier run. Physical keyboard input
has not been verified; this is not established as an engine defect.
