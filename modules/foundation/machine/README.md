# machine

What a runtime is asked to run on and what it reports back about it: the device filter and the
pin policy a caller sets, the device descriptions and the worker pool a runtime returns. The
module is header-only and its headers are installed.

It depends on the C++ standard library and on the SYCL headers, for the `sycl::backend` a device
description names. It depends on no other module of the repository.

## Public entry points

| Header | Holds |
| --- | --- |
| `device_filter.hpp` | `DeviceFilter`, the device classes and features a runtime accepts |
| `device_kind.hpp` | `DeviceKind`, the class of a compute device |
| `device_description.hpp` | `DeviceDescription`, the inventory record of one device |
| `pin_policy.hpp` | `PinPolicy`, the choice between bound and floating workers |
| `worker_pool.hpp` | `WorkerPool`, the worker pool as the scheduler built it |

Every name is declared in `SushiRuntime`. `topology` reads the filter, `device` fills the
descriptions, `scheduler` reads the policy and fills the pool, and `api` passes all of them to and
from the caller, so the module sits in the foundation tier, below the four.

The module has no source and no test program: its headers define plain records, and the header
check of the build compiles each one alone.

## device_filter.hpp

### require_fp64

A kernel using `double` on a device without `sycl::aspect::fp64` fails at launch with a
driver-shaped error that names neither the device nor the reason. Setting require_fp64 moves the
failure to discovery, where the offending device is simply not selected and the choice is logged. It
is off by default: a single-precision workload has no reason to lose a device over it.

## pin_policy.hpp

### PinPolicy

Pinning is what lets a host application reserve cores: bound workers cannot drift onto the core the
render or audio thread is using. Float gives that up in exchange for letting the OS scheduler move a
worker off a core that a higher-priority thread has taken over. That is the better trade on an
oversubscribed machine or inside a container whose CPU allowance is enforced by quota and not by
affinity.

## worker_pool.hpp

### WorkerPool

The settings are what the caller asked for; WorkerPool is what the runtime could give. They
diverge when the requested core range does not fit the machine, or when the count exceeds
K_MAX_WORKER_THREADS. A host that reserved cores should verify against WorkerPool and not
against its own request, because a reservation is only worth something if it actually happened.
