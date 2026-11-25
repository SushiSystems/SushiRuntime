# Host device isolation: plan

**Status:** Open — phase 1 implemented and syntax-checked on 2026-11-25; phases 2 to 4 wait on the owner.
**Designs:** GLOBAL_HANDLE

SushiAI keeps the tensors only kernels read in `Residency::Device` (sushiai RW-201). On CUDA a host
dereference of such a tensor faults with 0xc0000005. On the CPU device the test suites run on,
device USM is ordinary host memory, so the same dereference reads or writes and the test passes.
Six such defects were found only on CUDA runs. This work makes the host backend fault the same way.

## What the backends do today

- `Runtime::create()` on the CPU device builds `make_sycl_usm_source()`. `sycl::malloc_device` on
  that device returns host-addressable memory, and SushiAI's tests run there
  (`exchange_test_support.hpp` turns the GPU off; most tests take the default filter on a CPU
  machine).
- `Runtime::create_host()` builds `make_host_usm_source()`: every kind of USM comes from the heap.
  Its engine runs a launch's host form on the calling thread.
- `Buffer::read_range()` and `write_range()` copy through `USMAllocator::copy()`, which calls
  `USMSource::copy()`. Kernels run from graph nodes the scheduler dispatches and, in SushiBLAS,
  from command groups submitted to `AdvancedControls::queue()`. SushiBLAS's
  `RuntimeBlock::copy_to_host()` and `copy_from_host()` call `queue.memcpy()` on that queue, and
  SushiAI's `host_copy.hpp` goes through them. The runtime does not see these submissions.

## Decisions

| Id | Decision | Reason |
| --- | --- | --- |
| D1 | A guarded Device allocation lives on pages of its own, mapped with `VirtualAlloc` on Windows and `mmap` elsewhere, and is `PAGE_NOACCESS` / `PROT_NONE` while no runtime kernel or copy runs. | A stray access then faults at the instruction that made it, as it does on CUDA. A shadow allocation with explicit transfer would only produce stale numbers later, which a test may not notice. Per-thread protection keys (`pkey_mprotect`) exist only on Linux on x86 with PKU and have no Windows counterpart. |
| D2 | One counted window opens every guarded page of a source at once: `DeviceAccessGate::open()` makes them read-write when the count leaves 0, `close()` makes them inaccessible when it returns to 0. | Page protection is per process on both systems, so a page cannot be open to a worker thread and closed to the host thread. A per-buffer window would fault correct kernels: a launch, a raw command group and an untracked node capture addresses the runtime cannot list. What this costs: host code that runs while a window is open, another thread during an asynchronous run or a host body inside a run, is not caught. Host code between runs is caught every time. |
| D3 | Three units in `modules/platform/memory`: `page_protection.hpp` with one adapter per system (`page_protection_windows.cpp`, `page_protection_posix.cpp`); `GuardedDevicePages`, which maps, protects and releases the pages and implements `DeviceAccessGate`; `make_guarded_usm_source()`, a `USMSource` decorator that sends Device requests to the pages, forwards every other call to the source it wraps, and opens the window around `copy()`. `DeviceAccessScope` opens and closes a gate as an RAII object and does nothing for a null gate. | Each unit has one reason to change. The decorator wraps the host source and the SYCL source alike, so neither is edited (OCP), and the platform branch sits in two adapter files as the platform-portability skill asks. |
| D4 | A guarded allocation starts on a page boundary and takes whole pages, one page for a zero-byte request. An alignment above the page size, or one that is not a power of two, is refused with null, as the host source refuses one. | Page granularity is what protection works on. SushiBLAS asks for `K_STORAGE_ALIGNMENT`, below a page. |
| D5 | Release runtimes never build the guarded source. The runtime picks it at construction (phase 2), and the window hooks of phase 3 take a `DeviceAccessGate*` that is null when the guard is off, which costs one pointer test per run. | Opening and closing costs two system calls per guarded allocation per window, acceptable in a test run and not in a device run. |
| D6 | Proposed, the owner decides (public interface): `RuntimeConfig::guard_device_memory` and `HostRuntimeConfig::guard_device_memory`, `false` by default. SushiAI's tests turn it on where they build a runtime: `Test::` runtime factories, `named_runtime()` in `exchange_test_support.hpp` and `cluster_fabric.hpp`. | A config field is explicit, holds per runtime and needs no process-wide state. An environment variable would turn the guard on for every runtime of a process without a code change, but it is a second, hidden interface. |
| D7 | Proposed, the owner decides: work the runtime cannot see opens the window itself. SushiBLAS's `RuntimeBlock` copies move to `Buffer::read_range()` / `write_range()`, or SushiRuntime publishes `DeviceAccessScope` beside `AdvancedControls::queue()` for a consumer that submits to the queue directly. | Without one of these, a guarded runtime faults SushiBLAS's correct copies, the path SushiAI's `host_copy.hpp` uses. The runtime cannot open a window around a submission it does not see. |
| D8 | Whether the OpenCL CPU device runs a kernel over memory that `sycl::malloc_device` did not return is not verified. Phase 3 opens with a medium test that does it; if the device refuses, the guard serves `create_host()` runtimes only. | No build or run happens in phase 1. |

