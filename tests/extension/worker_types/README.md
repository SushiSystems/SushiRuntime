# worker_types tests

The tests of the `worker_types` module, mirroring `modules/extension/worker_types/`.
`tests/extension/worker_types/CMakeLists.txt` builds them into `sr_worker_types_test_objects`, linked into
`sr_extension_tests`. `sr test` runs them with the rest; `sr run sr_extension_tests --
--gtest_filter='<pattern>'` runs one program alone.

A section is named after a test file, and a subsection after the test, helper or topic it describes.

## distributed/test_kernel_registry.cpp

### `SealedRegistryStopsARegistration`, `SealingKeepsTheRegisteredKernels`, `EmptyKernelStopsTheRegistration`

Finding QC-37. `Medium_KernelRegistryDeathTest` needs no queue: its kernel returns a default event
and is never called. After `seal()`, `register_kernel` must stop the process for a new operation
and for one already registered. `SealingKeepsTheRegisteredKernels` runs small: sealing marks the
registry sealed and keeps what it held. A default-constructed `Kernel` must stop a registration on
an open registry and on a sealed one, with the message that names the empty kernel.

## distributed/test_buffer_view.cpp

The file tests BufferView: the bytes it names, read as an element type.

| Test | What it proves |
| --- | --- |
| `AsTypesTheAddress` | As() returns the address of the bytes, typed as the element asked for. |
| `ADefaultViewNamesNothing` | A default view names no bytes. |

## distributed/test_worker_statistics.cpp

The file tests `WorkerStatistics`, the counters of a worker agent.

| Test | What it proves |
| --- | --- |
| `Small_WorkerStatistics.EveryCounterStartsAtZero` | A new `WorkerStatistics` reports no task, no cache byte and no registration. |
