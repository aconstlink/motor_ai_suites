# Logger Regression Tests

Two small console applications exercise the existing logging implementation.
motor is not modified. Failures are real regressions, not marked `WILL_FAIL` or
converted into successful test results.

## Build and Run

```sh
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build --config Release --target 00_log_store 01_log_system --parallel 2
ctest --test-dir build -C Release -L '^log$' --output-on-failure
```

For a quick run that excludes the two potentially crashing copy cases:

```sh
ctest --test-dir build -C Release -L '^log$' -LE log_unsafe --output-on-failure
build/bin/Release/00_log_store.exe --smoke
build/bin/Release/01_log_system.exe --smoke
```

Use `build/bin` without `Release` for single-configuration generators. Each
application accepts an individual scenario name instead of `--smoke`.
Each registered CTest scenario runs in its own process with a 20-second timeout.
The existing CI workflows' explicit build targets/label filters are unchanged;
these tests are available locally through CTest, not added to those CI selections.

## Store Scenarios

| Scenario | Contract checked |
| --- | --- |
| `empty` | Empty count, positive capacity, no callbacks for an empty range, empty copy. |
| `order` | Six inserted messages and levels are returned in insertion order. |
| `wrap` | After two full wraps plus seven entries, exactly the newest `get_max_items()` messages remain, oldest first. |
| `bounded` | Retained motor-tracked memory stops growing after warmup for fixed-size repeated messages. |
| `copy_zero` | Copy `[0,3)` returns the first three messages. |
| `copy_range` | Copy `[2,5)` returns three messages starting at destination index zero. |
| `copy_wrap` | Copy `[0,3)` after wrapping returns the oldest three retained messages. |
| `move` | Store contents survive move construction and move assignment, then accept another write. |
| `writers` | Four threads write 64 unique messages each; after joining, every message appears exactly once. |

The intended ring contract uses all `get_max_items()` slots and half-open ranges.
The tests do not inspect private fields or substitute a test implementation.

`bounded` runs without background threads, directly against a local store. It
creates a fixed payload, writes twice the advertised capacity, samples
`memory::global_t::get_sib()`, writes another six capacities and samples again.
There is no global logging between those measurements. It measures retained
tracked bytes, not OS RSS, allocation-call count or peak memory. The final memory
dump separately checks that test-owned resources have been released.

`copy_range` and `copy_wrap` are labelled `log_unsafe` and omitted from the
in-process `--smoke` run. With the current implementation they can corrupt memory,
crash or hang during process teardown. Run them through CTest for process isolation
and timeout handling, not as part of a long-lived application.

## System Scenarios

| Scenario | Contract checked |
| --- | --- |
| `fanout` | Reject null sinks, deliver to two sinks and avoid duplicate registration. |
| `test_result` | `test()` forwards status/error with `[OK]`/`[FAIL]` to a custom sink. |
| `default` | Selecting a default logger routes subsequent output there; resetting detaches it and returns the former pointer. |
| `move` | A moved system writes into its own store, not the moved-from system's store. |

`default` expresses the intended meaning of `set_default_logger`, not merely its
current pointer bookkeeping. The caller owns the recording sinks and keeps them
alive longer than the system. `move` keeps the source alive while checking counts,
so it detects misrouting without intentionally dereferencing a destroyed source.
The intentionally emitted `[FAIL] fail-marker` in `test_result` is test input;
the scenario passes when that input is forwarded correctly.

`system` and `std_cerr_logger` are non-exported internal classes in Windows shared
builds. The system test therefore compiles their **unchanged original source
files** into its executable. The exported store, global logger and memory manager
still come from motor. This is a white-box test target, not an engine modification
or a copied replacement implementation.

## Baseline: 2026-10-16

Windows / MSVC Release, motor `33c6422`:

| Result | Scenarios |
| --- | --- |
| Passed | `empty`, `fanout`, `test_result` |
| Failed | `order`, `wrap`, `bounded`, `copy_zero`, store `move`, `writers`, `default`, system `move` |
| Timed out | `copy_range` (20 seconds; reported failure before process teardown stopped completing) |
| Access violation | `copy_wrap` (CTest: SegFault) |

**3/13 passed.** These are not ten independent defects: several store tests fail
because of the same append-versus-ring-buffer problem. In particular, `writers`
failing does **not** demonstrate a threading bug; incorrect storage/readback alone
already explains its result.

Observed `wrap` count: **1**, advertised capacity: **1,000**.
Observed retained bytes in `bounded`: **778,320 -> 2,794,992**.
Normally completed scenarios ended with an empty motor memory dump. That does not
contradict growing storage during use: destruction can still free that storage.

## Limits and Follow-Ups

- No proof of race freedom. Concurrent readers, first global initialization and
  logger registration require a separately agreed lifetime/concurrency contract
  and preferably ThreadSanitizer coverage.
- No intentional race against `global::deinit()`; producers must be stopped first.
- No recursive logging from `for_each` callbacks: callbacks currently execute
  under the store mutex.
- No throughput benchmark, formatting/console-color checks, or Linux validation
  in this baseline.
- Future corrections belong in motor; rerun these unchanged tests afterward.
