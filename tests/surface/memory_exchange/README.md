# memory_exchange tests

The tests of the `memory_exchange` module, mirroring `modules/surface/memory_exchange/`.
`tests/surface/memory_exchange/CMakeLists.txt` builds them into `sr_memory_exchange_test_objects`, linked into `sr_surface_tests`. `sr
test` runs them with the rest; `sr run sr_surface_tests -- --gtest_filter='<pattern>'` runs
one program alone.

A section is named after a test file, and a subsection after the test, helper or topic it describes.

## test_memory_exchange.cpp

The file tests the memory exchange: capabilities, a host export, and an import that names no mechanism, the capabilities and the import through a `CapabilityCache` of its own that answers from a `ScriptedCapabilityProbe`, on the program's one runtime, because `Interop::capabilities` and the import call a device queue and have no host path. The host export runs small, on a host runtime. The suite `Medium_MemoryExchangeImport` holds the import test that keeps the wiring; it skips on a device that is not a GPU, which refuses every import before it reads the descriptor.

| Test | What it proves |
| --- | --- |
| `CapabilitiesOfDeviceZero` | Device 0 reports the capabilities its probe answered. Suite `Medium_MemoryExchange`. |
| `AHostExportGivesTheSizeAsked` | A host export gives an allocation of the size asked. Suite `Small_MemoryExchange`. |
| `AnImportWithNoKindIsRefused` | An import runs the descriptor checks: one naming no handle kind is refused with InvalidAccess, and the message names `Unspecified`. Suite `Medium_MemoryExchangeImport`. |

## test_descriptor_refusal.cpp

The file runs `descriptor_refusal()` and `device_refusal()` on values alone, with no device.

| Test | What it proves |
| --- | --- |
| `ADescriptorWithNoKindIsRefused` | A descriptor naming no handle kind is refused with InvalidAccess, and the message names `Unspecified`. |
| `AKindTheDeviceDoesNotImportIsRefusedByName` | A device that imports only `OpaqueFd` refuses an `OpaqueWin32` descriptor with InvalidAccess, and the message names `OpaqueWin32`. |
| `ARegionPastTheAllocationIsRefused` | A region that ends past `allocation_bytes` is refused with InvalidAccess. |
| `ADescriptorTheDeviceImportsIsNotRefused` | A descriptor of a kind the device imports, inside its allocation, passes. |
| `ADeviceWithNoUuidIsRefused` | A device that reports no UUID is refused with InvalidAccess (suite `Small_DeviceRefusal`). |
| `ANullWin32HandleIsRefusedByName` | A null `OpaqueWin32` handle is refused with InvalidAccess, and the message says `descriptor.handle is null`. |
| `AFileDescriptorAboveIntIsRefusedWithItsValue` | An `OpaqueFd` handle of `INT_MAX + 1` is refused, and the message carries the value. |
| `ARegionPastTheAllocationIsRefusedWithItsNumbers` | Sixteen bytes at offset 60 of a 64-byte allocation are refused, and the message carries the byte count, the offset and the allocation size. |
| `AnOffsetPastTheAllocationIsRefusedWithoutWrapping` | An offset of 65 in a 64-byte allocation with zero bytes is refused with its offset; the subtraction that follows cannot wrap. |
| `AnotherDevicesUUIDIsRefusedWithBothUUIDs` | A descriptor whose last UUID byte is inverted is refused, and the message carries `descriptor.device is` and `and device` (suite `Small_DeviceRefusal`). |
| `TheDevicesOwnUUIDIsNotRefused` | A descriptor naming the device's own UUID passes (suite `Small_DeviceRefusal`). |

## test_capability_cache.cpp

The file tests `CapabilityCache` with a `ScriptedCapabilityProbe` and the device SYCL selects by default.

| Test | What it proves |
| --- | --- |
| `EachDeviceIsProbedOnce` | A device is probed once over two queries, and both return its answer. |
| `AThrowingProbeKeepsNothing` | A probe that throws passes the throw on and keeps no answer, so the next query probes again and returns its answer. |

## test_scripted_capability_probe.cpp

A `ScriptedCapabilityProbe` answers from a queue the test fills. The tests call `next_answer()`, which
needs no `sycl::device`. `operator()` forwards to it and is not exercised here, since it takes a device.

| Test | What it proves |
| --- | --- |
| `ReturnsTheQueuedAnswersInOrder` | Queued answers come back in queue order, and each call is counted. |
| `RaisesTheQueuedFailureThenAnswersTheNext` | A queued throw is raised by its own call and does not consume the next answer. |
| `RefusesACallWithNothingQueued` | A call with nothing queued fails with `std::logic_error`, and is still counted. |

## compile_memory_exchange_log_category.cpp

Includes `memory_exchange_log_category.hpp` alone, so the build fails if the header misses an include. It holds no test.
