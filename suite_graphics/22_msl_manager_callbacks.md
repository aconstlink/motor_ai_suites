# msl manager callbacks

Regression test for `register_funk`, `unregister_funk` and `on_update`.
The default console mode uses the real manager and its worker thread to create
msls. No rendering window, assets or GPU are required in that mode.

Checks early and late registration, existing and newly ready msls, pending
objects, exactly-once delivery across repeated updates, two-backend readiness,
unregister before backlog delivery and before completion, completion without
listeners, registration-list growth, holes and reused slots.
Also verifies callback names, object pointers and execution on the update thread.

The console mode deliberately simulates backend readiness through the existing
`graphics::object::data_manipulator` API. It tests callback dispatch, **not**
shader compilation, GPU configuration or rendering. It does not exercise
reentrant callbacks or the timing-sensitive readiness change between the two
internal callback loops. All manager updates are serialized; callbacks do not
register or unregister listeners themselves.

```powershell
cmake --build build --config Release --target 22_msl_manager_callbacks --parallel 4
ctest --test-dir build -C Release -R "^graphics_msl_manager_callbacks$" --output-on-failure
```

Each worker handoff has a five-second timeout, and CTest limits the complete
process to 30 seconds. A failed check or remaining tracked allocation produces
a nonzero exit code. No engine changes are required.

## Real window lifecycle

The same executable also offers two explicit GPU/window modes, implemented in
[22_msl_manager_windows.cpp](22_msl_manager_windows.cpp):

```powershell
.\build\bin\Release\22_msl_manager_callbacks.exe --windows-gl
.\build\bin\Release\22_msl_manager_callbacks.exe --windows-d3d
```

The D3D11 mode requires Windows. Both modes close automatically after these steps:

1. Create one msl asynchronously, configure it through the manager and submit
   20 triangle frames in the first window.
2. Open a second window after the original configuration queues have drained.
   Verify that its frontend does not already report the msl ready, call
   `on_render_init(fe)` and wait for backend-specific `ready/ok` before drawing.
3. Register a late listener, receive the existing msl once and unregister it.
4. Close the second window through a window message. Call `on_render_release`
   in its last-frame callback and wait for the fence notification plus another
   20 submitted frames in the first window.
5. Open the secondary window again, configure the **same** msl object through
   `on_render_init`, submit 20 frames, then close it again.
6. Verify both last-frame callbacks, continued primary-window submissions and
   unchanged listener counts (one early notification, one late notification).

The test does not directly configure the shader: all shader configuration and
release calls go through the manager. Geometry and render state are configured
before the manager for each new window. It never re-adds the msl to compensate
for a missing per-window initialization call.

These modes are launched manually rather than by the default console CTest,
so headless test runs do not open windows. An internal 30-second watchdog reports
the current lifecycle step on failure. Readiness and submitted draw commands
are checked, but framebuffer pixels are not read back. The existing fence API
also invokes callbacks on cancellation; the fence alone does not prove GPU
execution or completed native-window destruction.

## Verification

2026-10-27, Windows/MSVC Release, motor `96fd381`: ten consecutive CTest
runs passed (0.02-0.12 seconds each). A separate console run passed all
checks and reported zero tracked bytes / no memory-manager entries.

The added GL4 and D3D11 window modes also passed in Release (approximately
3.6 and 3.0 seconds respectively), with successful shader compilation for all
three window instances, exit code 0 and empty memory-manager dumps. The console
CTest passed again after the extension. Linux was not tested.

D3D11 printed `[dx11_context]: GetClientRect failed.` during secondary-window
closure. The lifecycle checks still passed, with no crash or hang. This remains
an engine diagnostic to investigate separately; this test does not suppress it
or classify the entire run as free of backend errors.
