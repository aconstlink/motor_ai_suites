# Live Recompile and Lock Costs

The [render-access and runtime compilation comparison](RENDER_ACCESS_AND_COMPILE_COSTS.md)
adds actual OID/status/variable lookup measurements and compares motor GL4's
support thread with synchronous raylib and a test-supplied raylib compiler worker.

Two focused tests for the existing renderer. Neither introduces frame snapshots,
changes lock ownership, nor removes synchronization.

The later [GL4 owner-thread lock experiment](GL4_OWNER_THREAD_EXPERIMENT.md)
compares the new messaging backend with and without `datas` locks. Its engine
changes are local and experimental, not a replacement for the tests below.

## Build

```sh
cmake --build build --config Release --target 13_live_recompile 05_datas_cost 04_datas_locking --parallel 2
```

On Windows, executables are in `build/bin/Release`; with a single-configuration
generator they are normally in `build/bin`.

## 13: Live Shader Recompile

[Source](../suite_graphics/13_live_recompile.cpp)

```sh
13_live_recompile --smoke --gl-only
13_live_recompile --smoke --d3d-only
13_live_recompile --smoke --dual
13_live_recompile --smoke --gl-only --valid-only
13_live_recompile --gl-only --close-on-submit
```

Without `--smoke`, use the A, B + tint and Invalid buttons. The default is one
OpenGL window. D3D11 and mixed-backend dual mode require Windows.

`--valid-only` starts with A and replaces the invalid revision with A. It isolates
successful layout recompilation when the error-handling path itself is broken;
it does not replace the original negative test. `--close-on-submit` closes the
application after submitting the first tested shader configuration. Compilation
may already have finished by shutdown, so this is not a deterministic test of
compiler overlap. A crash, timeout, or non-empty motor memory dump still fails.

- Twelve quads share one msl object and geometry, with separate variable sets.
- A uses `u_color`. B adds `u_tint`, changing the shader's active uniform layout.
- The smoke sequence is invalid -> A -> B -> A -> B -> invalid -> A -> B -> A.
- The invalid variant deliberately references an undeclared variable. Compiler
  error messages during this phase are expected, not a successful compilation.
- Each backend must independently acknowledge the expected result. A shared
  compilation listener is deliberately not used as a per-backend completion flag.
- A fresh invalid shader must reach `raw / failed`. After an earlier successful
  configure, motor deliberately preserves `ready / ok` for the existing resource
  (`object::data_manipulator::change_to_ready`). The test accepts that policy for
  the later invalid recompile; that state alone does **not** prove the latest
  compilation succeeded or failed. Compiler diagnostics establish the negative
  recompile in the recorded runs. There is no separate per-command compile result
  asserted in this phase.
- An unchanged shader renders a moving green quad while the tested shader is
  pending or invalid. The pending shader itself is not drawn until ready.
- Source and layout data are changed only after all participating backends finish
  the previous revision. No concurrent source mutation is being tested.
- Command fences check command-queue progress; they do **not** prove GPU completion
  or verify pixels. The smoke run requires 24 post-completion command rounds per
  revision. Compilation may be too quick to span many visible frames.

Visually verify that all twelve quads remain in their grid, B changes their tint,
the green quad moves, and recovery restores the full grid in both backends.
The application starts with the invalid variant, which intentionally hides the grid;
select A or B to begin drawing in interactive mode. This test does not require
preservation of the last successfully compiled GPU program.

There is a 20-second application-level operation timeout. A render-thread deadlock
can prevent application callbacks from running, so unattended runs also need an
external process timeout (for example 90 seconds). Graphics tests are not added
to the default headless CTest suite.

## 05: Isolated `datas` Costs

[Source](../suite_concurrent/05_datas_cost.cpp)

```sh
05_datas_cost --smoke
05_datas_cost --benchmark
ctest --test-dir build -C Release -R concurrent_datas_ --output-on-failure
```

