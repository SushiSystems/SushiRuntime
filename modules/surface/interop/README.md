# interop

The non-template entry points behind the `Interop` facade. The headers are in
`include/SushiRuntime/interop/`.

## interop.cpp

### export_host_memory

`export_host_memory` builds the `USMAllocationOwner` before it makes the allocation that owner
will free. A throw between the two, for example `bad_alloc` from `make_unique`, then never leaves
an allocation that nothing records. Zero bytes return an empty `Allocation` and allocate nothing.

### import_device_memory

The checks run in this order, and the first that fails throws:

| Check | Error |
| --- | --- |
| `capabilities(device).device_import` is true | `unsupported` |
| `descriptor.kind` is not `Unspecified` | `invalid_access` |
| `descriptor.kind` is the platform's kind: `OpaqueWin32` on Windows, `OpaqueFd` elsewhere | `invalid_access` |
| an `OpaqueWin32` handle is not null; an `OpaqueFd` handle is at most `INT_MAX` | `invalid_access` |
| `offset + bytes` ends inside `allocation_bytes` | `invalid_access` |
| `descriptor.device` equals the device's UUID | `invalid_access` |

File descriptor 0 is accepted, because it is a valid descriptor. Zero bytes that pass every check
return an empty `Allocation`: no import, no mapping, no registry entry.

`device_import` is true only when the device also reports a UUID, so a device that passes the
first check can always be matched against `descriptor.device`.

### Who closes descriptor.handle

SushiRuntime never closes and never duplicates `descriptor.handle`. That holds for both kinds and
at every point:

| Point | What SushiRuntime does with the handle |
| --- | --- |
| A check above refuses the descriptor | Nothing; no SYCL call has seen it. |
| `import_external_memory` throws | Nothing more; the handle was passed to SYCL once. |
| `map_external_linear_memory` or the registry throws | Calls `release_external_memory` on the SYCL import object, nothing on the handle. |
| The import succeeds | Keeps the SYCL import object, not the handle. |
| The `Buffer` dies | Forgets the address, unmaps it, calls `release_external_memory`. |

The caller therefore keeps ownership of the handle in every row and closes it.

Not established: whether the SYCL backend takes the handle over inside `import_external_memory`,
or closes it in `release_external_memory`. The toolchain's `sycl/ext/oneapi/bindless_images.hpp`
and `bindless_images_interop.hpp` say nothing about it for `resource_fd` or
`resource_win32_handle`, and `docs/design/SYCL_VULKAN_INTEROP.md` in SushiEngine says only that an
import is "unmapped and released, never freed". Until the backend's rule is read from its own
documentation, when the caller may close the handle (straight after the import, or only after
the `Buffer` dies) is open for both kinds, and for `OpaqueFd` so is whether a close after a
successful import closes a descriptor the driver already owns.

### release_import

Releasing an import is three steps: forget the address, unmap it, release the SYCL import object.
Each step runs in its own `try`, so a failed unmap still releases. A step that throws is logged
once at ERROR with its name and the device index, and nothing is rethrown, because the steps run
from a destructor and from `AllocationOwner::release`, which is `noexcept`.
