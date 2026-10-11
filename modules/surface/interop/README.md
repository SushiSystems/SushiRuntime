# interop

The descriptors a consumer exchanges with another graphics or compute API: the UUID of a device,
an exported memory handle and what a device offers a pairing. The module is header-only and its
headers are installed.

It depends on the C++ standard library and on no other module of the repository.

## Public entry points

| Header | Holds |
| --- | --- |
| `device_uuid.hpp` | `DeviceUUID` and its comparisons |
| `external_memory.hpp` | `ExternalMemoryDescriptor` and `ExternalHandleKind` |
| `interop_capabilities.hpp` | `InteropCapabilities` |

Every name is declared in `SushiRuntime`. The `Interop` facade of `api` takes and returns these
types; `interop_engine` holds the functions that act on them.

## external_memory.hpp

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

## Tests

`tests/test_interop_vocabulary.cpp` builds `sr_interop_tests`.
