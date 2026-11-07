# api tests: interop

The tests of importing and exporting memory. They are part of `sr_surface_tests`;
[the page of the module's tests](../README.md) says how to run them.

## test_interop.cpp

The file covers what `import_device` refuses, what `capabilities()` reports and what `borrow()`
keeps. Its small tests run on a host runtime and its medium tests on the program's runtime. No test imports a real allocation: the descriptors
hold a placeholder handle of 1, the refusal tests expect a refusal, and the zero-element test
requests no elements. A test that needs a device the machine may lack skips in its fixture, not in its
body.

### Fixtures

| Fixture | What it holds |
| --- | --- |
| `Medium_InteropCapabilities` | Rows of device index 0 to 3; a row past the machine's last device skips. `Medium_InteropImportingDevice` also skips a device that does not import. |
| `Small_InteropExportAsCount` | Runs on a host runtime. |
| `Medium_InteropImportOnADeviceWithoutImport` | Finds the first device that does not import, and skips when every device imports or the toolchain has no bindless images, since it then refuses before it reads a device. |
| `Medium_InteropImport` | Finds the first device that imports, and skips when none does. `acceptable_descriptor` builds a descriptor with this platform's handle kind, the placeholder handle 1, an allocation of 64 bytes and the UUID of that device; a test then breaks one field, so the refusal it sees can only come from that field. |
| `Small_InteropBorrow` | The borrow tests over memory the host runtime allocated as `Residency::Host`, whose kind its allocator records. `lent_memory` allocates it. |
| `Medium_InteropBorrow`, `Medium_InteropBorrowHostUSM` | The borrow tests over memory SYCL allocated, on the program's runtime; the second skips where device 0 offers no host USM. |

### Capabilities and exports

| Test | What it proves |
| --- | --- |
| `HostExportReportsWhetherTheQueueOffersHostUSM` | `host_export` equals `sycl::aspect::usm_host_allocations` of the device behind the queue of that row's device. |
| `ReportsItsUUID` | A device that imports external memory reports a device UUID. |
| `AnExportIsHostResident` | An exported buffer reports `Residency::Host` and its `address()` is its `data()`. |
| `ALateCountTakesAnExportAsItsCounter` | `sized_from_device()`, which stops the process for any other residency, takes an export as its counter, and a task sized from it runs the count the host wrote there. |

### Imports

| Test | What it proves |
| --- | --- |
| `ADeviceWithoutImportIsRefusedByItsIndex` | The capability is checked before the descriptor. The refusal is `ErrorCode::Unsupported` and its message carries the device index and `device_import is false`. |
| `ZeroElementsImportNothingAndReturnAnEmptyBuffer` | `import_device` with a count of zero, on the first device that imports, returns a buffer that is empty and has size zero. |
| `IsRefusedWithInvalidAccess` | Rows of one descriptor each broken in one field. A null Win32 handle (Windows) or a file descriptor above `INT_MAX` (elsewhere) is refused naming the handle. Four floats at offset 60 end at byte 76 of a 64-byte allocation, and the message carries the byte count, the offset and the allocation size. An offset of 65 with zero bytes is refused with its offset, since the check compares the offset first and the subtraction that follows cannot wrap. A descriptor with the last byte of its device UUID inverted is refused with both UUIDs. |

The checks on the kind, the region and the device live in pure functions of
`modules/surface/memory_exchange/include/SushiRuntime/memory_exchange/descriptor_refusal.hpp`, whose small rows are in
`tests/surface/memory_exchange/test_descriptor_refusal.cpp`: an unspecified kind, a kind the device
does not import, a region past the allocation, and a device with no UUID. This file keeps only the
rows that file does not yet hold, as medium rows through the import; RW-112 moves them there. A
device that reports no UUID is refused with its own message; no test here reaches that case, because
a device cannot be made to drop its UUID.

### Borrowed buffers

| Test | What it proves |
| --- | --- |
| `ABorrowedBufferViewsTheCallersMemory` | The buffer's address, size, device and residency are the ones passed, and the host reads the caller's vector through it. |
| `AWriteThroughABorrowedBufferReachesTheCallersMemory` | A write through the buffer changes the caller's vector. |
| `ADestroyedBorrowedBufferReleasesItsShareOfTheOwner` | The buffer keeps no share of the owner token once it is destroyed. |
| `ARunNamingABorrowedBufferRunsWhileTheOwnerLives` | A host task over the buffer runs. |
| `ARunNamingABorrowedBufferWhoseOwnerLetGoIsRefused` | After the owner token is reset the run is `DanglingResource` and the task does not run. |
| `DroppingTheBorrowedHandleLeavesTheRunValid` | The borrowed handle is destroyed after recording and the run still executes the task, because the owner token lives. |
| `ARangeCopyReachesBorrowedMemoryTheRuntimeDidNotAllocate` | Over shared USM from `sycl::malloc_shared`, which the runtime does not record, `write_range()` and `read_range()` copy through the queue of the buffer's device. |
| `AMismatchedResidencyIsRefusedWithBothNames` | Host USM from `sycl::malloc_host` borrowed as `Residency::Shared` is refused with `InvalidArgument`, and the message names both residencies. |
| `ARuntimeBufferBorrowedAsAnotherResidencyIsRefusedByItsRecord` | A `Residency::Host` buffer the runtime allocated, borrowed as `Residency::Shared`, is refused with `InvalidArgument` from the kind the allocator recorded. |
| `ARuntimeBufferBorrowedAsItsOwnResidencyIsAccepted` | Borrowed as `Residency::Host` it is accepted. |

## test_host_export.cpp

The file tests exporting host memory a device and the host both reach.

| Test | What it proves |
| --- | --- |
| `AlignsAndRoundsTheRegion` | The exported region is aligned to the requested alignment, its byte size is rounded to it and holds the elements asked for. Rounding is a decision on values; no seam makes it small. |
| `ARangeWrittenToAnExportReadsBack` | `write_range()` and `read_range()` over the exported buffer give the values written, so the address routes to the device that owns it. |
| `AKernelWritesWhatTheHostReads` | A kernel writing through the exported buffer is visible from the host. |
| `ImportOnADeviceWithoutTheExtensionReturnsUnsupported` | `import_device` on a device without `device_import` returns `Unsupported`; the fixture skips where device 0 imports. |
| `AnAlignmentThatIsNotAPowerOfTwoStops`, `ASizeThatOverflowsWhenRoundedStops` | The two preconditions of `export_host` stop the process with their messages. |
| `AnExportNoDeviceCanSatisfyReturnsAllocationFailed`, `AnExportAfterAFailedOneSucceeds` | An export of half the address space in bytes returns `AllocationFailed`, and a later export of 16 floats succeeds. |
