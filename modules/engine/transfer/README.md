# transfer

The module copies bytes between two memory spaces: the `TransferPath` port, every path that
implements it, the staging ring and pinned host memory the host-staged and file paths use, the
queues a space copies on, the observer that hears each copy and the price of a copy. It is an internal module: its headers reach the modules
that name it and the tests, and are never installed. Its names are in
`SushiRuntime::Internal::Coherence`.

It depends on `handle`, `log` and `memory`.

## Public entry points

| Header | Holds |
| --- | --- |
| `transfer_path.hpp` | `TransferPath`, the interface of one way of copying, and `TransferRequest` |
| `transfer_cost.hpp` | `TransferCost` and `uniform_transfer_cost()` |
| `same_context_path.hpp` | `SameContextPath`, one memcpy between two devices of one SYCL context |
| `peer_path.hpp` | `PeerPath`, one peer copy between two contexts of one backend |
| `host_staged_path.hpp` | `HostStagedPath`, the copy through pinned host staging buffers |
| `file_path.hpp` | `FilePath`, the read of a file space into a host or device space through the staging ring |
| `staging_ring.hpp` | `StagingRing`, `StagingRingShape` and `StagingSlot`, the fixed ring of pinned slots |
| `pinned_host_memory.hpp` | `PinnedHostMemory`, the port that makes and frees pinned host memory |
| `space_queues.hpp` | `SpaceQueues`, the port that names the queue a space copies on |
| `peer_access.hpp` | `PeerAccess`, the port that grants one device the memory of another |
| `transfer_observer.hpp` | `TransferObserver` and `TransferRecord`, the report of which path each copy took |
| `transfer_observer_slot.hpp` | `TransferObserverSlot`, the observer the runtime's paths hold, which forwards to one a caller installs |
| `transfer_path_list.hpp` | `admit_path()` and `PathListStatus`, which add a path to a list unless its `TransferPathID` is taken |
| `transfer_log_category.hpp` | `K_TRANSFER_LOG_CATEGORY`, the log category of the module |

## Path identity and space kinds

Each path class declares `K_NAME` and `K_ID = TransferPathID::named(K_NAME)`, and `TransferPath::id()`
returns it. The name is for logs, reports and the profile file; selecting a path, pricing it and
recording a copy compare the id. `TransferRecord::id` carries it to the observer. A path asks a space
what it is through `MemorySpace::is_host_memory()` and `MemorySpace::is_file_backed()`, and never
compares a kind name, so a new kind of space or a new path edits no sibling.

## SameContextPath and PeerPath

Both copy with one `memcpy` on the destination's queue, which `SpaceQueues` names. `SameContextPath`
connects two spaces when the source lists the destination among its direct targets, which is how a
device space records a shared SYCL context. `PeerPath` connects two spaces the source does not list,
when `PeerAccess` grants the destination's device the source's memory. The runtime's `PeerAccess`
asks the SYCL peer-access extension on DPC++ and refuses everywhere else, so under AdaptiveCpp the
path connects nothing. Each path tells its optional `TransferObserver` the path name, the two spaces
and the byte count of every copy it starts. The runtime gives every path its `TransferObserverSlot`,
which drops each copy until a caller installs an observer through
`AdvancedControls::install_transfer_observer()`, between runs.

The runtime builds the paths once, in `engine/execution`'s `build_transfer_paths()`, and the
context hands them out as `RuntimeContext::transfer_paths()`: `SameContextPath`, `PeerPath`,
`HostStagedPath`, then `FilePath`. The planner takes the first path that connects, so each later path serves only
what the earlier ones refuse.

`admit_path()` builds the list of paths a runtime tries. It adds a path unless a listed path has the
same `TransferPathID`, then returns `DuplicateID` and logs the refused path and the listed one at
error level; the path is not listed. `admit_paths()` builds a list from candidates and returns
`InvalidState` when one was refused; `RuntimeContext::create()` returns that error, so a runtime with
two paths of one id is not created. `TransferPathTally` keys on the id, so two paths with one id
would share a count.

## Failure

`TransferPath::copy()` returns a `Result<sycl::event>`; no path throws. A path with no queue for its
destination returns `InvalidArgument`. A file that cannot be opened, cannot be sought or ends before
its range returns `InvalidState`. A ring that cannot make its slots returns `AllocationFailed`.
What a SYCL call throws is caught inside the path and returned as the error of
`Internal::SYCL::translate_exceptions()`. A path that fails after it took a ring slot retires the
slot before it returns.

## HostStagedPath and StagingRing

`HostStagedPath` connects any two spaces that have a queue, and the host space with any space that
has one. A copy between two devices is cut into chunks of one ring slot. A chunk goes from the
source into a slot on the source's queue, and from the slot to the destination on the destination's
queue; the copy out of the slot for chunk k runs while chunk k + 1 is copied into the next slot, so
the two legs overlap. The path waits on the host for each source-to-slot leg, because the two
queues may belong to two contexts and an event does not cross a context. It returns the event of
the last slot-to-destination leg; each such leg waits for the one before it.

The host space is pinned host memory, so a copy to or from it is one `memcpy` on the device's
queue, with no slot. A space `SpaceQueues` gives no queue is read as host memory in `copy()`.

`StagingRing` holds `slot_count` pinned buffers of `slot_bytes` each. The defaults,
`K_DEFAULT_STAGING_SLOT_BYTES` (4 MiB) and `K_DEFAULT_STAGING_SLOT_COUNT` (4), are in
`staging_ring.hpp` and nowhere else; the runtime builds the ring with them. The slots are made
through `PinnedHostMemory` on the first `acquire()`, never again, and freed with the ring. `acquire()`
returns `Result<StagingSlot>`: `AllocationFailed` when the memory has no room for every slot, after
it released the slots it had made. A slot
has one holder; the holder retires it with the event of its last read, and the next holder waits
for that event. The ring locks only to hand a slot out or take it back.

The path does not keep the host bytes as a valid instance: the slot is reused, and the directory
never hears of it. Keeping a host instance when a later reader needs it is a planner refinement,
not part of this path.

## FilePath

`FilePath` reads a file space into any space that is not a file. It connects a source only when the
source is a `FileMemorySpace` added to it with `add()`; the runtime adds each file space when a
buffer is made from a file range, between runs, and `remove()` lets go of it when the buffer is
dropped, so its id can name the next file space. A copy to a space with a queue cuts the range into
chunks of one ring slot, in file order: the path reads a chunk from the file into a slot and copies
the slot to the destination on the destination's queue, and the read of the next chunk runs while
that copy does. The host memory a copy holds is therefore the ring, `slot_bytes * slot_count`, and
the file stream's own buffer, far under the 512 MB the design allows for read buffering. A copy to
a space with no queue, the host, reads the file straight into the destination. Reads go through
`std::ifstream`, so the path is the same on Windows and Linux. The file must not change while a
buffer made from it lives.

`PlannedHostAccess::mint_held()` mints the buffer: its one valid instance is the file space's token,
and the coherence map names the file space as the holder of every byte. A read from the host, or a
node the planner places on a device, then copies from the file through this path.
