# Motor GL4 frame-end glFinish A/B benchmark

This follow-up isolates removal of the unconditional `glFinish()` in GL4
`end_frame()`. `glFlush()`, buffer swapping, CPU command handoff, resource
ownership, locks and error checks remain unchanged. The ES3 and D3D11 backends
are unchanged. The [original comparison](RESULTS.md) remains a separate baseline.

## Method

The finish-on Release executable and its DLLs were saved before the change.
After rebuilding, SHA-256 comparisons confirmed that only `motor_platform.dll`
differed; the benchmark EXE and all other saved DLLs were identical. GL4 source
diff: one deleted `glFinish()` call. Both binaries run in the same measurement
series, alongside both raylib variants, with rotated ordering between repeats.

- Windows 11, i7-6700, RTX 2060, driver 32.0.15.9621; x64 Release.
- Motor base revision `cc4c402daf10b05404c1e158f15beb2449447330`.
- 1280x720 client area and matching 144 DPI, verified on every run.
- 100 / 1000 / 5000 unlit cubes, one draw per cube, no instancing or Scene Graph.
- 180 warmup frames and 600 measured intervals; three repeats per case.
- 36 sequential runs; no concurrent build or other test application.
- Normal desktop/browser applications remained active, not an isolated machine.

The measure is the CPU frame-start interval, **not** a GPU timestamp, display FPS
or end-to-end latency. Removing the finish permits GPU overlap but does not remove
driver/presentation backpressure. See the original report for the remaining Motor
versus raylib implementation differences and non-equivalent phase timings.

## Results on 2026-10-12

Milliseconds per CPU frame-start interval, median of three run medians:

| Objects / draws | Motor finish on | Motor finish off | Raylib three matrices | Raylib combined MVP |
|---|---:|---:|---:|---:|
| 100 | 2.053 | 1.881 | 0.771 | 0.922 |
| 1000 | 10.424 | 9.632 | 4.809 | 4.005 |
| 5000 | 44.585 | 44.899 | 20.686 | 19.558 |

The finish-off aggregate interval is **8.4% / 7.6% lower** at 100 / 1000 objects,
and **0.7% higher** at 5000. These are descriptive differences, not established
performance guarantees. Three repeats and active desktop load limit the precision;
the run ranges overlap substantially:

| Objects | Finish on: min-max run median | Finish off: min-max run median |
|---|---:|---:|
| 100 | 2.051-2.416 | 1.860-3.052 |
| 1000 | 10.239-15.641 | 9.464-14.245 |
| 5000 | 44.168-47.934 | 41.245-49.301 |

There is no clear gain at 5000 objects. Removing this wait alone does not explain
the gap to raylib in this workload. The results do not isolate the remaining
cost: profile CPU backend work and waits, and use GPU timestamps before attributing
it to uniforms, locking or GPU execution. A GPU-heavier scene may behave differently.

The removal avoids an unconditional GPU drain; it does **not** fix variable-set
snapshotting or otherwise make the entire backend fully asynchronous.

All 36 runs exited with code 0 and 600 measured samples. All 18 Motor runs had
empty memory-manager dumps; the known WGL shutdown diagnostic appeared in each.
All stderr files were empty. Reported draw counts remain application-side counts,
not an independent GPU capture.

## Lifecycle regression checks

All seven launches below exited with code 0 and an empty Motor memory-manager dump:

| Executable | Arguments | Coverage |
|---|---|---|
| `01_window_lifecycle` | `--smoke` | GL primary; D3D11 secondary opened/closed twice |
| `01_window_lifecycle` | `--smoke --gl-only` | GL secondary opened/closed twice; shaders ready after both openings |
| `03_shared_scene` | `--smoke --gl-only` | Shared scene and dynamic variable sets on GL |
| `03_shared_scene` | `--smoke` | Shared scene across GL/D3D11 with secondary reopen |
| `04_reconfigure` | `--smoke --gl-only` | Two reconfigure and two release/configure cycles; texture selection changes |
| `08_render_to_texture` | `--smoke --gl-only` | RTT lifecycle and framebuffer changes |
| `10_transform_feedback` | `--smoke --gl-only` | Transform feedback and cube-field rendering |

The reconfigure log confirms repeated shader compilation on the GL support thread
and return to ready/ok. This exercises recompilation of the same shader source;
it is not a test of arbitrary edited shader interfaces or all shared-context races.
These smoke tests assert lifecycle progress, not pixel correctness, GPU output
counts or driver-resource leak freedom. Linux/Intel hardware was not retested here.

The known `wglMakeCurrent(00)` shutdown diagnostic remains. Dual-backend lifecycle
runs also logged D3D11 `GetClientRect failed` during closure, without a failed test
or crash. Removing a GL frame-end wait does not fix window teardown ordering.

CPU borrowed-resource lifetime and GPU completion remain separate contracts.
Motor's frontend `fence` is a command-list callback, not a GPU fence. Cross-context
resource handoffs still require their own visibility/order protocol; a blanket
frame-end finish is not a substitute.

## Reproduction

Save a finish-on runtime before rebuilding; see the [runner instructions](README.md#repeated-motor-comparison).

```powershell
./suite_raylib/run_comparison.ps1 -BuildDirectory ./build -MotorFinishOnDirectory ./saved-finish-on -Objects 100,1000,5000 -Warmup 180 -Frames 600 -Repeats 3
```

`motor-gl` denotes the current finish-off build; `motor-gl-finish-on` denotes the
saved baseline. Metadata records both platform DLLs and source hashes. Neither
the runner nor a runtime toggle changes the engine's synchronization policy.

Saved data: [36 run summaries](results/2026-10-12-glfinish.csv),
[aggregates](results/2026-10-12-glfinish-summary.csv), and
[environment, source and binary hashes](results/2026-10-12-glfinish-environment.json).
CSV numeric strings retain the host's `de-DE` decimal comma inside quoted fields.

Unabridged benchmark logs remain locally under
`E:/repos_aconstlink/_build/motor_ai_suites.x64.vs2026/raylib-comparison/20261012-220837-982/`.
Regression stdout/stderr logs are under the same build's `glfinish-regression/`.
The saved finish-on runtime is in `glfinish-ab/finish-on-20261012-220339/`.
