# interop_engine

The non-template entry points behind the `Interop` facade: they query a device, export host
memory and import device memory against the engine's `RuntimeContext`. The module is internal:
its header is in `include/SushiRuntime/interop_engine/` for `api` to include, and it is not
installed.
The module is internal: its headers are for the other modules of the library and are not installed.

It depends on `core`, `memory` and `interop` in its header, and on `logging`, `device` and
`execution` in its source.

## Public entry points

| Header | Holds |
| --- | --- |
| `interop_engine.hpp` | `Detail::query_capabilities`, `Detail::export_host_memory` and `Detail::import_device_memory` |

## interop_engine.cpp

### export_host_memory

`export_host_memory` builds the `USMAllocationOwner` before it makes the allocation that owner
will free. A throw between the two, for example `bad_alloc` from `make_unique`, then never leaves
an allocation that nothing records. Zero bytes return an empty `Allocation` and allocate nothing.
The function returns `Result<Allocation>`: its `try` covers the owner and the SYCL allocation, and
both a `sycl::exception` and a null address become an `AllocationFailed` error.

### import_device_memory

The function returns `Result<Allocation>`. The checks run in this order, and the first that fails
is the error returned; `descriptor_refusal()` and `device_refusal()` hold the checks of the
descriptor and return `std::optional<Error>`:

| Check | Error |
| --- | --- |
| `capabilities(device).device_import` is true | `Unsupported` |
| `descriptor.kind` is not `Unspecified` | `InvalidAccess` |
| `descriptor.kind` is the platform's kind: `OpaqueWin32` on Windows, `OpaqueFd` elsewhere | `InvalidAccess` |
| an `OpaqueWin32` handle is not null; an `OpaqueFd` handle is at most `INT_MAX` | `InvalidAccess` |
| `offset + bytes` ends inside `allocation_bytes` | `InvalidAccess` |
| `descriptor.device` equals the device's UUID | `InvalidAccess` |

File descriptor 0 is accepted, because it is a valid descriptor. Zero bytes that pass every check
return an empty `Allocation`: no import, no mapping, no registry entry.

`device_import` is true only when the device also reports a UUID, so a device that passes the
first check can always be matched against `descriptor.device`.

### release_import

Releasing an import is three steps: forget the address, unmap it, release the SYCL import object.
Each step runs in its own `try`, so a failed unmap still releases. A step that throws is logged
once at ERROR with its name and the device index, and nothing is rethrown, because the steps run
from a destructor and from `AllocationOwner::release`, which is `noexcept`.

## Tests

`tests/test_interop_import.cpp` builds `sr_interop_engine_tests`.