## Phases

| Phase | Work | State |
| --- | --- | --- |
| 1 | `page_protection`, `GuardedDevicePages`, `DeviceAccessGate`, `DeviceAccessScope`, `make_guarded_usm_source()`, their tests and the module README | Implemented, syntax-checked; not built or run |
| 2 | The selector of D6 in `RuntimeConfig` and `HostRuntimeConfig`; `RuntimeContext` wraps the source and keeps the pages as its gate | Waits on the owner |
| 3 | The windows: `HostExecutionEngine` around a node's host form, the scheduler engine from `submit()` to `complete()` and around `execute()`, the chain runs, and D8's test | Waits on phase 2 |
| 4 | D7: the consumer copies and raw submissions | Waits on the owner; crosses into SushiBLAS |

## Tests of phase 1

| Test | Done when |
| --- | --- |
| `Medium_GuardedUSMSourceDeathTest.AHostReadOfADeviceAllocationFaults` | A read through the address of a Device allocation kills the process |
| `Medium_GuardedUSMSourceDeathTest.AHostWriteOfADeviceAllocationFaults` | A write through it kills the process |
| `Small_GuardedUSMSource.AKernelInsideAnOpenScopeReachesADeviceAllocation` | A host loop, the host form of a kernel, writes and reads the allocation inside a `DeviceAccessScope` |
| `Small_GuardedUSMSourceCopy.ACopyThroughTheAllocatorRoundTripsADeviceAllocation` | `USMAllocator::copy()`, the call `read_range()` and `write_range()` make, writes bytes in and reads the same bytes out |
| `Small_GuardedUSMSource.ASharedAllocationStaysHostAddressable` | A Shared request goes to the wrapped source and the host writes it |
| `Small_GuardedUSMSource.ADeviceAllocationReportsTheDeviceKind` | `kind_of()` answers `USMKind::Device` for an address inside the pages |
| `Small_GuardedUSMSource` contract rows | `USMSourceContract` passes on the decorator over the host source |
| `Small_GuardedDevicePages.ANestedCloseLeavesThePagesOpen` | After two opens and one close the pages are still reachable |
| `Medium_GuardedDevicePagesDeathTest.AnAllocationMadeInsideAWindowClosesWithIt` | An allocation made while the window is open faults once the window closes |
| `Small_GuardedDevicePages.RefusesAnAlignmentAboveThePageSize` | The request returns null |
| `Small_DeviceAccessScope.OpensTheGateWhileItLives` | The gate is open while the scope lives |
| `Small_DeviceAccessScope.ClosesTheGateWhenItEnds` | The gate is closed once the scope is destroyed |
| `Small_PageProtection.ReadWritePagesStartZeroedAndTakeAWrite` | A read-write page reads 0, then the byte written |
| `Small_PageProtection.PagesOpenedAgainKeepTheirBytes` | A page closed and opened again keeps its byte |
| `Medium_PageProtectionDeathTest.AReadOfPagesWithNoAccessFaults` | A read of a page with no access kills the process |

A death test is medium because it starts a child process (`tests/README.md`).
