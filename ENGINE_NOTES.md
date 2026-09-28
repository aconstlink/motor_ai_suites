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
Runtime window recreation, Debug builds and other platforms are not tested yet.

## Findings

### Scalar vector construction in MSL

The original expression `in.pos.xy * 0.5 + vec2_t( 0.5 )` compiled on OpenGL,
but the D3D11 vertex shader failed with HLSL error X3014 (incorrect number of
arguments to a numeric-type constructor). The sample now uses
`vec2_t( 0.5, 0.5 )`, which works on both backends. This records a portability
limitation encountered by the sample, not a diagnosis of the generator.

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
