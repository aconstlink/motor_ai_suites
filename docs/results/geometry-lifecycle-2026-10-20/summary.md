# Geometry lifecycle regression, 2026-10-20

Engine: `origin/async_rendering`, commit `7c50472`.
Windows, MSVC, Release. No engine source changes.

Build:

```sh
cmake --build build --config Release --target 02_dynamic_geometry 04_reconfigure --parallel 2
```

Build succeeded without compiler warnings or errors in the captured build log.

| Test arguments | Backend | Exit | Memory-manager dump |
| --- | --- | --- | --- |
| `02_dynamic_geometry --smoke --geometry-lifecycle` | GL4 | 0 | Empty |
| `02_dynamic_geometry --smoke --geometry-lifecycle --dual` | GL4 + D3D11 | 0 | Empty |
| `04_reconfigure --smoke --gl-only` | GL4 | 0 | Empty |
| `02_dynamic_geometry --smoke` | GL4 | 0 | Empty |

No run reached its timeout.

## Coverage

The new optional `--geometry-lifecycle` mode in test 02 leaves the shader
configured while reconfiguring the triangle geometry four times per backend.
It also exercises two square link/unlink cycles, releasing the geometry after
each unlink and configuring it again on the next link. The test verifies
backend ready/raw states and operation results before resuming rendering.

The original test 02 smoke mode retains square geometry until shutdown.
It therefore additionally exercises shader release before releasing previously
unlinked geometry. Test 04 covers full reconfigure without release and
release followed by configure, along with texture-variable changes.

## Limits and diagnostics

- All runs still report `[WGL Context] : wglMakeCurrent(00)` at shutdown.
  The runs complete successfully despite this existing diagnostic.
- No pixel readback or visual verification was performed.
- An empty motor memory-manager dump does not prove absence of GL/driver leaks.
- Release builds disable `glGetError` polling through `NDEBUG`.
- Buffer-allocation failure was not injected; this does not exercise the
  `construct_geometry` failure-result path.
- VAO cache size/reuse is not instrumented. The test exercises the relevant
  reconfigure path but does not assert internal cache capacity.

Raw stdout/stderr logs are stored beside this report.
