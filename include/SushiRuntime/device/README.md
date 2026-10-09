# device

The device inventory and the SYCL queues built on it.

## What the module owns

- The inventory: one `DeviceInfo` per device (index, name, kind, backend, NUMA node, memory,
  compute units). `DeviceQueueSet::devices()` returns it.
- Every SYCL queue of every device, in one table with four slots per device: in-order or not,
  times profiling or not.
- The routing from a NUMA node to the devices that sit on it.

The set is built once, from a `std::vector<DeviceDescriptor>` and the profiling flag, and does
not change afterwards. There is no re-probe. A reference to a queue or to the inventory stays
valid until the set is destroyed.

## What it depends on

`core` (`export.hpp`, `error.hpp`, `logger.hpp`, `common.hpp` for `MAX_NUMA_NODES`) and SYCL.
It includes no header of `topology`, `execution`, `scheduler`, `graph`, `memory` or `api`. The
caller turns whatever it discovered into descriptors; the module never asks the machine what
devices exist.

The source is `src/device/device_queue_set.cpp`.

## Public headers

| Header | Declares |
| --- | --- |
| `device/device_info.hpp` | `DeviceKind`, `DeviceInfo`, `DeviceDescriptor` |
| `device/queue_flavour.hpp` | `QueueFlavour` and its comparison |
| `device/device_queue_set.hpp` | `DeviceQueueSet` |

## Behaviour that differs from RuntimeContext

`DeviceQueueSet` replaces the queue members of `Execution::RuntimeContext`. Four things differ.

1. **`wait_all()` waits for every built queue.** `RuntimeContext::wait_all()` waited for the
   plain queues only, so work submitted to a profiling or chain queue could still be running
   when it returned.
2. **The NUMA fallback is resolved and logged at construction.** `get_numa_local_queue()`
   logged a WARN on every call whose NUMA id had no queue, which the scheduler reaches once per
   dispatched task. The set now logs one WARN at construction that lists the nodes below the
   highest populated one that hold no device, and one INFO that states the rule for every id
   outside the table. A lookup logs nothing.
3. **A bad device index throws `Core::SushiException` with `Errc::invalid_access`.** The
   message carries the index and the device count. `RuntimeContext` threw `std::runtime_error`
   with the text "Queue index out of bounds." and logged an ERROR line first; the set does not
   log, because the caller receives the failure.
4. **A device whose plain queue is in-order has two queues, not four.** See "One slot per
   flavour" below. No CPU or GPU is affected.

## device_queue_set.hpp

### The plain queue

The constructor builds one queue per device in that device's own SYCL context. A device that is
neither a CPU nor a GPU gets an in-order queue, because an FPGA or a custom accelerator may not
support out-of-order queues. When the set is built with `profiling` true, every plain
queue carries `enable_profiling`, so a profiled run pays no first-use latency.

A descriptor whose NUMA node is negative or not below `Core::MAX_NUMA_NODES` is placed on node
0, with a WARN that names the device and the value it reported. `devices()[i].numa_node` holds
the node the device was placed on. `devices()[i].index` is always `i`, whatever the descriptor
carried.

### One slot per flavour

`queue(index, flavour)` returns the queue in the slot the flavour names, and builds it on first
use in the plain queue's context. The plain queue sits in the slot of its own flavour, so
asking for that flavour returns the plain queue itself.

On a CPU or a GPU this reproduces the queues `RuntimeContext` had:

| Old call | New call | Queue |
| --- | --- | --- |
| `get_queue(i)` | `queue(i)` | plain |
| `profiling_queue(i)` | `queue(i, {false, true})` | the plain queue when the set profiles, else a second queue |
| `chain_queue(i, false)` | `queue(i, {true, false})` | in-order |
| `chain_queue(i, true)` | `queue(i, {true, true})` | in-order with profiling |

`{false, false}` on a profiling set is a slot `RuntimeContext` had no call for: an out-of-order
queue without profiling.

A device whose plain queue is in-order never gets an out-of-order queue, so `in_order = false`
resolves to the in-order slot there. On such a device `chain_queue(i, false)` used to build a
second in-order queue beside the plain one, and `chain_queue(i, true)` a second one beside the
profiling queue. Each pair is now one queue.

`has_profiling_queue(i)` is true once either profiling slot of device `i` is built. It builds
nothing.

### Thread safety of the lazy build

Each slot holds a `std::optional<sycl::queue>`, a `std::once_flag` and a `std::atomic<bool>`.
`queue(index, flavour)` builds through `std::call_once` on the slot's flag, so two threads that
ask for the same unbuilt queue get one queue, and the second waits for the first. If the build
throws, the flag stays unset and the next call tries again.

The builder sets the atomic flag after the optional is constructed. `has_profiling_queue()`,
`wait_all()` and the destructor do not go through `call_once`; they read the flag and touch the
optional only when it is set, so none of them can see a half-built queue.

The table is sized once in the constructor and never resized. That is why a returned reference
stays valid.

### NUMA routing

The constructor builds a table with one route per NUMA node, from node 0 to the highest node
that holds a device. A route is the ascending list of the device indices on that node. A node
in that range with no device, and every id outside the range (negative ids included), takes the
fallback route: all devices.

`device_for_numa(numa)` returns the first device of the route. That is the lowest-indexed
device on the node, or device 0 on the fallback route. The answer is the same on every call and
every thread.

`queue_or_numa_local(device, numa)` returns `queue(*device)` when a device is given. With no
device it makes the choice `RuntimeContext::get_numa_local_queue(numa)` made: it rotates over
the route, one step per call, with a cursor that belongs to the calling thread. A node with
devices has its own cursor. All fallback routes share one cursor per thread, so a thread that
alternates between two device-less nodes still advances one step per call. The cursors are
`thread_local` and therefore shared by every set in the process, as they were shared by every
context.

The rotation spreads tasks that name no buffer over the devices of a node. It is also the cause
of the first entry in `docs/reference/KNOWN_ISSUES.md`: a kernel that captured USM of one
device can be submitted to another. The two functions are separate so that a caller who needs a
stable device, such as an allocator, uses `device_for_numa`.

An empty set has no route to anything. Both functions then throw `Core::SushiException` with
`Errc::unsupported`.

### The destructor

The destructor calls `wait_and_throw()` on every built queue. An asynchronous device error that
surfaces there is logged as a WARN with the device index and name, and the destructor
continues; a destructor that threw would terminate the process. The owner of the set must stop
everything that submits work before it destroys the set.

### What the set does not log

A failure while a plain queue is built leaves the constructor as the exception SYCL threw. The
set does not log it. `RuntimeContext` logs "Critical initialization failure" around its whole
probe, and a second line here would duplicate it.
