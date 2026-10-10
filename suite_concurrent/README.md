# Concurrent Tests

The existing `00_threads_parallel_for` covers threads, pooled tasks, ranges and
nested parallel work. See [build instructions](../BUILDING.md#concurrent-console-tests).
The following tests add synchronization and dependency-graph coverage without graphics.

## Synchronization

`01_sync_primitives <scenario>`:

| Scenario | Checks |
| --- | --- |
| `semaphore` | Countdown completion, visibility of worker results, zero-boundary decrement and reuse |
| `signal` | Pre-signaled wait, reset/reuse, waking multiple waiters and publishing data |
| `readers` | Multiple readers can hold an MRSW lock simultaneously; a writer can acquire it afterwards |
| `contention` | Two writers and four readers contend; writers remain exclusive and readers see consistent data |

## Task Graphs

`02_task_graph <pool|loose> <chain|diamond|fanout>` constructs a root, one or more
branches, a join and a terminal task. Checks require every task to execute exactly
once, after its dependencies. Fan-out uses twelve branches.

Workers are stopped before graph references are disconnected and released.
CTest runs graph scenarios serially and imposes a 30-second timeout, so a deadlock
is reported as a failure instead of blocking CI indefinitely. These are correctness
tests, not scheduler benchmarks.

## Running

```sh
cmake --build build --config Release --parallel 4 --target 01_sync_primitives 02_task_graph
ctest --test-dir build -C Release -L concurrent_extended --output-on-failure --no-tests=error
```

Use `-L concurrent` to include the existing parallel-for tests. Each scenario runs
in a separate process. Successful completion also requires an empty Motor memory
dump after cleanup. Repeated runs increase coverage but do not prove freedom from
all data races or scheduling bugs.

## Initial Results

Against Motor `8201afae`, all ten new scenarios pass in the final Windows
Debug/Release and Linux Debug (WSL) runs. Each scenario also passed five consecutive
Windows Release runs.

An earlier Windows Release run timed out in `concurrent_graph_pool_diamond`.
It has not reproduced after adding pool lifecycle diagnostics. Treat this as an
unresolved intermittent finding, not proof that the scheduler is race-free.
The new messages distinguish graph completion from pool shutdown if it recurs.
