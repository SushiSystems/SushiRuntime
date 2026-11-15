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
| `transfer_path_id.hpp` | `TransferPathID` |
| `handle_status.hpp` | `HandleStatus` |
| `handle_space.hpp` | `HandleSpace` and `MintOutcome` |
| `handle_resolver.hpp` | `HandleResolver` and `ResolveOutcome` |
| `handle_home_reader.hpp` | `HandleHomeReader`, `HandleHome` and `HomeMemory` |
| `device_contexts.hpp` | `DeviceContexts` |
| `element_layout.hpp` | `ElementLayout` |
| `bound_operands.hpp` | `BoundOperands`, `OperandAddress` and `OperandStatus` |
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

`TransferPathID` is a strong 64-bit value that names one way of copying between memory spaces. A
path class builds it once from its own name with `TransferPathID::named()`, a constexpr FNV-1a hash,
so the value is the same on every run and every machine, and 0 is kept for no path. Code that selects,
prices or records a path compares ids. A name is for logs and for the keys of the profile file, which
map back to an id once, when the file is read.

## handle_space.hpp and handle_resolver.hpp

`HandleSpace` mints a handle of a kind with flags and retires it. `HandleResolver` turns a handle
into the address of its instance on one memory space and says whether a handle is stale. Both are
interfaces with no data. The runtime's residency directory will implement both.

They return `MintOutcome` and `ResolveOutcome`, a value with a `HandleStatus`, and not the
repository's `Result`. `Result` lives in `foundation/core`, and this module takes no dependency on
another module.

## handle_home_reader.hpp

`HandleHomeReader` extends `HandleResolver` with `home_of()`, which says where a handle's buffer was
made and what it holds: `HandleHome` carries the status, the home space, whether that space holds an
instance, what the instance lives in (`HomeMemory`: host accessible, device only or unknown) and the
`ElementLayout` of one element. A module that must not name the residency directory's own types takes
this port: `engine/task` reads host visibility and element layout through it.

## device_contexts.hpp

`DeviceContexts` counts the devices of a runtime and says whether two of them share one memory context.
`USMAllocator` implements it, and `engine/task` takes it where it needs the device count and the shared
contexts, so its public headers name no `memory` type.

## element_layout.hpp

`ElementLayout` is the size and alignment of one element of a buffer. A layout of one byte aligned to
one byte is raw bytes (`is_bytes()`).

## bound_operands.hpp

`BoundOperands` is the one view of the buffers a node bound by handle, in the order the node named
them, each with its `ElementLayout`. `at<T>(position, byte_offset)` returns an `OperandAddress<T>`:
the address and `OperandStatus::Ok`, or a null address and the reason. It refuses a position past the
last operand (`OutOfRange`), an element type whose size or alignment differs from the buffer's
(`ElementMismatch`) and an offset that is not on an element boundary of the type (`Misaligned`). A
buffer of raw bytes is viewed as any trivially copyable type at an offset aligned for it. The runtime
knows the layout of a buffer and not the type name, so two types of one size and alignment pass the
same check. `untyped(position)` returns the table entry with no check, for a launch that holds its own
pointer types. `of_bytes()` views a table whose buffers are all raw bytes, as a host replay does.

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
