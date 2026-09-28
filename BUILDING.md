# Building the samples

Initialize the engine and its dependencies:

```sh
git submodule update --init --recursive
```

Generate a Visual Studio solution (select the generator installed locally):

```sh
cmake -S . -B build -G "Visual Studio 18 2026" -A x64
cmake --build build --config Release --target 00_triangle --parallel
```

Run `build/bin/Release/00_triangle.exe` for an OpenGL window, or pass `--dual`
for OpenGL and D3D11 windows rendering the same geometry with the same MSL
shader. Closing either window ends this first sample. `--frames 180` provides
a bounded smoke-test run; it is not an image correctness test.

Open the generated solution to build and debug in Visual Studio. Add another
`.cpp` to `suite_graphics` and list it in that directory's `CMakeLists.txt` to
create another application target.

The motor submodule pins an engine commit. This application does not modify
the engine or use the separate development checkout in `motor_suites`.

## Runtime window lifecycle (Windows)

```sh
cmake --build build --config Release --target 01_window_lifecycle --parallel
```

Run `build/bin/Release/01_window_lifecycle.exe`. The OpenGL window contains
an ImGui checkbox for opening/closing a D3D11 window and a shared color editor.
The D3D11 window can also be closed using its title-bar close button, then
reopened from the checkbox. Closing the OpenGL window ends the application.
Geometry, MSL and the color variable set are shared between the windows.

UI edits are staged in application data and applied to the shader variable
once in `on_graphics`, before either window's render commands are recorded.
Each new frontend configures the shared objects on its first frame and
requests their backend release on its last frame. The application retains
the objects until shutdown.

For a bounded automated lifecycle check:

```sh
build/bin/Release/01_window_lifecycle.exe --smoke
```

This opens/closes D3D11 twice, changes colors between steps, checks that the
shader becomes ready after both openings, and exits. It returns a nonzero
code if the sequence fails or times out after 30 seconds. It requires an
interactive Windows desktop with OpenGL and D3D11 support; it does not compare
rendered pixels or certify that native graphics resources are leak-free.

## Dynamic geometry and variable sets

```sh
cmake --build build --config Release --target 02_dynamic_geometry --parallel
build/bin/Release/02_dynamic_geometry.exe --dual
```

One MSL object renders a triangle and an optional square with separate color
and position variable sets. Toggle Square to link/unlink its geometry and
create/drop its variable set at runtime. The shader is configured only once
per backend. Square geometry remains allocated until shutdown; toggling tests
links and variable sets, not repeated geometry allocation.

Omit `--dual` for OpenGL only. `--smoke` performs two add/remove cycles and
changes square parameters between cycles, with a 30-second timeout. Its exit
code checks lifecycle progress only, not pixel correctness. Closing either
window ends the application.

Known failure on the pinned engine: after removing the square, change its
color and add it again. Both backends still display the old color. See
`ENGINE_NOTES.md`; the sample deliberately retains this regression case.
