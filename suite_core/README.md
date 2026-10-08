# Document tokenization

## Sponza OBJ via Motor IO

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

Locations are relative to the database's working directory and use Motor's
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
measurement is claimed. The final Motor dump does not track std allocations.

Local Windows/MSVC Release result (24,424,786 bytes, 2,187,283 tokens):

| Implementation | Build | Traverse + hash | Destroy | Total |
| --- | ---: | ---: | ---: | ---: |
| motor::document | 167.652 ms | 36.715 ms | 4.649 ms | 210.469 ms |
| std::string + substr | 261.634 ms | 35.543 ms | 43.219 ms | 339.763 ms |
| std::string + count/reserve + substr | 262.735 ms | 34.947 ms | 45.195 ms | 340.347 ms |
| stringstream | 696.819 ms | 35.122 ms | 45.612 ms | 776.545 ms |

Database init: 4.206 ms; load/wait/copy: 52.107 ms. Document reports 569,548
lines, 145,185 vertex records, 87,715 UV records, 72,781 normal records and
262,267 face records. All comparisons passed with an empty Motor memory dump.
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

`00_document` compares Motor's document constructor/token iterator with standard
string/substr, stringstream extraction, and a string_view baseline. The standard
implementations retain all tokens before traversal, as document does. Each copies
the source; string_view tokens reference that private copy. Standard types in the
baselines are intentional. Application data and logging use Motor.

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
No allocation count or peak-memory comparison is claimed; Motor's final leak
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

Two correctness tests currently fail in the unchanged Motor dependency:
`final_line` produces four rather than three tokens; `whitespace` produces one
rather than zero. Both expose an extra empty token. Basic cases and blank lines
pass. These failures intentionally remain visible in CTest/CI, not marked as
expected successes. Fix the engine and update its submodule to resolve them.
