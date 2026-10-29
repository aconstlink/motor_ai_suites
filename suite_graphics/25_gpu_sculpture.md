# GPU Sculpture

`25_gpu_sculpture` simulates 100,000 elements using gl4 transform feedback or
d3d11 stream output. Position and velocity persist on the GPU. Spring forces
pull the elements towards a sphere, a spiral or a wave surface, with damping
and optional swirl. The default cycle changes formation every eight seconds.

This complements [24 Buffer Sculpture](24_buffer_sculpture.md): the CPU creates
the seeds once, then updates only camera and control uniforms. There are no
recurring per-particle CPU loops, particle-buffer uploads or GPU readbacks in
the application. The test uses motor objects and msl, without engine changes.

## Run

```powershell
cmake --build build --config Release --target 25_gpu_sculpture -- /m:2
build/bin/Release/25_gpu_sculpture.exe
build/bin/Release/25_gpu_sculpture.exe --d3d-only
build/bin/Release/25_gpu_sculpture.exe --dual
build/bin/Release/25_gpu_sculpture.exe --no-ui
build/bin/Release/25_gpu_sculpture.exe --dual --smoke
ctest --test-dir build -C Release -R graphics_gpu_sculpture_cpu --output-on-failure
```

Default: one gl4 window at 1920x1080. `--dual` opens gl4 and d3d11 at 840x720
each; `--d3d-only` selects a single d3d11 window. d3d11 requires Windows.
VSync is requested on unless `--no-vsync` is supplied. `--no-ui` hides the
sample controls for recording; `--still` starts paused. Closing either window
closes the application.

`--count` accepts 1 to 500,000 elements and fixes the capacity at startup.
The UI exposes formation, palette, spring strength, damping, swirl, cube size,
simulation speed, camera, pause and reset. Changing appearance does not reset
the simulation. Reset replays the already uploaded seed geometry; it does not
upload a new particle array.

## GPU Data Flow

Each record contains two float4s (32 bytes):

| Field | xyz | w |
| --- | --- | --- |
| position | Simulated position | Stable normalized element index |
| velocity | Simulated velocity | Stable angular seed |

The velocity uses the existing `color0` vertex semantic as a transport channel;
it is not a color in the simulation. The display shader derives color from the
seeds and velocity magnitude. `as_vec3(glow)` broadcasts its brightness term.

1. Configure the point seed geometry, a `streamout_object`, and two msl objects.
2. The first simulation pass reads the seed geometry and captures position and
   velocity without a pixel shader or rasterization.
3. Later simulation passes set `feed_from_streamout` and read the previous
   result. The backend writes to the other internal buffer and swaps the pair.
4. The display pass reads the latest captured points. Its geometry shader
   expands each point into a shaded cube; the draw count comes from the captured
   stream through the existing transform-feedback / DrawAuto path.

A single motor streamout object already owns **two internal GPU buffers**.
At 100,000 elements these occupy 6,400,000 bytes per backend, plus the separate
3,200,000-byte seed vertex buffer. The CPU retains the seed geometry but never
rewrites it during animation. Both windows have independent backend storage and
initialization/reset tracking.

There are normally **two scene draws per window**: simulation and display,
plus ImGui draws. Pause skips simulation after initialization; reset while
paused performs one capture with a zero time step. Display continues while
paused. The test never filters out elements, so the captured count stays fixed.

## Timing and Scope

The simulation uses four semi-implicit spring substeps inside each vertex-shader
invocation, not four capture passes. The frame delta, multiplied by simulation
speed, is clamped to 1/30 second before subdivision. Long stalls deliberately
slow simulation time instead of triggering unbounded catch-up work.

The UI shows CPU camera/uniform preparation, a smoothed application frame
interval and submitted pass counts. These are not GPU timings. Particle
uploads are zero after initialization, but uniforms are still updated normally.

100,000 cubes expand to 2.4 million geometry-shader output vertices and
1.2 million triangles per displayed frame per window. Moving simulation to the
GPU removes the sample's CPU particle-update loop; it does not remove geometry
expansion, rasterization or backend work. This is not a like-for-like rendering
benchmark against test 24, which uses indexed vertex pulling instead of a
geometry shader. Test one window when evaluating one backend's frame times.

## Verification

The windowless CTest checks finite seeds at capacities 1, 17, 100,000 and
500,000, the 32-byte vertex layout and time-step clamping. A CPU numerical
reference checks spring convergence/stability across the exposed spring and
damping ranges. It does not read GPU state or prove shader equivalence.

The bounded `--smoke` run checks shader readiness and seven submission phases:
initial seed and feedback, spiral, waves, pause, reset while paused, resume and
automatic formation. Each selected backend must submit at least 24 display
frames per phase. It checks seed/feedback cadence separately for each window,
exits automatically and fails on compilation failure, a 60-second timeout or
nonzero tracked memory after cleanup.

The smoke verifies command submission and lifecycle, not rendered pixels or
GPU numerical results. Visual motion, framing and matching backend output
remain a separate check; no desktop automation is used.

### Checked on 2026-10-29

- Windows / MSVC, motor `5a80485`, unchanged.
- Release and Debug builds passed. The new CPU CTest and the existing buffer
  sculpture / render-state CPU CTests passed in both configurations (3/3 each).
- Dual-backend smoke with 100,000 elements passed all seven phases in Release
  and Debug. The one-element boundary also passed in Release. Both GLSL and
  HLSL compiled successfully. All these runs exited with code 0 and an empty
  motor memory dump.
