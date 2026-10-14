# GL4 Owner-Thread Lock Experiment

## Scope

Local experiment in the Motor submodule of `motor_ai_suites`, based on Motor
`0933369`. The committed source was fetched from the local `motor_suites/motor`
checkout without modifying that checkout. The experiment is not committed and
is not intended as an engine change to adopt unchanged.

Only GL4 opts into `datas<T, false>`. The template defaults to `Locking = true`,
so D3D11, other backends, and the concurrent container tests retain their locks.
D3D11 still calls its construction path from its support thread and must not
use the unlocked specialization yet.

The experimental specialization bypasses both the container reader/writer locks
and the per-entry guards. It preserves the container algorithms, storage, lookup,
callbacks, and entry mutex members; this is not a container rewrite. Support-thread
queue locks, graphics variable-set locks, memory-manager locks, and all other
synchronization are unchanged. No compile-status or shutdown bug was fixed to
make the tests pass.

The active GL4 worker now performs MSL parsing, dependency resolution, GLSL
generation, compilation, linking, and reflection, and returns the result through
the queue. The backend containers are updated by the result consumer in
`end_frame()`. This makes a GL4-only lock experiment possible. It is not proof
that every shared object in the renderer is race-free.

## Test Commands

```sh
13_live_recompile --smoke --gl-only --valid-only
13_live_recompile --smoke --gl-only
13_live_recompile --gl-only --close-on-submit
01_window_lifecycle --smoke --gl-only
ctest --test-dir build -C Release -R "^concurrent_datas_" --output-on-failure
```

`--valid-only` exercises repeated A/B uniform-layout changes without the deliberately
invalid shader. The original negative smoke test remains unchanged by default.
`--close-on-submit` requests application shutdown just after the first tested
shader configuration is submitted. It does not guarantee that the compiler is
still executing when shutdown reaches the render thread.

## Correctness Results

| Check | Original locks | GL4 `datas` locks disabled |
| --- | --- | --- |
| Valid A/B recompilation, nine revisions | Pass, exit 0, empty memory dump | Pass, exit 0, empty memory dump |
| Initial invalid shader and recovery | Fail: operation timeout, exit 1 | Same failure |
| Close after configure submission | Fail: 23,480 bytes retained, exit 1 | Same retained-byte count |
| GL4 window close/reopen lifecycle | Pass, exit 0, empty memory dump | Pass, exit 0, empty memory dump |
| Default locked `concurrent_datas_*` tests | Not rerun before patch | 7/7 pass |
| Valid shared-object recompile, GL4 + D3D11 | Not rerun before patch | Pass, exit 0, empty memory dump; D3D11 remains locked |

The positive graphics tests check resource state and command progress, not pixel
correctness. No screenshot comparison, GPU readback, sanitizer, or Linux run was
performed. The known `wglMakeCurrent(00)` shutdown diagnostic remains visible.

### Existing Failure Paths

1. `construct_msl_data(compilation_result&&)` returns after releasing the incoming
   object on compilation failure, but does not reset the in-transit state or
   publish the failure. The initial deliberately invalid shader therefore never
   settles. This happens with the original locks as well as the experiment.
2. Shutdown can destroy an unconsumed compilation result without releasing its
   raw, refcount-owning `object` pointer. The early-close logs retain an
   `msl_object` at refcount 1 and its associated variable sets. Queue cancellation
   must explicitly release outstanding references; successful consumption in
   `end_frame()` is not the only lifetime path. Outstanding forward work also
   needs a defined cleanup path.

The support-thread `running` and `has_work` flags still have unsynchronized
accesses. Passing these runs does not validate those accesses or GL cross-context
synchronization. The two failing tests should be resolved before treating the
new lifecycle as production-ready.

## Measurement Method

Windows, MSVC Release, Intel Core i7-6700, NVIDIA GeForce RTX 2060,
driver `32.0.16.1088`. Requested window size is 1280 x 720; VSync is disabled.
Each process runs 120 warmup frames and records 300 frames. No build or other
agent-started test runs concurrently with a graphics measurement. Desktop/system
load, CPU affinity, frequency, and GPU clocks were not controlled.

Workloads:

- Direct: 1,000 objects, no lights, 1,000 expected draws and variable sets.
- Scene: 1,000 objects, two multipass lights, 2,000 expected draws and 3,000 sets.
- Direct: 5,000 objects, no lights, 5,000 expected draws and variable sets.

Three separate runs per workload were first executed for each build. Because
the observed frame cadence varied considerably, two more pairs per workload
alternate the saved platform DLLs: locked/unlocked, then unlocked/locked.
Executables and other DLLs are unchanged within the paired comparison. The
unlocked platform DLL is restored afterward to match the local source.

These are CPU-side frame-cadence measurements, not GPU timestamps or isolated
backend lock timings. Do not interpret an individual percentage as a guaranteed
renderer speedup.

### Frame Cadence Results

Median of five process medians, milliseconds per frame. Parentheses show the
minimum and maximum process median, not individual-frame extremes.

| Workload | Locked | GL4 unlocked |
| --- | --- | --- |
| Direct, 1,000 objects | 7.790 (5.210-8.609) | 5.544 (4.473-7.489) |
| Scene, 1,000 objects, two lights | 19.085 (12.632-20.537) | 11.514 (9.013-16.215) |
| Direct, 5,000 objects | 20.626 (19.143-21.280) | 23.294 (16.492-27.353) |

All 30 benchmark processes exited successfully with empty Motor memory dumps.
The aggregate medians favor the unlocked build in the 1,000-object workloads,
but they are not a stable speedup estimate. In particular, paired comparisons
change direction:

| Workload / pair | Locked median | Unlocked median |
| --- | --- | --- |
| Scene, pair 1 | 19.085 | 9.684 |
| Scene, pair 2, reversed order | 12.632 | 15.903 |
| Direct 5,000, pair 1 | 21.280 | 16.492 |
| Direct 5,000, pair 2, reversed order | 19.143 | 27.353 |

Conclusion: the GL4-only lock bypass survived these positive functional tests,
but this frame-cadence study does not establish a dependable general FPS gain
or slowdown. Isolated backend CPU timing and GPU timing are needed to attribute
the remaining costs. No engine-wide lock removal is justified by these runs.

## Reproduction Artifacts

Recorded runs and the exact local engine diff are in
[`results/gl4-owner-thread-20261014`](results/gl4-owner-thread-20261014/).
The patch applies to Motor `0933369`; it changes only `datas.hpp` and the nine
GL4 container aliases. It intentionally leaves D3D11 locked.

Platform DLL SHA-256:

- Locked: `1E8451916EAB6CA2231E9B04780485CC372E0A663F98F69F5CD5F90AF9708863`
- Unlocked: `8A748E5797010395B81E4D851E0955B27E8AD0FCB35894D632F9CB97F2F933DD`

Example benchmark invocation:

```sh
05_scene_benchmark --backend gl --path scene --objects 1000 --lights 2 --warmup 120 --frames 300
```
