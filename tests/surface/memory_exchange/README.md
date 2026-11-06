# memory_exchange tests

The tests of the `memory_exchange` module, mirroring `modules/surface/memory_exchange/`.
`tests/surface/memory_exchange/CMakeLists.txt` builds them into `sr_memory_exchange_test_objects`, linked into `sr_surface_tests`. `sr
test` runs them with the rest; `sr run sr_surface_tests -- --gtest_filter='<pattern>'` runs
one program alone.

A section is named after a test file, and a subsection after the test, helper or topic it describes.

## test_memory_exchange.cpp

The file tests the memory exchange: capabilities, a host export, and an import that names no mechanism, each through a `CapabilityCache` of its own that answers from a `ScriptedCapabilityProbe`, on the program's one runtime. The suite `Medium_MemoryExchangeImport` holds the two import tests; it skips on a device that is not a GPU, which refuses every import before it reads the descriptor.

| Test | What it proves |
| --- | --- |
| `CapabilitiesOfDeviceZero` | Device 0 reports the capabilities its probe answered. |
| `AHostExportGivesTheSizeAsked` | A host export gives an allocation of the size asked. |
| `AnImportWithNoKindIsRefused` | An import naming no handle kind is refused with InvalidAccess, and the message names `Unspecified`. Suite `Medium_MemoryExchangeImport`. |
| `AKindTheDeviceDoesNotImportIsRefusedByName` | A device that imports only `OpaqueFd` refuses an `OpaqueWin32` descriptor with InvalidAccess, and the message names `OpaqueWin32`. |

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
