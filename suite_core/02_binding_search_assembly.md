# uint64 Search: Actual MSVC Release Assembly

## Build and Evidence

Inspected the actual `02_binding_search.dir/Release/02_binding_search.obj` with
MSVC `dumpbin /DISASM`, not a separately compiled reduction. The complete function
byte sequences were also found in the linked `02_binding_search.exe`.
The [disassembly extract](results/2026-10-13-uint64-release.asm.txt) includes sizes,
EXE file offsets and the executable SHA256. Addresses in function listings are
object-relative, while the recorded file offsets identify the linked bytes.

- Windows x64 Release; MSVC compiler file version 19.51.36260.0, toolset directory 14.51.36231.
- Recorded compiler command: `/O2 /Ob2 /MD /std:c++20 /DNDEBUG`, no `/GL` or custom `/arch`.
- Original two functions were inspected before adding variants and retained the
  same instruction sequences after rebuilding with all seven variants.
- Search functions remain `noinline`; find_if and its predicate are inlined inside
  the find_if wrapper. Neither wrapper calls a lambda or any other function.
- No SIMD and no automatic loop unrolling in either original uint64 search.

## Original Loops

`find_if` (48 bytes for the entire function) walks a pointer and loads the key once:

```asm
mov rcx, qword ptr [rdx]       ; key, outside the loop
loop:
cmp qword ptr [rax], rcx      ; current element
je  found
add rax, 8                   ; next uint64
cmp rax, r9                  ; end pointer
jne loop
```

`while` (52 bytes) walks an index. MSVC leaves the key load inside the loop:

```asm
loop:
inc rax                      ; index, initially -1
cmp rax, r9                  ; element count
jae end
mov rcx, qword ptr [rdx]      ; key loaded again each iteration
cmp qword ptr [r10+rax*8], rcx
jne loop
```

Labels and comments above are explanatory; instructions are taken from the extract.
The hot miss path has five instructions in find_if versus six in while, including
the extra key load. Both have two conditional branches per element. Find_if converts
its pointer to an index once on success; while computes the vector element count
before searching. The indexed addressing mode is a single instruction, not a
separate multiply in the loop.

Thus find_if is actually smaller in this build. A different Godbolt compiler,
standard library, options or source formulation need not produce these instructions.
This comparison does not identify the exact CPU bottleneck: no hardware counters
were collected, and alignment/branch layout can affect timings too.

## Added Variants

| Strategy | Entire function size | Observed code |
| --- | ---: | --- |
| find_if | 48 bytes | Pointer loop; key loaded once |
| while | 52 bytes | Index loop; key reloaded per iteration |
| while_cached | 52 bytes | Index loop; key load moved outside loop |
| pointer | 67 bytes | Same five-instruction hot loop shape as find_if, different setup/exit |
| unroll2 | 94 bytes | Two scalar comparisons per full block |
| unroll3 | 118 bytes | Three scalar comparisons per full block |
| unroll4 | 128 bytes | Four scalar comparisons per full block |

The unrolled variants use a copied key, check the first match in order and have
an ordinary scalar tail. Their full-block loop checks the remaining count once
per block, but still branches on each comparison. There is no SIMD in these
variants either. Larger code is intentional, not an optimization failure.

## Measurements (2026-10-13)

Three sequential Release runs with `--uint64-only`; one warmup plus seven rounds
per strategy, with rotating order. Each number below is the median of the three
process medians in ns per search. Same 256-element vector and query sets for every
strategy; mixed requests contain 75% hits and 25% misses. No affinity or frequency
isolation was applied. Setup, allocation and logging are outside the timed section.
All three runs and the correctness-only CTest passed.

| Strategy | First hit | Miss | Mixed |
| --- | ---: | ---: | ---: |
| find_if | 10.39 | 97.08 | 78.35 |
| while | 9.58 | 165.67 | 93.32 |
| while_cached | 9.17 | 182.94 | 103.05 |
| pointer | 9.99 | 96.76 | 76.89 |
| unroll2 | 9.30 | 89.46 | 64.00 |
| unroll3 | 10.29 | 119.77 | 76.33 |
| unroll4 | 9.33 | 72.63 | 49.81 |

The local key copy alone did not recover the gap: attributing the whole slowdown
to the repeated load would be incorrect. The pointer variant behaved similarly
to find_if; four-way unrolling helped long searches here, despite larger code.
Three-way unrolling was not monotonically better than two-way. Early-hit differences
are small and include common call/query/checksum overhead. These observations do
not establish universal rankings or predict inlined backend performance.

[All per-run medians and round min/max](results/2026-10-13-uint64-unrolled.csv).
