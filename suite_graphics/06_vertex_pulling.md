# Vertex pulling

`06_vertex_pulling` compares indexed vertex pulling with a conventional vertex
buffer. Windows defaults to an OpenGL 4 window and a D3D11 window sharing the
same graphics objects. Other platforms default to OpenGL 4.

## Expected image

- Left: position and color fetched from an `array_object` using MSL `vertex_id`
  and `fetch_data`. The shader has no ordinary vertex inputs.
- Right: the same position and color supplied as vertex attributes.
- Both quads should have matching shapes and color gradients, apart from their
  horizontal offset. Their corners and colors animate together.
- `--still` freezes the initial data for comparison across windows.

Each array record contains two `vec4` values: position followed by color.
The shader reads texels `vid * 2` and `vid * 2 + 1`. The index buffer is
`{ 2, 0, 1, 0, 2, 3 }`, deliberately reusing vertices out of sequential order.
The pulling geometry still carries an unused zero-filled VB to exercise the
existing geometry/draw API; all actual positions and colors come from the array.
An ordinary `u_offset` uniform is used alongside the array binding.

Data is changed once in `on_graphics`. Each frontend independently submits
`update(array)` and `update(reference_geometry)` for the current revision before
rendering. Objects remain owned by the app until backend release/shutdown.

## Run

From the repository root:

```powershell
cmake -S . -B build
cmake --build build --config Release --target 06_vertex_pulling --parallel 4
.\build\bin\Release\06_vertex_pulling.exe
.\build\bin\Release\06_vertex_pulling.exe --still
.\build\bin\Release\06_vertex_pulling.exe --smoke
.\build\bin\Release\06_vertex_pulling.exe --gl-only --smoke
.\build\bin\Release\06_vertex_pulling.exe --d3d-only --smoke
```

Closing either window exits the app. The smoke mode waits for both shaders to
be ready on each active backend, at least 180 draw submissions and 60 buffer
updates per window (one update with `--still`). It has a 30-second timeout.
Exit code 1 indicates timeout, early closure, or a nonempty Motor memory dump.
This is a lifecycle/submission smoke test, not an automated pixel comparison:
the frontend does not expose the result of every queued update/draw here.

## Verification

Release build and GL-only, D3D-only, and dual-backend animated smoke runs passed
on Windows. Static and animated output was also visually inspected on both
backends: pulled and reference quads matched. Memory dumps were empty.

Known diagnostics, not fixed by this test:

- GL warns that the pulling geometry's vertex attributes are unused. This is
  intentional for this shader.
- Existing shutdown messages can include `wglMakeCurrent(00)` and, when closing
  the D3D window manually, `GetClientRect failed`; tested runs still exited 0.
- MSVC C4723 is locally suppressed around initialization because the current
  vertex/index buffer `resize` helpers divide by the initial zero element count
  when calculating a floating-point resize ratio. No engine code was changed.

Not covered: buffer resizing, stream-out, instancing, nonzero base-vertex,
reconfiguration, or window reopening. This is not a performance benchmark.
