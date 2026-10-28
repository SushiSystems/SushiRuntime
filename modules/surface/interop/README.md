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
| `external_handle_kind_set.hpp` | `ExternalHandleKindSet` |
| `interop_capabilities.hpp` | `InteropCapabilities` |

Every name is declared in `SushiRuntime`. The `Interop` facade of `api` takes and returns these
types; `memory_exchange` holds the functions that act on them.

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

Who closes the handle depends on its kind. Ruling 8 of SushiEngine's `SYCL_VULKAN_INTEROP`
design topic sets the rule; it follows the CUDA driver, which the
Unified Runtime CUDA adapter reaches without duplicating the handle.

| Kind | After a refused import | After a successful import |
| --- | --- | --- |
| `OpaqueWin32` | The exporter keeps the handle and closes it. | The exporter keeps the handle and closes it once the `Buffer` has died; the driver does not take it over. |
| `OpaqueFd` | The caller still owns the descriptor and closes it. | The driver owns the descriptor. Nobody closes it, and nobody uses it again. |
| `Unspecified` | Refused before SYCL sees it; the caller keeps the handle. | Never imported. |

A kind the device does not import (see `InteropCapabilities::importable_kinds`) is refused before
SYCL sees the handle, so the caller keeps it.

## external_handle_kind_set.hpp

`ExternalHandleKindSet` holds a set of `ExternalHandleKind` values as one bit per enumerator, in one
byte. `insert`, `erase` and `contains` take a kind; `is_empty` tells whether the set holds none.

## interop_capabilities.hpp

`InteropCapabilities::importable_kinds` holds the kinds `import_device` accepts on the device.
`memory_exchange` fills it by asking SYCL's `supports_importing_handle_type` once per kind, and
leaves it empty on a toolchain without the bindless-images extension, such as AdaptiveCpp, and on
a device that is not a GPU. `device_import` is true only when the set holds a kind.

## Tests

`tests/surface/interop/` builds `sr_interop_test_objects`, linked into `sr_surface_tests`.