The smoke run uses 2,000 iterations per worker; the benchmark uses 100,000.
Both validate payloads and measurement counters, not speed thresholds. There is
an independent 30-second watchdog and a 40-second CTest timeout.

Workloads:

- One worker repeatedly accesses an existing ID.
- Recursive access to the same entry on one thread.
- Two workers access different entries in the same container.
- Two workers contend for the same entry.
- One worker accesses an existing entry while the other creates 64 entries.
- A held entry receives 100 unsuccessful `try_access` attempts from another thread.

Every timed workload has three controls: original engine `datas`, recording
disabled, and counters only. Logging, initialization and warmup are outside the
measured interval. Thread joining is included in wall time; this matters especially
for the short smoke runs. Repeat benchmark runs before drawing conclusions.

### Instrumentation

The engine's `datas<T>` is restored to its version before the diagnostic changes
in commit `59fe05f`. No diagnostics header, template parameter or build entry
remains in motor. The uninstrumented baseline uses this actual engine template.

Only the test uses a separate copy,
[`support/measured_datas.hpp`](../suite_concurrent/support/measured_datas.hpp),
with counters in
[`support/datas_diagnostics.hpp`](../suite_concurrent/support/datas_diagnostics.hpp).
These live in `motor::tests`, not the engine's platform namespace. Instrumentation
surrounds the original lock operations without changing their acquisition order.
This is a snapshot: future engine changes must be reviewed against the test copy
before interpreting new measurements.

`motor::tests::diagnostic::local` is thread-local. Each worker copies its own
counters after its workload, and the test aggregates after joining. No shared
counter mutex is introduced. Recording can be off, counters only, or timings.

The output separates:

- Container read/write lock acquisition: acquisition overhead **plus** any wait.
- Entry guard acquisition, including its mutex, recursive checks and any wait.
- Callback elapsed time, excluding entry-lock acquisition and release.
- Recursive entries and failed try attempts.

Callback timing is **inclusive**: a recursive access is inside the outer callback.
Do not sum all categories as independent costs. Concurrent totals are sums across
threads, not frame latency. Lock release, name lookup, allocation and callable
wrapping are not separately instrumented; they still contribute to wall time.
Clock calls themselves perturb this small workload, hence the controls.

This is a CPU microbenchmark comparing motor's actual `datas` template with a
test-only instrumented copy and dummy payloads, not GPU profiling or real shader-compilation
contention. In particular, it cannot attribute the motor/raylib frame-time gap to
locks. The structural-writer workload only models concurrent container access.

## Verification: 2026-10-13

Windows, MSVC Release; motor baseline `1b40c53`, suites baseline `20c636f`, plus
the test and diagnostic changes described here:

| Check | Result |
| --- | --- |
| GL4 live-recompile smoke | Passed, exit 0 |
| D3D11 live-recompile smoke | Passed, exit 0 |
| GL4 + D3D11 shared-object smoke | Passed, exit 0 |
| `concurrent_datas_*` CTest selection | 7/7 passed |
| CPU cost test, 100,000 iterations per worker | Counts and payloads passed, exit 0 |

Memory dumps were empty. Both graphics backends logged the deliberately introduced
compiler error, recovered, and completed the final valid revision. OpenGL still
emitted the previously observed `wglMakeCurrent(00)` shutdown diagnostic.

The CPU run demonstrated measurable instrumentation overhead; it was not an
isolated repeated performance study and overlapped a build. Its wall times are
not a baseline for renderer optimization. Linux and visual/pixel correctness were
not verified in this run.

### Test-only Instrumentation Follow-up

After restoring the original engine files and moving diagnostics into the suite,
the Release targets `04_datas_locking`, `05_datas_cost` and `07_owner_queue_cost`
rebuilt successfully. All eight selected CTests (`concurrent_datas_*` plus
`concurrent_owner_queue_cost`) passed. The full `05_datas_cost --benchmark` also
passed with an empty motor memory dump. Graphics tests were not rerun for this
restoration.
