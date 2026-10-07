# Configure and Reconfigure

Build target: `04_reconfigure`.

The app shares one quad geometry, MSL object, two active variable sets,
two procedural textures and a render-state object between GL4 and D3D11 windows.
Two textured quads are the visual reference. Initially the left quad has a blue,
coarse checkerboard (A), the right quad an orange, fine checkerboard (B).
Both textures are 128x128 RGBA with nearest filtering and require no asset files.
Variable-set ID 1 is dropped; sets 2 and 0 are rendered in that order.
No windows are recreated during this test: it isolates resource lifecycle
from the window lifecycle tested by 03_shared_scene.

## Interactive Controls

- **Configure again** reconfigures the same objects without releasing first.
- **Release -> Configure** releases backend resources, waits for all five
  objects to report `raw / ok`, then configures the same CPU objects again.
- **Backend** selects both windows or only GL4 / D3D11. The other backend
  continues rendering without receiving that operation.
- **Left texture / Right texture** change only that quad's `u_tex` variable.
  No shader or image configure is sent. These shared selections affect both
  windows, independently of the Backend selector for lifecycle operations.

The panel shows object states, results and completed configure cycles for
each backend. Every request is consumed separately by each selected window.
No affected draw is queued while release/configure is pending or failed.
The CPU objects remain alive throughout. Texture-variable changes are applied
once in `on_graphics`; each backend then processes them in its own render call.

A failed release stops that cycle; it is not hidden by an immediate configure.
In interactive mode, the failure remains visible and Configure again can be
used to attempt recovery once no commands remain in transit. Results are
also logged through motor::log. Individual operations time out after 15 seconds.

## Automated Runs

```powershell
cmake --build build --config Release --target 04_reconfigure --parallel 4
build/bin/Release/04_reconfigure.exe --smoke --gl-only --reconfigure-only
build/bin/Release/04_reconfigure.exe --smoke --d3d-only --release-only
build/bin/Release/04_reconfigure.exe --smoke
```

Default: both backends on Windows, GL4 only elsewhere.
`--gl-only` and `--d3d-only` isolate a backend. `--smoke` runs two reconfigure
cycles followed by two release/configure cycles, using the same request path
as the buttons. Before the first cycle and after each cycle it changes the
texture of just one quad, alternating sides. Each backend must submit at least
30 frames using the current texture revision before advancing. Thus a later
reconfigure cannot immediately mask a missing texture-variable update.
`--reconfigure-only` or `--release-only` restrict the smoke
sequence to two cycles of that operation. These restrictions do not remove
buttons in interactive mode. Smoke has an overall 60-second timeout.

Exit 0 requires the requested sequence to complete and no remaining tracked
allocations at shutdown. A failure or timeout exits nonzero. Shader readiness
and recorded draws are checked, but pixel correctness is not automated.
Final shutdown release results are not included in the completed-cycle count.

## Texture Extension Verification

Release build passed with the user's current D3D11 indirection changes,
including the corrected `varset_to_idx[vsid].idx` texture lookup.

| Run | Result |
| --- | --- |
| Both backends, full smoke sequence | Exit 0, empty memory dump |
| D3D11, reconfigure-only smoke | Exit 0, empty memory dump |
| GL4, release-only smoke | Exit 0, empty memory dump |

The smoke runs exercised individual texture-variable changes before and after
resource lifecycle operations. No engine changes were made for this extension.
The known GL4 `wglMakeCurrent(00)` shutdown diagnostic remains.

Visual verification is still pending: Computer Use access to the interactive
test was not approved. That separate interactive process was stopped and is
not counted as a passing test. In a manual run, change just the right texture
(set 2), verify the left quad stays unchanged in both windows, then repeat after
each lifecycle operation. Both quads should retain their independently selected
checkerboards. A passing smoke alone does not verify those pixels.

## Historical Results Before the Texture Extension

These results refer to the earlier, single-color triangle version, on Motor
5f8f05b plus local MSL release implementations, not the new texture regression.

Release builds of 04_reconfigure and 03_shared_scene passed. Retested with
the user's uncommitted release implementations in gl4.cpp and d3d11.cpp:

| Run | Result |
| --- | --- |
| GL4, two reconfigure cycles | Exit 0 |
| D3D11, two reconfigure cycles | Exit 0 |
| GL4, two release/configure cycles | Exit 0 |
| D3D11, two release/configure cycles | Exit 0 |
| Both, two reconfigure + two release/configure cycles | Exit 0 |
| 03_shared_scene, secondary window close/reopen/close | Exit 0 |

All six runs ended with an empty Motor memory dump. Release now reports
success and each release/configure cycle reaches rendering again.
The known `wglMakeCurrent(00)` shutdown message remains in GL4 runs;
the window lifecycle test also logs `GetClientRect failed` while closing.

Review caveat: the new release_msl_data currently invalidates only the MSL
entry. It does not invalidate the corresponding _renders and _shaders entries.
Those backend resources and variable-set references can remain resident until
later reconfiguration or backend destruction. Passing this smoke test does
not demonstrate immediate release of those resources.

These are status/lifecycle checks, not proof of visual correctness or GPU
resource leak freedom. In particular, frontend object status is the observable
contract here; if it preserves earlier success, it can hide a later backend
failure. Check the image and backend diagnostics as well.
