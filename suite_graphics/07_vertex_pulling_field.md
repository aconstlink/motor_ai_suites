# Vertex Pulling Field

`07_vertex_pulling_field` renders a wave field of rounded cubes using two array buffers:
one stores the shared cube's positions/normals, the other stores each object's
position, scale and color. MSL derives object and mesh indices from `vertex_id`.
The shader animates height and rotation and applies simple directional lighting.
Each face is subdivided before its vertices are projected onto a rounded box.
The additional triangles therefore describe curved edges and corners, not just
duplicate geometry. Normals follow the rounded surface.

There is one indexed scene draw per window, plus separate ImGui draws in the
primary window. This is batched vertex pulling, not hardware instancing: the
index buffer and unused dummy VB still grow with the maximum cube count.

## Run

```powershell
cmake --build build --config Release --target 07_vertex_pulling_field --parallel 4
build/bin/Release/07_vertex_pulling_field.exe
build/bin/Release/07_vertex_pulling_field.exe --side 96 --subdivisions 8 --no-vsync
build/bin/Release/07_vertex_pulling_field.exe --side 48 --subdivisions 16 --smoke
build/bin/Release/07_vertex_pulling_field.exe --side 8 --subdivisions 16 --still
```

Default: 48x48 cubes, 8 subdivisions per face edge, and GL4 plus D3D11 on Windows,
GL4 only elsewhere. This submits 1,769,472 triangles per window, 64 times the old
12-triangle cube workload.
`--side 4..96` controls capacity. `--gl-only` and `--d3d-only` select one backend.
`--subdivisions 1..16` selects mesh density at startup. `--no-vsync` requests
disabled VSync for load experiments; the OS/driver may still limit presentation.
`--still` starts paused. ImGui controls active cube count, wave amplitude,
speed, camera yaw, orbit, pause and palette. Closing either window exits.

## Workload

For `N` subdivisions, each cube references `6 * (N+1)^2` mesh vertices and
`12 * N^2` triangles. Only the shared mesh array contains positions and normals;
the dummy vertex buffer and index buffer are sized for all objects.

| Side | Subdivisions | Cubes | Triangles per window | Dummy VB + IB per backend |
| --- | --- | --- | --- | --- |
| 48 | 1 | 2,304 | 27,648 | 0.95 MiB |
| 48 | 8 (default) | 2,304 | 1,769,472 | 33.06 MiB |
| 48 | 16 | 2,304 | 7,077,888 | 126.72 MiB |
| 96 | 8 | 9,216 | 7,077,888 | 132.26 MiB |

ImGui reports submitted triangles, referenced vertices, buffer capacity and UI
frame rate. These are not GPU invocation counts or GPU timings. Two windows
submit the workload twice. Buffer capacity does not shrink when reducing the
active cube slider. Combinations requiring more than 256 MiB of dummy VB + IB
per backend are rejected before allocating. CPU copies, array buffers and driver
overhead require additional memory. At high density, many triangles are smaller
than a pixel; use a small field to inspect the rounded mesh visually.

Each window owns a `motor::gfx::generic_camera`. `make_orthographic` handles
projection and aspect ratio; `look_at` handles the orbit frame. `on_graphics`
updates the cameras and writes `projection * view` into each window's variable
set. Resize events update that window's aspect ratio. No camera matrices are
assembled manually. Small fields keep extra framing space for wave height.

## Verification

The bounded smoke run draws 1, capacity/4, capacity and one row of cubes,
with a palette-buffer update between phases. It waits for shader readiness,
draw submissions and buffer-update submissions on every active backend.
Timeout is 120 seconds. It does not automatically compare pixels or time GPU work.

Release verification on 2026-10-06:

- Dual-backend smoke: side 48/subdivisions 8, side 96/subdivisions 8 without
  VSync, and side 48/subdivisions 16 passed, exit 0 and empty Motor memory dumps.
- Zero subdivisions and side 96/subdivisions 16 were rejected; the latter
  exceeds the buffer memory budget.
- A paused side 8/subdivisions 16 scene displayed rounded cubes under GL4 and
  D3D11. Closing it returned exit 0 with an empty Motor memory dump. This is a
  manual mesh check, not a pixel-equivalence regression test.

The final panel-only adjustment encountered unrelated concurrent D3D11 backend
edits: accesses to the removed `cbuffer::var_set_idx` blocked a full dependency
rebuild. Those engine edits were left untouched; the sample can be rebuilt
against the previously successful Motor libraries using MSBuild's
`/p:BuildProjectReferences=false`. The runs above validate those earlier binaries,
not the unfinished backend edits.

GL's unused-position-attribute warning is expected for the dummy VB. Known
`wglMakeCurrent(00)` and occasional D3D11 `GetClientRect failed` shutdown
diagnostics remain. This is not a benchmark.
