# Building the samples

## Wire console tests

Build targets `00_wire_slots` and `01_wire_bridges`, then run:

```sh
ctest --test-dir build -C Release -L wire --output-on-failure --no-tests=error
```

The slot test covers initial values, fan-out, explicit exchange, independent
change flags, disconnect/reconnect, type rejection and release. The bridge
test covers input transfer, output-to-two-subsets transfer, staged updates,
independent variable ownership and rebinding. Assertions remain active in
Release through explicit checks, and remaining Motor allocations fail the run.

Motor is pinned to `52b0177`, with separate input/output bridges. All bridge
tests run without a compatibility/skip path. Additional scenarios verify
replacement of whole variable sets while preserving same-type connections,
and float-to-int slot replacement with disconnection of old endpoints.
Run `01_wire_bridges input`, `output`, `rebind`, `replacement`,
`input_type_change` or `output_type_change` for an individual scenario.

`00_wire_slots borrow` / `wire_sheet_borrow` checks that repeated `borrow_or_add`
does not acquire an extra reference. At `52b0177` its existing-entry path calls
`motor::share`, so this regression test is expected to report a memory leak.
It remains a normal failing test, not a skipped or inverted test.

## Concurrent console tests

```sh
cmake --build build --config Release --target 00_threads_parallel_for --parallel 4
ctest --test-dir build -C Release -L concurrent --output-on-failure --no-tests=error
```

The five scenarios cover four native threads, Motor's thread pool, empty/small/
threshold/large parallel ranges with nonzero offsets, repeated remainder ranges,
and nested `parallel_for` calls. Atomic visit counters and a serial checksum
detect missing, duplicate and out-of-range work. Checks remain active in Release.
Motor's final memory report must be empty. CTest enforces a 90-second timeout per
scenario and runs them serially to avoid competing pools.

No window, graphics backend, audio device or assets are used. The Windows and Linux
CTest workflows run these tests in Debug and Release, separately from the full
build workflows. They are correctness smoke tests, not
performance benchmarks or proof of race freedom. See the remainder-capture
finding in `ENGINE_NOTES.md` even if all tests pass.

To run one case directly, pass `threads`, `pool`, `ranges`, `remainder` or `nested`
to `build/bin/Release/00_threads_parallel_for.exe` (CTest supplies the timeout;
direct execution does not). Substitute Debug for Release to test a Debug build.

## Initial setup

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

## Runtime window lifecycle

```sh
cmake --build build --config Release --target 01_window_lifecycle --parallel
```

Run `build/bin/Release/01_window_lifecycle.exe`. The OpenGL window contains
an ImGui checkbox for opening/closing a D3D11 window and a shared color editor.
The D3D11 window can also be closed using its title-bar close button, then
reopened from the checkbox. Closing the OpenGL window ends the application.
Geometry, MSL and the color variable set are shared between the windows.

On Linux, run `build/bin/01_window_lifecycle`. Both windows use GL4 with
independent GLX contexts. An X11 display (including XWayland/WSLg) with OpenGL 4
support is required.

UI edits are staged in application data and applied to the shader variable
once in `on_graphics`, before either window's render commands are recorded.
Each new frontend configures the shared objects on its first frame and
requests their backend release on its last frame. The application retains
the objects until shutdown.

For a bounded automated lifecycle check:

```sh
build/bin/Release/01_window_lifecycle.exe --smoke

# Linux, single-configuration build
build/bin/01_window_lifecycle --smoke
```

This opens/closes the secondary window twice, changes colors between steps, checks that the
shader becomes ready after both openings, and exits. It returns a nonzero
code if the sequence fails, times out after 30 seconds, or leaves entries in
Motor's memory manager. On Windows it requires OpenGL and D3D11 support;
on Linux only OpenGL is used. It does not compare rendered pixels or certify
that native graphics resources are leak-free.

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
