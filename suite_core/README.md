# Core Tests

- [Binding search: while vs. find_if](02_binding_search.md): isolated string lookup benchmark, without a graphics backend.
- [Component lookup: RTTI, type IDs and single-pass dispatch](#component-lookup): 1-5 components per node, including misses.
- [Command storage](#command-storage): command creation and clearing versus reusable indexed slots.
- Document tokenization and OBJ measurements are documented below.

## Command storage

`04_command_storage` uses the actual `motor::graphics::render_engine::command_t`
to compare three local queue implementations. No engine changes, backend calls,
windows or GPU work are involved. This is a single-threaded CPU microbenchmark,
not a measurement of the complete command engine or scene frame.

| Strategy | Recording | Reset |
| --- | --- | --- |
| `clear` | Construct commands and emplace into a reserved vector | `clear()`, retaining vector capacity |
| `index_keep` | Construct commands and move-assign into pre-existing slots | Set the active count to zero, retaining captures |
| `index_release` | Same pre-existing slots | Assign empty commands to active slots, then reset the count |

All strategies create fresh lambdas each frame. `index_keep` **does not eliminate
capture destruction**: replacing a slot destroys its previous callable, and that
cost is included in recording. It changes the timing and locality of that work.
Unused tail slots retain their captures until overwritten or finally cleared.
The final cleanup is timed separately and printed by the executable. This is
therefore not a drop-in replacement when captures must be released every frame.
No nontrivial object is treated as POD and no destructor is skipped.

### Workload and validation

- 1,000 and 10,000 commands, with small pointer/value captures, render-like
  captures (two pointers plus the actual `render_detail`), and a large capture
  containing an array of 32 `uint64_t` values plus a pointer.
- The cancel callable remains the command type's default empty callback.
  Production commands with substantial cancel captures may behave differently.
- Queue allocation/reservation happens before timing. Recording includes callable
  construction and move construction/assignment. Execution runs a checksum, not
  a real draw. Reset and execution are measured separately.
- Three warmup frames, then 15 rounds of eight frames, rotating strategy order.
  Every timed frame's checksum is checked outside the measured interval. Logging
  and CSV output are outside each timed batch.
- CTest verifies changing active counts (8, 2, 0, 5, 1), changed values, no stale
  execution after reset, and owned-capture destruction/retention for each strategy.
- `std::function` allocation counts and allocator internals are not instrumented.
  The motor memory dump does not account for all standard-library allocations.

### Local results, 2026-10-23

Windows x64, Intel Core i7-6700, MSVC 19.51, Release, motor `100ba18`.
Three independent process runs. Each entry is the median of three run medians;
each run median contains 15 eight-frame averages. Values are milliseconds per
synthetic frame, not GPU timings or individual-frame tail percentiles.
On this build, `sizeof(command_t)` is 128 bytes, `sizeof(std::function)` is 64,
and `sizeof(render_detail)` is 56.

**10,000 render-like commands:**

| Strategy | Record | Reset | Record + reset | Including checksum execution |
| --- | ---: | ---: | ---: | ---: |
| `clear` | 1.209 | 0.490 | 1.702 | 1.843 |
| `index_keep` | 0.823 | <0.001 | 0.823 | 0.952 |
| `index_release` | 1.178 | 0.568 | 1.735 | 1.893 |

Columns are aggregated independently; their rounded medians need not add up.
The combined record/reset cost is about **52% lower** for `index_keep` in this
case. Resetting every callable immediately (`index_release`) does not provide
the same benefit. Final retained-capture cleanup for `index_keep` took about
0.35-0.39 ms for this case, once at the end of each process workload.

**Record + reset across capture sizes:**

| Commands | Capture | `clear` | `index_keep` | `index_release` |
| ---: | --- | ---: | ---: | ---: |
| 1,000 | Render-like | 0.08 | 0.07 | 0.08 |
| 10,000 | Small | 0.383 | 0.295 | 0.423 |
| 10,000 | Render-like | 1.702 | 0.823 | 1.735 |
| 10,000 | Large | 2.506 | 1.020 | 2.527 |

The result depends strongly on capture size and queue size. Cache/allocator
behavior is a possible contributor, but this test does not isolate the cause.
10,000 commands numerically matches two commands for each of 5,000 draws;
it does **not** reproduce their exact capture mix, concurrency or full frame.
This supports testing indexed reuse in the real recording path, not claiming
a 52% application speedup. Three runs and rotated order reduce but do not
eliminate background-load and allocator-history effects.

[Raw measurements](results/command-storage-2026-10-23/) contain all timed samples.
Release build and `core_command_storage` passed locally; Linux was not tested.

```sh
cmake --build build --config Release --target 04_command_storage --parallel 2
ctest --test-dir build -C Release -R "^core_command_storage$" --output-on-failure
./build/bin/Release/04_command_storage.exe --csv command-storage.csv
```

## Component lookup

`03_component_search` compares five ways to obtain the same five requested
component types from small nodes. This is an isolated CPU microbenchmark using
local polymorphic fixtures, not a modified motor scene graph. No engine changes,
graphics backend, window, shader compilation or scene traversal are involved.

| Strategy | Work per node |
| --- | --- |
| `linear_rtti` | Five independent pointer-list scans using `dynamic_cast` |
| `linear_id` | Five independent linear scans comparing 16-bit type IDs |
| `binary_id` | Five `lower_bound` searches in the same sorted ID/pointer list |
| `single_pass_rtti` | One pointer-list traversal, with an `if/else dynamic_cast` chain |
| `single_pass_id` | One sorted-list traversal, with a type-ID `switch` |

Every variant returns five result pointers in the same order. Identical checksum
work consumes those results and their payloads, including misses. Dispatch does
not execute application logic in component order: real visitors with ordering
dependencies must preserve their required order too.

### Workload and correctness

- Nodes contain 1, 2, 3, 4 or 5 unique components. Five types are queried per node.
- Ten concrete fixture types directly derive from a polymorphic base. Wanted
  types have even IDs, unrequested types odd IDs; misses are not all outside the
  sorted range. These are local fixed IDs, not an engine type-ID registry.
- `wanted_only`: every stored component is requested. The query hit rate is
  count/5, so 100% only at five components.
- `mixed`: approximately half the stored components are requested. Odd counts
  alternate floor/ceil across nodes. At five components this is 50% query hits.
- `all_miss`: none of the stored types is requested; all five searches fail.
- 4096 nodes use deterministically shuffled insertion order, type subsets and
  visitation order. Nodes are reused for 16 sweeps per timing sample.
- One warmup round and seven measured rounds rotate the five strategies.
  Allocation, sorting, fixture construction, validation and logging are outside
  the timed region. Containers use motor wrappers; fixture ownership uses local
  `unique_ptr`s. No ownership operation occurs in the search kernels.
- Search logic can inline into each batch; batch functions are not inlined.
  Volatile function-pointer calls prevent repeated sweeps being folded away.
  Every measured checksum is checked against an independently constructed result.
- CTest checks exact result pointers for all 638 subsets of ten types containing
  up to five components, including empty nodes, with four insertion orders each.
  The benchmark also validates every generated node before measurement.

The fixtures deliberately use distinct types without duplicate components or
derived-type queries. Exact type-ID matching does **not** replace `dynamic_cast`
semantics for base-class queries. The ID variants store the ID next to the
pointer and do not access the object on a miss; a virtual type getter or a type
ID stored inside the object could have different costs. This measures complete
lookup designs, not an isolated RTTI instruction cost.

### Local results, 2026-10-23

Windows x64, Intel Core i7-6700, MSVC 14.51, Release `/O2`. Three independent
processes, each with the seven measured rounds above. Values are medians of the
three per-process medians, **nanoseconds per node for all five queries plus the
common checksum**, not time per individual lookup.

| Five components per node | All queries hit | 50% query hits | All queries miss |
| --- | ---: | ---: | ---: |
| Linear RTTI | 445.7 | 659.5 | 805.3 |
| Linear type ID | 19.4 | 44.7 | 16.8 |
| Binary type ID | 22.9 | 58.1 | 17.9 |
| Single pass, RTTI chain | 440.1 | 636.6 | 730.6 |
| Single pass, type ID | 17.3 | 42.6 | 11.3 |

Linear type-ID lookup has a lower aggregate median than binary lookup in all
15 tested count/scenario combinations. With five components and mixed hits,
changing only the outer traversal to a single RTTI pass helps little; avoiding
the repeated RTTI checks has the much larger effect. A single pass still makes
up to five type checks per component when implemented as a cast chain.

This supports trying linear integer-ID comparison before adding a binary search
for such small collections. It does **not** establish a scene-frame speedup:
real component sizes, hierarchy, DLL boundaries, cache behavior and visitor work
are not reproduced. Uniform all-hit/all-miss cases also train branch prediction
more strongly than mixed workloads. No allocations, memory footprint or GPU work
were timed. All runs passed with empty motor memory-manager dumps.

- [Per-case summary and run-median ranges](results/component-search-2026-10-23/summary.csv).
- Individual samples: [run 1](results/component-search-2026-10-23/run1.csv),
  [run 2](results/component-search-2026-10-23/run2.csv),
  [run 3](results/component-search-2026-10-23/run3.csv).

```powershell
cmake -S . -B build
cmake --build build --config Release --target 03_component_search --parallel 2
ctest --test-dir build -C Release -R '^core_component_search$' --output-on-failure
./build/bin/Release/03_component_search.exe
./build/bin/Release/03_component_search.exe --csv component-search.csv --nodes 4096 --sweeps 16 --rounds 7
```

CTest uses `--verify-only` and has no timing threshold. The executable can also
be started directly from Visual Studio; it needs no assets or working directory.

## Document tokenization

## Sponza OBJ via motor IO

`01_document_obj` loads `suite_core/working/sponza.obj` through
`motor::io::database_t(path_t(DATAPATH), "./working", "data")`, followed by
`load(location_t("sponza.obj")).wait_for_operation(...)`. No std::ifstream is
used. DATAPATH is the source suite_core directory, so launching from a build
directory also works. The OBJ is external test data; it is not copied or packed.

```powershell
cmake --build build --config Release --target 01_document_obj
./build/bin/Release/01_document_obj.exe
./build/bin/Release/01_document_obj.exe --verify-only
./build/bin/Release/01_document_obj.exe --location another.obj
ctest --test-dir build -C Release -R '^core_document_obj$' --output-on-failure
```

Locations are relative to the database's working directory and use motor's
location syntax. CTest registers the correctness-only test when sponza.obj
exists at CMake configure time; no benchmark timing threshold is imposed.

The test measures database initialization and load/wait/copy separately, then
destroys the database before benchmarking. Disk timing is not a cold-cache
measurement. Each tokenizer retains its own source copy and all tokens until
traversal finishes. Standard-library baselines intentionally use std allocators.
Document additionally normalizes whitespace and retains line metadata.
The stream baseline also creates its own istringstream buffer.

Construction/tokenization, traversal with a character-wise checksum, destruction
and total time are measured separately. Count/reserve is inside construction.
One warmup and five measured rounds rotate the implementation order; reported
phase medians need not sum to the median total. These are repeated, warm runs.
The independent whitespace scanner validates token count, bytes and checksum;
document's for_each_line and for_each_token must also agree. Comments and face
indices are tokenized as text, not interpreted as mesh data. Counts for v/vt/vn/f
are reported, but this is not a complete OBJ importer benchmark.

Lines reaching 4096 bytes are rejected before constructing document to avoid
its current fixed scratch-buffer limit. No allocation-count or peak-memory
measurement is claimed. The final motor dump does not track std allocations.

Local Windows/MSVC Release result (24,424,786 bytes, 2,187,283 tokens):

| Implementation | Build | Traverse + hash | Destroy | Total |
| --- | ---: | ---: | ---: | ---: |
| motor::document | 167.652 ms | 36.715 ms | 4.649 ms | 210.469 ms |
| std::string + substr | 261.634 ms | 35.543 ms | 43.219 ms | 339.763 ms |
| std::string + count/reserve + substr | 262.735 ms | 34.947 ms | 45.195 ms | 340.347 ms |
| stringstream | 696.819 ms | 35.122 ms | 45.612 ms | 776.545 ms |

Database init: 4.206 ms; load/wait/copy: 52.107 ms. Document reports 569,548
lines, 145,185 vertex records, 87,715 UV records, 72,781 normal records and
262,267 face records. All comparisons passed with an empty motor memory dump.
These numbers describe this one local run, not portable speed guarantees.

## Counting and reservation comparison

`--benchmark` also runs count/reserve variants of all three STL implementations.
Their exact token count and reserve happen inside the timed interval. Counting
recognizes transitions from space/tab/CR/LF to non-whitespace; it does not allocate.
`--benchmark-counting` measures this count separately, and also an LF-only count
using std::count. LF counting models the document line-reservation strategy, but
is not an isolated measurement of document's internal code or its token reserve.
The standalone counts exclude source construction, reserve and destruction.

One local Release measurement on the same 646058-byte corpus:

| Representation | Without pre-count | With count/reserve |
| --- | ---: | ---: |
| std::string + substr | 6.950 ms | 5.072 ms |
| stringstream | 15.077 ms | 13.631 ms |
| string_view | 3.381 ms | 3.789 ms |

Document (unchanged, with its own reservations): 4.448 ms. Standalone exact token
count: 0.479 ms; LF count: 0.012 ms (31-sample medians, warm repeated scans).
Do not subtract standalone medians from total medians to derive exact phase costs.
No document-without-reserve variant was implemented, so these measurements do not
quantify how much document itself gains from reserve. Results are workload-specific.

`00_document` compares motor's document constructor/token iterator with standard
string/substr, stringstream extraction, and a string_view baseline. The standard
implementations retain all tokens before traversal, as document does. Each copies
the source; string_view tokens reference that private copy. Standard types in the
baselines are intentional. Application data and logging use motor.

CTest checks exact token contents and ordering, including LF, CRLF, whitespace,
blank lines and an indented final line without a newline. These are whitespace
tokenization tests, not a programming-language lexer. Punctuation stays attached.
Indent metadata and document writing are not covered.

Run from the repository root:

```powershell
cmake --build build --config Release --target 00_document
ctest --test-dir build -C Release -L core --output-on-failure
./build/bin/Release/00_document.exe --benchmark
./build/bin/Release/00_document.exe --benchmark-traversal
```

The benchmark uses 8192 short scene-like lines, short and long tokens, one warmup,
nine samples, rotating implementation order, and median elapsed time. It includes
construction, normalization/tokenization, hashing every token, and destruction.
Every digest and token count is checked. There is no CI speed threshold.

This is not equal functionality: document also stores line/indent metadata and
normalizes text; the baselines only tokenize. Allocators differ as well. Results
describe this workload, not general superiority. Release builds are important.
No allocation count or peak-memory comparison is claimed; motor's final leak
check cannot account for standard-library allocations.

The separate `--benchmark-traversal` mode builds all representations beforehand
and keeps them alive. It times only traversal and hashing of every token character,
with one warmup and 31 measured traversals per representation in rotating order.
Every digest is verified outside the timed interval. No parsing, source copying,
token allocation or container destruction is timed. The stringstream entry reads
its already extracted string tokens, not the stream again: extracting again would
measure tokenization. Both owning-string entries therefore use the same container
type. These are warm repeated-access measurements, not cold-cache results.
Hashing costs are included and may dominate; this is not an isolated iterator
overhead benchmark. Document uses its public callback API, the baselines range-for.

Long lines are deliberately not exercised: the current document implementation
uses a fixed 4096-byte stack buffer without a length check. An oversized-input
test should be isolated and sanitizer-backed before enabling it in normal CI.

## Initial local results

Windows/MSVC Release, 646058 bytes, 49152 tokens, median of nine runs:

| Implementation | Milliseconds |
| --- | ---: |
| document | 5.281 |
| std::string + substr | 7.926 |
| stringstream | 18.129 |
| std::string_view | 3.898 |

These are one local measurement, not portable performance guarantees.

Two correctness tests currently fail in the unchanged motor dependency:
`final_line` produces four rather than three tokens; `whitespace` produces one
rather than zero. Both expose an extra empty token. Basic cases and blank lines
pass. These failures intentionally remain visible in CTest/CI, not marked as
expected successes. Fix the engine and update its submodule to resolve them.
