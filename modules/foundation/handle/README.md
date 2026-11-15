# handle

The module holds the identity of a buffer, state or image across devices and machines, and the
name of a physical device across runs. It is header-only, its headers are installed, and it
depends on the C++ standard library alone: no SYCL header and no other module of the repository.
A consumer's own backend and the wire format can mint and carry handles without the runtime.

The design is in `docs/design/GLOBAL_HANDLE/README.md`.

## Public entry points

| Header | Holds |
| --- | --- |
| `global_handle.hpp` | `GlobalHandle`, `HandleKind`, `HandleFlags` and `has_flags()` |
| `device_key.hpp` | `DeviceKey`, `DeviceBackend`, `HardwareIDKind`, `to_text()` and `parse_device_key()` |
| `memory_space_id.hpp` | `MemorySpaceID` |
| `handle_status.hpp` | `HandleStatus` |
| `handle_space.hpp` | `HandleSpace` and `MintOutcome` |
| `handle_resolver.hpp` | `HandleResolver` and `ResolveOutcome` |
| `local_handle_space.hpp` | `LocalHandleSpace` and `K_LOCAL_HANDLE_UNIVERSE` |

Every name is declared in `SushiRuntime`.

## global_handle.hpp

`GlobalHandle` is 16 bytes, trivially copyable, ordered and hashable. It names data and never
says where the data is, and it cannot be dereferenced.

| Field | Bits | Meaning |
| --- | --- | --- |
| universe | 32 | the runtime instance that minted it |
| machine | 16 | the machine; 0 on a single machine |
| kind | 8 | `Buffer`, `State`, `Image` or `Reserved` |
| flags | 8 | `DoNotCache`, `Borrowed`, `HostOnly` |
| generation | 32 | raised each time the slot is reused |
| slot | 32 | the index into the residency directory |

`to_high_word()` packs universe, machine, kind and flags into one 64-bit word, high bits first, and
`to_low_word()` packs generation and slot. `from_words()` rebuilds the handle from the two.

Generation 0 is never minted, so the default value is the null handle and `is_null()` tests the
generation alone.

Handles compare field by field in this fixed order: universe, machine, slot, generation, kind,
flags. Sorting handles therefore groups them by runtime and machine and then follows slot order,
which is recording order. The hash mixes the two words with the splitmix64 finaliser, so it gives
the same value on every platform and in every run.

## device_key.hpp

`DeviceKey` names one physical device with the backend that reports it, the 16-byte hardware id
the backend gives (a UUID, or a PCI address with the domain in bytes 0 to 3 followed by bus,
device and function), and an ordinal for a device with no id. Keys order by backend, id kind, id
and ordinal, and hash with 64-bit FNV-1a over those fields.

The text form is `backend:kind:id:ordinal`, the id written as 32 lower-case hexadecimal digits:

```
cuda:uuid:00112233445566778899aabbccddeeff:0
```

`parse_device_key()` reads that form back and returns nothing for any other text.

## memory_space_id.hpp and handle_status.hpp

`MemorySpaceID` is a strong 32-bit value that names one memory space. Its meaning belongs to the
runtime that hands it out. `HandleStatus` names what happened to a mint, retire or resolve.

## handle_space.hpp and handle_resolver.hpp

`HandleSpace` mints a handle of a kind with flags and retires it. `HandleResolver` turns a handle
into the address of its instance on one memory space and says whether a handle is stale. Both are
interfaces with no data. The runtime's residency directory will implement both.

They return `MintOutcome` and `ResolveOutcome`, a value with a `HandleStatus`, and not the
repository's `Result`. `Result` lives in `foundation/core`, and this module takes no dependency on
another module.

## local_handle_space.hpp

`LocalHandleSpace` mints handles with no runtime, for host memory that only its owner uses, such as
the host-only buffers of a BLAS library. `mint(kind)` takes no lock: one atomic counter gives the
slots, from 0 in mint order, so the same sequence of mints gives the same handles.

Every handle carries `K_LOCAL_HANDLE_UNIVERSE` (`0xFFFFFFFF`), generation 1, and the flags
`HostOnly` and `Borrowed`. The space's id, given by the caller at construction, sits in the machine
field; an owner with several spaces gives each a different id, or their handles collide. A handle is
never retired. After 2^32 mints `mint()` returns `SpaceExhausted`.

No runtime uses the reserved universe: `ResidencyDirectory` refuses to be built in it. A runtime
therefore answers a local handle with `ForeignUniverse` and never resolves it. The space is not a
`HandleSpace`, because it has nothing to retire.

## Tests

`tests/foundation/handle/` builds `sr_handle_test_objects`, linked into `sr_foundation_tests`.
