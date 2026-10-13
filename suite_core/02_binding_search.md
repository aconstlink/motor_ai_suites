# Binding Search: while vs. std::find_if

Standalone console benchmark inspired by the two search loops in GL4's
`shader_data`: an index-based `while` and iterator-based `std::find_if`.
The current version searches plain `std::vector<std::string>`,
`std::vector<std::uint32_t>` and `std::vector<std::uint64_t>`. It uses no Motor
objects, allocators, containers, graphics types or locks in the measured work.
Only diagnostic output uses `motor::log`, outside timing. No engine changes.

```powershell
cmake --build build --config Release --target 02_binding_search --parallel 2
./build/bin/Release/02_binding_search.exe
./build/bin/Release/02_binding_search.exe --verify-only
./build/bin/Release/02_binding_search.exe --uint64-only
ctest --test-dir build -C Release -R '^core_binding_search$' --output-on-failure
```

The default run measures 0/4/16/64/256 entries, first/middle/last hits, misses,
and mixed requests (75% hits, 25% misses). Empty containers only use the miss case.
Names are 9 or 64 characters long with common prefixes. Misses have the same
length as hits. Numeric keys are unique integers; the uint64 case includes bits
above bit 32. Both searches return the first matching index. The long-name case
stresses string comparison, not allocation.
These are synthetic distributions, not captured application workloads.

The uint64 test also includes `while_cached` (a local copy of the key), `pointer`
(a pointer loop), and `unroll2`/`unroll3`/`unroll4` (2/3/4 scalar comparisons per
full block, followed by a bounded tail). The string and uint32 cases still compare
the two original strategies. See the [actual Release assembly analysis](02_binding_search_assembly.md).

All data and queries are prepared before timing. The benchmark uses one warmup
per implementation and seven measured rounds, rotating execution order.
Each round performs at least 50,000 searches (up to 200,000 for smaller vectors).
Output is nanoseconds per lookup (median and min/max across rounds).
Current `SEARCH_VARIANT` records report each strategy separately; lower is faster.
Earlier CSV files retain their original ratio columns and are not mixed with new runs.
This is a warm-cache CPU microbenchmark, not a frame-rate prediction.

Both lookup functions and the measurement wrapper are marked noinline to keep
the repeated searches observable across the call boundary. Each timed run checks
an accumulated result against the prepared query expectations; a volatile sink
consumes the result outside timing. Timings include identical function-call,
query-selection and checksum overhead. The noinline boundary means these results
do not establish what an inlined backend call site would cost.

Correctness checks validate hit/miss results and the returned index, including
an unchanged output argument on failure and first-match behavior for duplicates.
Additional uint64 checks cover sizes 0..33, every hit position, every tail length
for all unroll factors, zero/max values and keys referencing vector elements.
CTest runs correctness only, without
performance thresholds. Use Release without a debugger, avoid concurrent builds,
and repeat before interpreting small differences. Logging uses `motor::log`.

## Standard-Only Result (2026-10-13)

Windows/MSVC Release, three sequential process runs; all checks passed, including
the separate CTest. Motor's memory dump is not used to validate standard allocations.
Mixed requests below contain 75% hits and 25% misses. Values are ns per lookup,
the median of three process medians, not pooled frame or lookup samples.

| Type | Entries | find_if | while |
| --- | ---: | ---: | ---: |
| string, 9 characters | 4 | 37.40 | 32.53 |
| string, 9 characters | 16 | 97.71 | 90.45 |
| string, 9 characters | 64 | 246.25 | 267.56 |
| string, 9 characters | 256 | 784.90 | 874.85 |
| string, 64 characters | 4 | 42.20 | 40.59 |
| string, 64 characters | 16 | 104.21 | 91.34 |
| string, 64 characters | 64 | 312.03 | 308.02 |
| string, 64 characters | 256 | 1156.03 | 1116.38 |
| uint32 | 4 | 9.30 | 10.78 |
| uint32 | 16 | 18.04 | 19.19 |
| uint32 | 64 | 35.58 | 32.94 |
| uint32 | 256 | 75.26 | 83.97 |
| uint64 | 4 | 10.17 | 10.70 |
| uint64 | 16 | 17.51 | 18.29 |
| uint64 | 64 | 36.96 | 39.76 |
| uint64 | 256 | 67.60 | 110.34 |

Neither loop wins every case. In the 256-entry uint64 mixed case all three process
medians favored find_if (65.71-78.94 ns vs. 99.39-121.64 ns). Smaller differences
overlap the measured run-to-run variation. Strings show a different pattern from
the earlier struct-based test; this does not isolate Motor allocator overhead.
No assembly-based explanation or inline-call performance is claimed.

[Standard-only per-run results](results/2026-10-13-std-search.csv).

## Historical Motor-Container Result (2026-10-13)

The measurements below belong to the previous implementation with Motor containers
and `{vertex_attribute, motor::string_t}` records. They are retained as history,
not current results. The standard-only version changes element layout, output
handling and iteration counts as well, so differences cannot be attributed solely
to changing the container allocator.

Windows/MSVC Release, three sequential process runs, all correctness checks passed
and all Motor memory dumps empty. The corresponding CTest passed as well.
The table uses the median of the three per-process medians, in ns per lookup,
for mixed queries (75% hits). These are not pooled sample medians.

| Entries | Name length | find_if | while |
| ---: | ---: | ---: | ---: |
| 4 | 9 | 36.894 | 47.647 |
| 16 | 9 | 104.895 | 105.528 |
| 64 | 9 | 261.444 | 243.220 |
| 256 | 9 | 793.040 | 973.456 |
| 4 | 64 | 42.268 | 45.877 |
| 16 | 64 | 90.805 | 120.249 |
| 64 | 64 | 318.636 | 406.452 |
| 256 | 64 | 1119.360 | 1564.256 |

There was substantial variation; for example, the 64-entry short-name mixed case
changed winner across runs. No CPU affinity or frequency isolation was applied.
The measurements do not establish a general while-loop advantage. Longer names
generally favored find_if here, but neither spelling has a portable speed guarantee.
Do not rewrite the backend based on these numbers alone, particularly since its
lookup functions may be inlined. Both searches are linear and compare the same names.

[All per-run results, including round min/max](results/2026-10-13-binding-search.csv).
