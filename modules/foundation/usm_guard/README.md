# usm_guard

The module holds `USMShared`, the move-only guard that frees a shared USM allocation, so a failing
assertion never leaks one. It is an internal module built only with the tests: its headers are
never installed and the library does not link it.

It depends on SYCL and on no other module of the repository.

## Public entry points

| Header | Holds |
| --- | --- |
| `usm_shared.hpp` | `USMShared` |
| `shared_allocation_source.hpp` | `SharedAllocationSource`, the port a guard allocates and frees through |
| `queue_shared_allocation_source.hpp` | `QueueSharedAllocationSource`, shared USM of one SYCL queue |
| `in_memory_shared_allocation_source.hpp` | `InMemorySharedAllocationSource`, host memory on a 64-byte boundary that counts allocations and frees |

Every name is declared in `SushiRuntime::Test`.

## Where a guard's memory comes from

`USMShared(queue, count)` allocates through a `QueueSharedAllocationSource` it owns, with
`sycl::aligned_alloc_shared` at the element's alignment. `USMShared(source, count)` allocates
through a `SharedAllocationSource` the caller keeps alive, and frees through it; a move hands the
allocation and the source over. `InMemorySharedAllocationSource` refuses an alignment above 64
bytes with null. Both sources pass `SharedAllocationSourceContract`
(`tests/support/shared_allocation_source_contract.hpp`).
