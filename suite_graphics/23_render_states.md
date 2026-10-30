# Render States

Target: `23_render_states`. No external assets or engine modifications.

Twelve fields render shared geometry and an msl shader through indexed sets of
a `state_object`. Windows opens gl4 and d3d11 by default; other platforms use gl4.
The scene uses clip-space geometry, deliberately avoiding camera/projection
differences. The two winding variants use separate geometry links.

## Run

For square or portrait recordings, use `--youtube-short square|vertical`.
Add `--gl-only` or `--d3d-only` for a single window. See
[Short Video Capture](../README.md#short-video-capture) for dimensions and examples.

```powershell
cmake --build build --config Release --target 23_render_states --parallel 2
build/bin/Release/23_render_states.exe
build/bin/Release/23_render_states.exe --smoke
build/bin/Release/23_render_states.exe --smoke --gl-only
build/bin/Release/23_render_states.exe --smoke --d3d-only
ctest --test-dir build -C Release -R "^graphics_render_states_cpu$" --output-on-failure
```

`--cpu-only` runs without graphics windows. D3D11 requires Windows.

## Visual Reference

Fields are ordered left to right, top to bottom. Every field ends with a small
solid green marker at its lower right, drawn after popping its special states.
Missing, clipped, transparent or wireframe markers indicate state leakage.

| Field | Expected result |
| --- | --- |
| Depth off | The later blue rectangle covers the orange overlap. |
| Depth LESS | The nearer orange rectangle remains in front of the later blue rectangle. |
| Depth LESS_EQUAL | Both rectangles have equal depth; the later blue rectangle covers the overlap. |
| Depth write off | The near orange draw does not write depth; the later, farther blue draw remains visible in the overlap. This uses two nested state pushes. |
| Opaque | The later orange rectangle replaces blue despite its alpha of 0.5. |
| Alpha | Orange at alpha 0.5 mixes with the underlying blue/background. |
| Additive | The overlap is brighter than either individual rectangle. |
| Viewport + pop | A blue square is contained in a smaller, offset viewport; the green marker is outside it after pop. |
| Cull back | Only one of the two opposite-winding rectangles remains. |
| Cull front | The other rectangle remains; the result is complementary to Cull back. |
| Wireframe | Rectangle outlines and triangulation diagonals, not solid fills. |
| Scissor + pop | Rectangles are clipped to a narrow region on the left; the green marker is outside that region. |

The labels and controls use ImGui; the fields and markers use motor draw calls.
Stencil is not covered because motor's stencil state currently has no operations.

The header states the first row's draw order: **orange first, blue second**.
Orange is nearer except in LESS_EQUAL, where both depths are equal. Hover a field
title or its `(?)` marker for draw order, depth relationship, active state and the
expected result. The wireframe explanation follows the solid-mode checkbox.
The remaining two-rectangle fields draw **blue first, orange second**; the
viewport field draws only blue before restoring the viewport and drawing green.

## Controls and Lifecycle

- **Reconfigure** configures the state objects again without releasing them.
- **Release / configure** waits for `raw / ok`, then configures again.
- **Solid instead of wire** changes the polygon fill mode through reconfigure.
- Resizing updates viewport and scissor rectangles in pixels.

Requests are consumed independently by each window. Geometry, shader and draw
variable sets remain shared. Each window owns its state object because viewport
and scissor dimensions may differ. No affected draws are submitted while state
release/configure is pending. All states are popped before ImGui rendering.

## Automated Coverage

The CPU CTest checks partial state inheritance, explicit disable, restoration,
clear suppression during push/pop, indexed mutation and multiple sets per object.

GPU smoke runs four phases: initial configuration, solid-mode reconfigure,
wireframe restoration, then release/configure. Each window must submit at least
30 frames per phase. The test checks resource statuses, completed configure
cycles and the final motor memory dump, with a 30-second application timeout.

**A smoke pass does not verify pixels.** State-object status and successful draw
submission cannot establish that driver state or the visible result is correct.
Use the visual table above for that separate check. A screenshot from each
backend is enough for a first comparison; desktop automation is not required.

## Verification, 2026-10-29

Windows Release build with motor `578afa7`:

- CPU CTest: passed, nine checks.
- Dual-window gl4/d3d11 smoke: all four phases passed, exit 0.
- Final tracked memory: 0 bytes, no entries.
- Visual verification: pending user inspection; desktop capture was declined.

No motor source files were changed. Linux and Debug were not tested in this run.

### Screenshot Follow-Up

User screenshots showed missing fields on gl4 and collapsed rectangles on d3d11.
The initial lifecycle smoke success therefore did **not** mean visual correctness.
Two test setup assumptions have been removed:

- Active `cull_mode::none` reaches `glCullFace(GL_NONE)` in gl4, which is not a
  valid argument. The previous cull mode remains active. The test now explicitly
  uses back-face culling with counter-clockwise front faces, including wireframe.
- The msl expression `in.pos.xy * u_rect.zw` generated HLSL
  `mul(u_rect.zw, in_pos.xy)`, a dot product, not component-wise scaling.
  This was incorrect operator usage in the test, not a translation bug in msl.
  The test now uses msl's component-wise multiplication operator `'`:
  `(in.pos.xy ' u_rect.zw) + u_rect.xy`. The temporary scalar workaround has
  been removed. Generated HLSL was inspected using a temporary diagnostic
  outside the repo.

A separate engine issue remains exposed by this test: d3d11's pop path removes
the child stack entry and computes the CPU state difference, but binds the native
depth/blend/rasterizer objects from that removed child entry. It does not bind
the remaining parent's native objects. Thus CPU merge checks can pass while GPU
state restoration is wrong. No workaround is applied in this test. Inspect the
green markers, nested depth-write field and states immediately following a pop.

The user performs visual verification; no further desktop automation is needed.
