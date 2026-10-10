# Memory Tests

Console regression tests for Motor's memory APIs. No graphics context or assets
are required. Every scenario runs in its own process and checks Motor's memory
dump after releasing test objects and shutting down logging.

## Ownership

`00_memory_ownership <scenario>`:

| Scenario | Checks |
| --- | --- |
| `sharing` | Borrow preserves identity, share retains ownership, move clears its source, final release destroys once |
| `guard` | `mtr_release_guard` releases on early return and transfers ownership through `move()` |
| `concurrent` | Six threads repeatedly share/release one immutable object while an owning reference keeps it alive |

## Allocations

`01_memory_allocations <scenario>`:

| Scenario | Checks |
| --- | --- |
| `raw` | Several allocation sizes, purpose metadata, byte contents and exact tracked-byte accounting |
| `typed_array` | Each element in an allocated array is destroyed exactly once |
| `containers` | Motor vector/string allocations, independent copies, moves and complete cleanup |
| `malloc_copy` | Typed copy contents, element count and allocation size without multiplying `sizeof(T)` twice |
| `malloc_move` | Move construction and explicit pointer extraction without double release |
| `malloc_assignment` | Move assignment into a nonempty guard releases its previous allocation |

Allocation counts here refer to Motor's tracked bytes, not process RSS or total
system allocations. They test correctness rather than allocator speed.

## Running

```sh
cmake --build build --config Release --parallel 4 --target 00_memory_ownership 01_memory_allocations
ctest --test-dir build -C Release -L memory --output-on-failure --no-tests=error
```

For both new areas, use `-L "concurrent_extended|memory"`. The Windows and Linux
CTest workflows build and run these tests in Debug and Release. Regression
failures intentionally remain failures; they are not marked `WILL_FAIL`.

## Initial Findings

Tested against Motor `8201afae` on Windows (MSVC, Debug and Release) and Linux
(GCC, Debug, WSL). Five of the nine memory scenarios pass. Four expose existing
engine issues; the tests do not modify the engine:

| Scenario | Observed result | Implementation |
| --- | --- | --- |
| `raw` | Purpose lookup returns false even for a tracked allocation; an unconditional `return false` remains after commented-out code | [manager.cpp](../motor/memory/manager/manager.cpp) |
| `typed_array` | Destruction counts are `0, 4, 0, 0`; the loop uses `ptr + 1` instead of `ptr + i` | [global.h](../motor/memory/global.h) |
| `malloc_copy` | Four unsigned integers allocate 64 bytes instead of 16; the element size is multiplied twice | [malloc_guard.hpp](../motor/memory/malloc_guard.hpp) |
| `malloc_assignment` | The destination's previous allocation is lost; the memory dump reports seven leaked bytes | [malloc_guard.hpp](../motor/memory/malloc_guard.hpp) |

Consequently, the expanded CTest workflows will report failures until these
regressions are fixed in the Motor version used by the suite.
