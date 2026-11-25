# device_guard

**Describes:** `modules/engine/execution/source/runtime_context.cpp`, `modules/engine/execution/source/host_form_window.hpp`, `modules/engine/execution/source/scheduler_execution_engine.cpp`, `modules/engine/execution/source/host_execution_engine.cpp`, `modules/engine/execution/source/host_step_run.cpp`, `modules/engine/execution/source/chain_run.cpp`, `modules/engine/execution/source/chain_replay.cpp`

What `RuntimeSettings::guard_device_memory` builds and which windows open it. The
[module README](../README.md) links this page.

## guard_device_memory

`guard_device_memory` puts a runtime's Device USM on `GuardedDevicePages`, so a host read or write
of a Device allocation faults unless the runtime has opened the pages. It catches the host
dereference that passes on a backend whose device memory the host can address and faults on CUDA.
It is off by default. `create()` refuses it with `Unsupported` unless every device is a CPU device,
because a GPU cannot reach the host pages. The context wraps its USM source with
`make_guarded_usm_source()`, wraps each transfer path with `make_guarded_transfer_path()`, and
hands the pages out as `device_access_gate()`. The windows that open them:

| Who | Opens | Closes |
| --- | --- | --- |
| `SchedulerExecutionEngine` | in `submit()`, before the roots go to the pool | at the end of `complete()` |
| `ChainRun`, `ChainReplay` | when the run is built | when the run is destroyed |
| `HostExecutionEngine`, `HostStepRun` | before a kernel's `host_form` | after it returns |
| `USMAllocator::copy()` | around the copy | when the copy has waited |
| each transfer path | around the copy | when its event has completed |

A native host node of a host run is outside every window, so its read of a Device buffer faults.
On the scheduler and the chain the window spans the whole run, and a host node or a host thread
that touches Device memory during it is not caught. An open window opens the pages to every thread
of the process, not only to the one that opened it. Two guarded runtimes have pages of their own,
and opening one runtime's pages leaves the other's shut.

## device_access_gate

Returns the `GuardedDevicePages` the context made, as the `DeviceAccessGate` its engines and runs
open, or null when `guard_device_memory` is off. The pages live as long as the allocator that
serves from them, so an allocation that outlives the context is still released to them.
`create_host()` takes the setting as its last parameter.
