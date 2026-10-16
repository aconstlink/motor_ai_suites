# Variable Sets and Owner Queues

CPU-only experiments; no graphics window, shader compiler, GPU work or backend
changes. These tests complement [the datas diagnostics](RECOMPILE_AND_LOCKS.md).

```sh
cmake --build build --config Release --target 06_variable_set_cost 07_owner_queue_cost --parallel 2
ctest --test-dir build -C Release -R "concurrent_(variable_set|owner_queue)_cost" --output-on-failure
```

Run from `build/bin/Release` on Windows (usually `build/bin` for single-config builds):

```sh
06_variable_set_cost --benchmark
07_owner_queue_cost --benchmark
```

`--smoke` (also the default) runs a short correctness check, with no performance
thresholds. `--benchmark` runs three measured trials with rotated scenario order
and reports median/min/max wall time. Initialization, warmup, checking and logging
are outside the interval. Both workers are ready before timing starts; gate release
and thread joining are included. `ns/op` is aggregate throughput, not latency of an
individual contended operation. A separate 60-second watchdog catches hangs.

## Variable-set Access

[06_variable_set_cost.cpp](../suite_concurrent/06_variable_set_cost.cpp) uses real
motor variable sets with 8 or 64 `vec4f` variables and initialized values.

It compares:

- Typed name lookup followed by reading the returned variable.
- Reading a previously cached variable pointer.
- The cached-pointer path inside a separate motor `mrsw` reader lock, as a
  primitive-cost control. This is not instrumentation of the set's private lock.

Each case runs with one reader, two readers sharing the same immutable set, and
one writer doing `set/get`. The benchmark performs 100,000 operations per worker;
smoke uses 2,000. Every trial validates its checksum.

Important: `variable_set` locks name lookup and structural mutation. The current
`data_variable<T>::get/set/data_ptr` methods do not lock the value. A borrowed
pointer does not become protected merely because it came from a locked lookup.
This test therefore never concurrently reads and writes the same variable.
The standalone reader-lock control is not a proposed synchronization contract
for writers; its write scenario has exactly one worker.

The name/pointer difference includes **both** linear name search and locking,
not just locking. The control helps interpret it, but subtracting its timing does
not precisely recover the private lock cost. All modes use the same non-inlined
value-read helper to prevent the cached baseline from collapsing to a precomputed
sum; that call overhead remains part of the measurements.

Cached pointers are usable only while their variables remain alive. They must be
refreshed when the owning set is cleared, replaced or otherwise destroys them.
The test keeps each set and its structure alive until all workers have joined.

## Shared Access versus Owner Handoff

[07_owner_queue_cost.cpp](../suite_concurrent/07_owner_queue_cost.cpp) creates 256
entries. One worker generates updates, the other repeatedly reads all entries.
The benchmark executes 1,000 read rounds (256,000 reads), with either 1,000 or
16,000 updates. These are total workload ratios, not producer updates paced to
real frames; the producer can run ahead.

Five topologies perform the same number of reads and apply every update:

| Mode | Data access | Handoff |
| --- | --- | --- |
| `shared_datas` | Both threads use real `datas` guards | Direct mutation |
| `queue_datas_batch1` | Only consumer touches `datas`; guards remain | One message per pop |
| `queue_datas_batch32` | Only consumer touches `datas`; guards remain | Up to 32 messages per pop |
| `queue_owned_batch1` | Only consumer touches a plain fixed array | One message per pop |
| `queue_owned_batch32` | Only consumer touches a plain fixed array | Up to 32 messages per pop |

The queue is bounded to 256 messages and uses a mutex and condition variables.
It is intentionally ordinary, not lock-free. The producer blocks when full.
Commands carry values, not borrowed pointers. Each push locks once; batching only
reduces consumer pop locking. Queue high-water and pop-call counts are reported.

Timing includes generation, enqueue, apply, reads, backpressure and draining any
remaining commands. The test validates FIFO sequence, total application count,
and each entry's final update count and sum of sequence numbers. Intermediate
read checksums can differ because visibility is
delayed by the handoff; only the final state is required to match.

Do not interpret this as a drop-in equivalent scheduling policy: latency and the
point at which a render sees a change differ. Per-message latency is not measured.
The flat-array model also eliminates callback and indirection costs along with
the locks, so its speedup is not a pure measurement of lock removal. Actual shader
compilation, resource creation/destruction, graphics contexts, multiple producers
and shutdown cancellation are outside this model.

For a real backend, removing container locks requires **all** structural writes
and accesses to that container to obey its owner-thread contract, including release,
reconfigure and shutdown. Variable-value snapshots are a separate problem.

## Measured Results

Local Windows/MSVC Release run, 2026-10-13, Intel Core i7-6700 @ 3.40 GHz.
Values below are medians of three trials, in milliseconds, not frame times.
Both CTest smoke cases and both benchmark executables passed, with empty motor
memory dumps. No Linux run or backend changes were made for these experiments.
Raw logs include min/max and queue occupancy:
[variable sets](../suite_concurrent/results/variable_set_cost_2026-10-13.log),
[owner queue](../suite_concurrent/results/owner_queue_cost_2026-10-13.log).

### Variable Sets

100,000 operations per worker; two readers perform 200,000 operations in total.

| Variables | Workers / operation | Name lookup | Cached pointer | MRSW + cached control |
| --- | --- | ---: | ---: | ---: |
| 8 | 1 / read | 16.858 | 1.085 | 8.736 |
| 8 | 2 / read | 48.976 | 1.107 | 23.995 |
| 8 | 1 / set/get | 16.565 | 1.356 | 8.057 |
| 64 | 1 / read | 50.468 | 1.061 | 8.739 |
| 64 | 2 / read | 89.128 | 1.780 | 22.536 |
| 64 | 1 / set/get | 59.084 | 1.359 | 11.552 |

Name lookup gets more expensive with more variables, while cached access remains
similar. Even the standalone MRSW control is appreciably more expensive than the
tiny value read. This supports resolving variables once per layout/rebind rather
than searching by name for every update. It does not imply that current backend
value accesses all lock: paths already using cached pointers have already avoided
this lookup overhead. Value synchronization and pointer lifetime remain separate.

### Owner Queue

256,000 reads in every case. The second column varies total producer updates.

| Mode | 1,000 updates | 16,000 updates |
| --- | ---: | ---: |
| Shared datas | 56.135 | 70.060 |
| Queue + datas, batch 1 | 64.125 | 86.488 |
| Queue + datas, batch 32 | 59.002 | 72.389 |
| Queue + owner-only array, batch 1 | 0.717 | 2.286 |
| Queue + owner-only array, batch 32 | 0.596 | 1.303 |

Messaging alone did not improve this workload while per-access datas guards
remained. Owner-only storage removed most of the overhead in this deliberately
small-work-per-access experiment. Consumer batching helped particularly at the
higher update count. The magnitude must not be extrapolated to engine frame rates:
real rendering has substantial additional work, and the array baseline also
removes datas wrappers and indirection. Three trials provide a local indication,
not a hardware-independent performance guarantee.

The useful next step is a narrow owner-thread experiment for one backend resource
type: prepare work on the support thread, publish commands/results, and apply
container changes on the render thread at defined boundaries. Only then can its
data-access locks be removed safely. This need not start with a full asynchronous
frame redesign, but every access to the chosen container must follow the ownership
rule, including cleanup. Benchmark the real render path before broadening it.
