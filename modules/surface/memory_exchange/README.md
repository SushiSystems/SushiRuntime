# memory_exchange

The non-template entry points behind the `Interop` facade: they query a device, export host
memory and import device memory against the engine's `RuntimeContext`. The module is internal:
its header is in `include/SushiRuntime/memory_exchange/` for `api` to include, and it is not
installed.

It depends on `core`, `log` and `interop` in its headers, and on `memory`, `sycl`, `device` and
`execution` in its sources.

## Public entry points

| Header | Holds |
| --- | --- |
| `memory_exchange.hpp` | `query_capabilities`, `export_host_memory` and `import_device_memory`, in `SushiRuntime::Internal::MemoryExchange` |
| `capability_cache.hpp` | `CapabilityCache` and `probe_device_capabilities`, in `SushiRuntime::Internal::MemoryExchange` |

## memory_exchange.cpp

### Exceptions

No exception leaves the three functions. Each runs its body through `translated()`, which calls
`Internal::SYCL::translate_exceptions()` of `modules/foundation/sycl` and hands back the
body's own `Result` when nothing was thrown. The codes are:

| Function | `sycl::exception` | `std::bad_alloc`, refused device memory | any other exception |
| --- | --- | --- | --- |
| `query_capabilities` | `DeviceLost` | `AllocationFailed` | `DeviceLost` |
| `export_host_memory` | `AllocationFailed` | `AllocationFailed` | `AllocationFailed` |
| `import_device_memory` | `InvalidAccess` | `AllocationFailed` | `AllocationFailed` |

### export_host_memory

`export_host_memory` builds the `USMAllocationOwner` before it makes the allocation that owner
will free. A throw between the two, for example `bad_alloc` from `make_unique`, then never leaves
an allocation that nothing records. Zero bytes return an empty `Allocation` and allocate nothing.
The function returns `Result<Allocation>`: `export_checked()` builds the owner and makes the SYCL
allocation, and both an exception out of it and a null address become an `AllocationFailed`
error.

### import_device_memory

The function returns `Result<Allocation>`; `import_checked()` holds its steps. The checks run in
this order, and the first that fails is the error returned; `descriptor_refusal()` and
`device_refusal()` hold the checks of the descriptor and return `std::optional<Error>`:

| Check | Error |
| --- | --- |
| `capabilities(device).device_import` is true | `Unsupported` |
| `descriptor.kind` is not `Unspecified` | `InvalidAccess` |
| `descriptor.kind` is in `capabilities(device).importable_kinds`; the message names the kind | `InvalidAccess` |
| an `OpaqueWin32` handle is not null; an `OpaqueFd` handle is at most `INT_MAX` | `InvalidAccess` |
| `offset + bytes` ends inside `allocation_bytes` | `InvalidAccess` |
| `descriptor.device` equals the device's UUID | `InvalidAccess` |

File descriptor 0 is accepted, because it is a valid descriptor. Zero bytes that pass every check
return an empty `Allocation`: no import, no mapping, no registry entry.

A mapped import is adopted into the runtime's allocator as `USMKind::Device`, so a later
`Interop::borrow()` of it reads its kind from the record and asks SYCL nothing.

`device_import` is true only when the device also reports a UUID, so a device that passes the
first check can always be matched against `descriptor.device`.

### Capabilities and their cache

`probe_device_capabilities()`, in `capability_cache.cpp`, asks one `sycl::device` for host USM,
its UUID and, behind `SYCL_EXT_ONEAPI_BINDLESS_IMAGES`, `supports_importing_handle_type` once per
kind of `K_NAMED_HANDLE_KINDS`. Without that extension, as on AdaptiveCpp, no kind is importable
and `device_import` stays false. `capabilities_for()` then clears `device_import` and
`importable_kinds` when the context does not count the device as a GPU.

`query_capabilities()` and `import_device_memory()` take the `CapabilityCache` to ask through;
this module owns none. Each runtime owns one, in the surface context that `context_access`
builds, and `api` hands it in. The cache dies with the runtime's last handle, so every
`sycl::device` it holds is released while the SYCL runtime is still up.

`CapabilityCache::get()` holds a mutex across its scan and, on a device it has not seen, across
the probe, so two first queries from two threads probe once. A probe that throws keeps nothing.
The default constructor probes through `probe_device_capabilities()`; the one that takes a
`Probe` lets a test count the probes.

### release_import

Releasing an import is three steps: forget the address, unmap it, release the SYCL import object.
Each step runs in its own call of the translator, so a failed unmap still releases. A step that throws is logged
once at ERROR with its name and the device index, and nothing is rethrown, because the steps run
from a destructor and from `AllocationOwner::release`, which is `noexcept`.

## Tests

The import is tested through the surface that reaches it, in
`tests/surface/api/interop/test_interop.cpp`, and in `tests/surface/memory_exchange/`.
