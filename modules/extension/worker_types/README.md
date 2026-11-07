# worker_types

The module holds the public types a worker and the bricks it is built from share: the view of a
buffer a kernel reads or writes, the registry of the kernels a worker can run, and the counters of
a worker agent. They sit below `distributed` so that `dispatch_executor` and `distributed` both use
them without one depending on the other. It is built with the distributed layer, under
`SR_ENABLE_DISTRIBUTED`, and every name it declares is in `SushiRuntime::Distributed`. The headers
keep the include paths they had in `distributed`.

It depends on `core` and `api`, and its source on `log`.

## Public entry points

| Header | Holds |
| --- | --- |
| `distributed/buffer_view.hpp` | `BufferView` |
| `distributed/kernel_registry.hpp` | `KernelRegistry` and its `Kernel` form |
| `distributed/worker_statistics.hpp` | `WorkerStatistics` |

## buffer_view.hpp

`BufferView` names bytes: an address and a size. `as<T>()` types the address as the element asked
for. A kernel receives its inputs and outputs as views.

## kernel_registry.hpp

`KernelRegistry` maps an operation to the kernel that runs it. It is open until a `WorkerAgent` is
built with it and sealed from then on, so no thread writes it while the exec thread reads it. A
registration on a sealed registry, or of an empty `Kernel`, stops the process with a message that
names the cause.

`register_host_kernel()` registers a `HostKernel`, which takes the views and the options and no
queue. `find()` then answers a `Kernel` that ignores its queue and calls it, so a device runtime
runs it like any other, and `find_host()` answers the host kernel itself. A worker on a host
runtime, which holds no queue, runs only host kernels.

## worker_statistics.hpp

`WorkerStatistics` holds a worker's counters since `start()`: the tasks run, the tasks failed by
cause, the cache's bytes and evictions, the registrations and the reconnects. The executor fills
the task counters, the agent the cache and link ones.
