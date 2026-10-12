# Concurrent Tests

The existing `00_threads_parallel_for` covers threads, pooled tasks, ranges and
nested parallel work. See [build instructions](../BUILDING.md#concurrent-console-tests).
The following tests add synchronization and dependency-graph coverage without graphics.

Result lines use `motor::log::global_t::test`: green `[OK]` at status level or red
`[FAIL]` at error level in the console, plain text in other loggers. This requires
a Motor revision containing that API. Exit codes and memory checks remain the
authority for CTest; colors are only presentation.

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

## Task Storage Reuse

`03_task_storage <schedule|disconnect>` guards against reserving
`capacity() + count` instead of `size() + count` in task scratch buffers.

- `schedule`: collect eight successors 10,000 times using the scheduler accessor,
  releasing collected references and clearing the scratch vector after each round.
- `disconnect`: connect and drain a nine-task graph 256 times through the cleaner
  accessor, reusing its scratch vector and releasing all transferred references.

Both cases retain a prefix entry while appending, verify pointer order and count,
and require capacity to stay unchanged after the first complete round. They do not
assume a particular vector growth factor. Cleanup must leave an empty memory dump.
The tests deliberately use no worker threads: they isolate storage reuse from
scheduler timing, deadlocks and backend behavior. A clean shutdown dump alone does
not detect capacity retained and repeatedly increased during runtime.

Run only these regressions with `ctest --test-dir build -C Release -L task_storage --output-on-failure --no-tests=error`.

## Backend Data Locking (No Rendering)

`04_datas_locking [--smoke|lifecycle|recursive|try_access|independent|contention|wakeup]`
tests the actual `motor::platform::datas<T>` header with dummy data. It links only
Concurrent, Memory and Log, with no window, graphics backend or GPU context.

| Scenario | Checks |
| --- | --- |
| `lifecycle` | Creation, lookup, missing IDs, growth to 81 entries, stable payload addresses, invalidation, slot reuse and cleanup |
| `recursive` | Nested blocking and try access to the same entry, preserving outer ownership after inner guards leave, acquisition by another thread afterwards |
| `try_access` | Missing entry, rejection while another thread owns the entry, successful acquisition after release |
| `independent` | Access to a second entry while the first entry remains locked |
| `contention` | Six threads perform 12,000 accesses; callbacks must remain exclusive and preserve values |
| `wakeup` | 32 rounds of four contenders, checking exclusion, wakeup and reacquisition |

No argument runs all scenarios. CTest runs each scenario in its own process under
the `datas` and `concurrent` labels, so existing console CI includes these tests.
Each scenario has a 20-second watchdog; CTest additionally enforces 30 seconds.
On a watchdog failure the process exits without normal cleanup, because joining
deadlocked workers would hang. Normal exits require no live dummy objects and an
empty Motor memory dump.

The test harness uses standard-library synchronization. Contended dummy values
are atomic, so broken mutual exclusion does not itself create payload data races.
These tests cannot prove that the guard's internal metadata is race-free; a
ThreadSanitizer run is useful in addition to repeated functional runs. Recursive
access while a container writer is pending is not covered here.

```sh
cmake --build build --config Release --target 04_datas_locking --parallel 2
ctest --test-dir build -C Release -L datas --output-on-failure --no-tests=error
ctest --test-dir build -C Release -L datas --repeat until-fail:10 --output-on-failure
```

### Initial Datas Results (2026-10-12)

With Motor `3d240b4`, Windows MSVC Debug and Release both passed five of the six
scenarios. `contention` hit the 20-second watchdog in both configurations and in
a second Release run. The five completed scenarios had empty memory dumps.
Do not interpret the passing recursive test as proof of thread safety.

Code inspection still shows owner fast paths in `guard_data::enter()` and
`try_enter()`, plus the updates in `leave()`, accessing `tid`/`entries` outside
`mtx`. These accesses race with other threads and the wait predicate can change
without the condition-variable mutex. This is a concrete suspect for the stall;
the timeout alone does not identify the exact interleaving.

The test and Motor Memory/Log sources also compiled with GCC ThreadSanitizer in
WSL. That executable could not run: the sanitizer aborted at startup with
`FATAL: ThreadSanitizer: unexpected memory mapping`. No sanitizer diagnosis was
obtained. The engine implementation was not changed as part of these tests.

## Running

```sh
cmake --build build --config Release --parallel 4 --target 01_sync_primitives 02_task_graph 03_task_storage
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
