# Building the first sample

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
