# Configure and Reconfigure

Build target: `04_reconfigure`.

The app shares one geometry, MSL object, variable set and render-state object
between GL4 and D3D11 windows. A green triangle is the visual reference.
No windows are recreated during this test: it isolates resource lifecycle
from the window lifecycle tested by 03_shared_scene.

## Interactive Controls

- **Configure again** reconfigures the same objects without releasing first.
- **Release -> Configure** releases backend resources, waits for all three
  objects to report `raw / ok`, then configures the same CPU objects again.
- **Backend** selects both windows or only GL4 / D3D11. The other backend
  continues rendering without receiving that operation.

The panel shows object states, results and completed configure cycles for
each backend. Every request is consumed separately by each selected window.
No affected draw is queued while release/configure is pending or failed.
The CPU objects and their variable values remain alive and unchanged.

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
as the buttons. `--reconfigure-only` or `--release-only` restrict the smoke
sequence to two cycles of that operation. These restrictions do not remove
buttons in interactive mode. Smoke has an overall 60-second timeout.

Exit 0 requires the requested sequence to complete and no remaining tracked
allocations at shutdown. A failure or timeout exits nonzero. Shader readiness
and recorded draws are checked, but pixel correctness is not automated.
Final shutdown release results are not included in the completed-cycle count.

## Results on Motor 5f8f05b + Local MSL Release Implementations

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
