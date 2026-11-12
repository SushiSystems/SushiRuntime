# Global handle

**Status:** Open — H0 to H10 planned; the specification is in `docs/agent/2026_11_02_GLOBAL_HANDLE/`
**About:** One identity for every buffer on every device and machine, resolved by the runtime, with the copies between devices planned as graph nodes.
**Modules:** `modules/platform/memory`, `modules/foundation/region`, `modules/extension/buffer_exchange`

## Problem

A buffer's identity is its address. `BufferRegion::base` is `ResourceID = void*`
(`modules/foundation/region`), and every dependency edge, boundary map, live window and placement
anchor keys on it. An address names one allocation on one device, so the runtime cannot say "this
data, wherever it is". Five consequences follow.

- A node whose buffers sit on two devices is refused (`dispatch_placement.cpp`). A job larger than
  one card cannot be split across cards without the caller writing every copy by hand.
- The distributed layer has a second identity, `Distributed::GlobalBufferID`, and a second map from
  address to id (`BufferRegistry`). The memory registry keeps the same fact a third time
  (`USMRegistry`), behind one mutex taken on every lookup.
- Boundary edges are wired in address order, so submission order changes between runs (KI-23).
- `DeviceIndex` follows enumeration order (KI-25). Discovery merges two entries only when both
  report one PCI address, and NVIDIA and AMD devices often report none, so one card can appear under
  two backends. A name that changes between runs cannot anchor a deterministic placement.
- The anchor rule is written twice, and the two copies disagree (KI-24).

SushiEngine copies the same `ResourceID = void*` into its own execution vocabulary, so the defect
crosses repositories.

## Decision

The owner approved the global handle and delegated the open choices to engineering, with
reliability and maintainability ruling over cost.

1. **The runtime plans copies.** A node may read and write buffers whose valid data lives on other
   devices. The runtime inserts the allocate, copy and free nodes the node needs, and
   LAYERED_EXECUTION's caller-recorded copy rule gives way to this one. Reason: a handle means the
   same thing everywhere only if the runtime keeps every copy of it coherent.
2. **The handle is 128 bits.** A 64-bit handle with a 20-bit generation repeats a stale handle after
   about a million reuses of one slot, which a long simulation reaches. 128 bits keep a 32-bit
   generation, a 16-bit machine field and a 32-bit universe field that refuses handles from another
   runtime instance. The cost is 8 more bytes per edge.
3. **The identity lives in a new foundation module, `modules/foundation/handle`.** It is header-only,
   with no SYCL dependency, so a consumer's own backend and the wire format can mint and carry
   handles without the runtime. Residency lives in `modules/platform/memory`; copy planning and
   partitioning live in a new `modules/engine/coherence`.
4. **`Buffer::data()` stays public.** It returns the address of the buffer's instance on its home
   device. `Buffer::handle()` is the identity.
5. **Determinism is part of the contract.** Handle ids, device keys, placement and copy plans are
   pure functions of the recorded graph, the declared costs, the device set, the profile file and
   the placement journal. A plan that differs between two runs with equal inputs is a defect. This
   holds on any mix of devices and does not wait on H8, which adds equal bytes across vendors for
   one node moved between them.
6. **The runtime owns placement.** A consumer declares costs and, for a node it can split, how to
   split it. The runtime chooses where every slice runs. A placement scope stays as an override the
   consumer may state, and the partitioner treats it as fixed.
7. **The shortest step, and never a slower one.** The partitioner minimises the time of one step
   within the measured capacity of every space. It discards a plan that is slower than the floor,
   the best plan that fills devices in order of speed over any subset of the machine's devices,
   so a device added to a machine never slows a job and may receive no work. Reason: the owner's measure of success is the fast card's speed with
   whatever a slower device can add on top. Equal shares are a result for independent work, never
   the goal.

## Design

### Prior work

No published runtime gives one handle across NVIDIA and AMD devices in one process with
deterministic copy plans. The design takes its parts from these:

| Source | Taken |
| --- | --- |
| Legion, SC12; Dynamic Tracing, SC18 | Logical buffer apart from physical instances; record the analysis once and replay it |
| StarPU, CCPE 2011 | Declared access modes as the only source of dependences and transfers; a cost model per task kind with transfer time, as its `dmda` policy keeps |
| HEFT; XKaapi, Lima et al. | List scheduling by earliest finish time over devices of unequal speed |
| Celerity IDAG, arXiv 2503.10516; Horizons | Allocation, copy and free as explicit graph nodes; horizons bound tracking state |
| IRIS, HPEC 2021 | One memory object tracked across CUDA, HIP and OpenCL in one process |
| HPX AGAS | Stable id with a mutable location; a do-not-cache flag bit |
| UPC++, IPDPS 2019 | A global handle type apart from the local pointer a kernel receives |
| Ray ownership, NSDI 2021 | One owner per handle; a lost copy is rebuilt by re-running its producer |
| ReproBLAS, EECS-2016-121; CORE-MATH; RLibm | Order-independent sums and correctly rounded functions for cross-vendor bytes |

Kokkos Remote Spaces and NVSHMEM let a kernel dereference remote memory. That model is set aside:
the target devices have no shared address space, and a remote load inside a kernel hides a
transfer from the graph.

### The handle

```
GlobalHandle (128 bits, trivially copyable, ordered, hashable)
    universe   32  runtime instance; from RuntimeConfig, assigned by the master in a cluster
    machine    16  machine id; 0 on a single machine
    kind        8  buffer, state, image, reserved
    flags       8  do-not-cache, borrowed, host-only
    generation 32  raised each time the slot is reused
    slot       32  index into the residency directory
```

- Slots are given out in recording order from an ordered free list, so the same program gets the
  same handles in every run.
- A handle never carries a location. Moving data changes the directory, never the handle.
- `GlobalHandle` cannot be dereferenced. A kernel receives a resolved local pointer, built when its
  node is dispatched.
- `HandleSpace` mints and retires handles; `HandleResolver` turns a handle into a local address on
  one memory space. Both are interfaces in `foundation/handle`. The runtime's directory implements
  both; a consumer's own backend may implement `HandleSpace` when it adopts the handle.

### Device key

`DeviceKey` names a physical device across runs: the backend, plus the hardware id the backend
reports (CUDA UUID, HIP UUID, Level Zero UUID, PCI address), plus a stable ordinal for a device with
none. One `DeviceKeySource` strategy per source computes it, which splits the 170-line
`probe_all_devices` into one unit each. Beyond the existing PCI key, discovery merges entries by
UUID and prefers a native backend over OpenCL for one card. `DeviceIndex` stays as the dense index of
this run; rules, profiles and stored plans name `DeviceKey`.

### Memory spaces

A `MemorySpace` is a place an instance can live: each device, pinned host memory per NUMA node, a
file, and later a remote machine or a CXL region. Each space reports its capacity, its measured
reserve and the spaces it can copy to directly. Host memory is a space of its own, because NVIDIA
and AMD devices cannot copy to each other. An integrated GPU whose memory is carved from system RAM
reports the shared pool, and the planner counts that pool once.

### Residency directory

One table indexed by slot replaces `USMRegistry` and `BufferRegistry`.

```
DirectoryEntry
    generation   atomic, checked against the handle
    size, element layout, home space
    instances    per memory space: address or none
    coherence    interval map: [begin, end) -> (valid spaces bitmask, version)
```

- The table grows in chunks whose addresses never move, so a reader never waits on a writer.
  Retired entries are reclaimed through the runtime's epoch-based reclamation.
- Resolve is one chunk load, one generation compare and one bitmask test.
- The interval map merges neighbouring intervals with equal state, so a buffer written whole
  stays one interval.
- Address-to-handle lookup serves borrowed memory only, through a sorted interval index.

### Coherence and copy planning

`CoherencePlanner` runs when the region composer installs a node.

1. For each read interval, find the spaces where it is valid. If the node's device is among them,
   no copy. Otherwise pick a source space and a `TransferPath`, and insert the copy nodes before the
   node.
2. For each write interval, record that after the node completes only the node's space holds the
   new version; the others become invalid.
3. Allocation of a missing instance is a node; freeing an instance no future node reads is a node.

The source space is the lowest `DeviceKey` among valid spaces whose path is cheapest in the profile
file. The profile is a frozen input, read once per run, so the choice is a pure function of it.

**Capacity.** When a space cannot hold a new instance, `EvictionPolicy`, a strategy apart from the
planner, chooses a victim. The default evicts by logical time: the instance whose next read is
furthest in the recorded graph goes first, with a write-back to host when it holds the only valid
copy. Eviction reads the graph and not the clock, so the eviction plan is the same in every run. A
job admitted as fitting has its weights pinned to their spaces, and eviction never touches them.

### Transfer paths

| Path | When | Copy |
| --- | --- | --- |
| `SameContextPath` | both spaces in one SYCL context | one `memcpy` on the destination queue |
| `PeerPath` | one backend, peer access granted | one peer copy |
| `HostStagedPath` | different vendors, or no peer access | device to pinned host, host to device, in chunks that overlap |
| `FilePath` | a file range to any space | read through the staging ring |

A path is a strategy behind `TransferPath`; a new interconnect adds a file. GeForce cards usually
refuse peer access, so two GeForce cards in one machine are expected to take the host-staged
path. The host-staged path goes through a fixed ring of pinned staging buffers. It
keeps its host copy as a valid instance only when the planner can place that instance within host
capacity and a later reader on another device needs it; otherwise the ring slot is reused.

### One home per byte

<!-- fact:begin one-home-per-byte -->
Host memory holds the CPU's own instances and the staging ring, and nothing else unless the planner
places it there. A buffer that lives on a card has no shadow copy in RAM, so the memory a job can
use is the sum of every space, less the reserve each space measures at start.
<!-- fact:end one-home-per-byte -->

A file is a read-only memory space. A buffer created from a file, such as the weights of a
model, names the file range as its first valid instance. Loading streams each range from the file
through the staging ring into its target space; the whole file is never read into RAM.

### Cost-based partitioning

`Partitioner`, on the host, shares a recorded graph across every device, the CPU included, so that
one step takes the least time within the measured capacity of every space. It knows no math and no
model: each node declares its cost as numbers, and the runtime reads only those.
[The partitioning page](PARTITIONING.md) holds the objective, the floor, the algorithm and the
device profile.

- **Declared cost.** A node states the bytes it reads, the bytes it writes, the operations it
  performs and the operation kind, through `NodeCost`. The consumer derives these from its own
  shapes; SushiBLAS derives them from tensor shapes.
- **Device profile.** `DeviceProfile` holds each device's measured capacity, bandwidth and
  throughput per operation kind, stored per `DeviceKey` in a profile file. `CostModel` turns a
  declared cost and a profile into a time. A device of any age or speed enters through its
  profile, and a device with none receives no work until it is measured.
- **Assignment.** Nodes are placed by earliest finish time, and a cut between devices is charged
  for the bytes that cross it over its transfer path. Independent nodes spread in proportion to
  device speed; a chain gives the fastest device as much as shortens the step.
  The capacity of a job is the sum of every pool less its measured reserve; an integrated GPU
  that carves its memory from RAM adds a device, not capacity. A job that does not fit is refused
  before loading, with the shortfall per pool.
- **The floor.** A plan slower than the floor is discarded, so a device may receive no node.
- **Splittable nodes.** A node may also declare a split: an axis, the slice kernel each part runs,
  and the combine node that joins the parts in slice order. The runtime decides how many parts and
  where each runs; the consumer still owns every kernel, so the runtime stays free of math.
- **Rebalancing.** At each horizon the partitioner reads a smoothed step time per device and moves
  nodes when the saving exceeds the copy cost by a margin. Each move is written to the placement
  journal, and a replay of the journal gives the same placement.

### Replay and horizons

The copy plan and the partition are part of the compiled graph. GRAPH_REPLAY replays them without
running the planner again; a replayed step costs the same as a step with no copies to plan. A
horizon node every N steps retires coherence history older than the horizon, so the directory does
not grow with run length.

### Distributed

The wire carries `GlobalHandle` in place of `BufferHandle`'s id, and the protocol moves to version 5.
A remote machine's memory is one more memory space, reached by a `NetworkPath`. Each handle has one
owner, the machine that minted it. When a worker dies, the owner rebuilds each lost instance by
re-running the node that wrote its current version; the bytes match because the plan is
deterministic.

### Units and their boundaries

| Unit | Module | Depends on |
| --- | --- | --- |
| `GlobalHandle`, `HandleSpace`, `HandleResolver`, `DeviceKey` | `foundation/handle` | nothing |
| `BufferRegion` on `GlobalHandle` | `foundation/region` | `foundation/handle` |
| `ResidencyDirectory`, `MemorySpace` interface and its spaces | `platform/memory` | `foundation/handle` |
| `DeviceKeySource` strategies | `platform/topology` | `foundation/handle` |
| `NodeCost` with read bytes, written bytes and operation kind; splittable-node declaration | `foundation/core` | nothing |
| `CoherencePlanner` | `engine/coherence` | the `ResidencyDirectory`, `MemorySpace` and `TransferPath` interfaces |
| `EvictionPolicy` strategies | `engine/coherence` | the `ResidencyDirectory` interface |
| `TransferPath` strategies | `engine/coherence` | the `MemorySpace` interface |
| `Partitioner`, `CostModel`, `DeviceProfile` | `engine/coherence` | `NodeCost`, the `MemorySpace` interface |
| Anchor rule, one copy | `engine/task` | `HandleResolver` |

### Public API

| Change | Kind |
| --- | --- |
| `GlobalHandle`, `DeviceKey`, `Buffer::handle()`, `Runtime::device_key()`, `Runtime::device_index(DeviceKey)` | added |
| `NodeCost` splits its bytes into read and written and gains an operation kind | breaking |
| The splittable-node declaration on `NodeOptions` | added |
| A buffer created from a file range | added |
| Placement rules may name a `DeviceKey` | added |
| `Runtime::partition_report()`: each device's share, its modelled and measured step time, and the floor plan's time | added |
| `BufferRegion::base` is a `GlobalHandle` | breaking |
| A node over buffers on several devices runs, with planned copies | behaviour |
| `Distributed::GlobalBufferID` folds into `GlobalHandle`; wire version 5 | breaking |
| `Buffer::data()` returns the home instance's address; `Buffer::handle()` is the identity | contract |

### Consumers

- **SushiEngine.** Its runtime adapter, `execution/backend/runtime_backend.hpp`, builds a
  `BufferRegion` from its own `ResourceID` and reads `region.base` back, so H1 breaks it. The
  adapter keeps a table from each wrapped buffer's `ResourceID` to its `GlobalHandle` and converts
  through it. Its own `ResourceID` and `hazard_core` stay; adopting the handle is later work, as it
  is for SushiAI.
- **SushiBLAS.** Tensors hold `Buffer`, so the change is mostly additive: a tensor reports its
  handle, and BLAS calls take local pointers resolved at dispatch. Its matrix products declare a
  split along rows or columns, so the partitioner can share one product across devices. Tests that
  allocate raw USM keep working as borrowed memory.
- **SushiAI.** No phase changes SushiAI or builds an inference engine. The phases build what it
  will consume: buffers created from a file range, `NodeCost`, splittable nodes and the
  partitioner. When SushiAI adopts them, it records a model as a graph whose nodes declare
  `NodeCost` and whose weights are buffers created from the model file, and it owns the file reader
  and the kernels for every device.

## Phases

| Id | Phase | State |
| --- | --- | --- |
| H0 | Stable DeviceKey from backend and hardware id; discovery deduplicates one card seen under two backends | planned |
| H1 | GlobalHandle replaces ResourceID void* in regions, edges, live IO and placement; one anchor rule | planned |
| H2 | ResidencyDirectory replaces USMRegistry and BufferRegistry; lock-free resolve by slot | planned |
| H3 | Logical buffers with per-device instances, interval coherence, and planned copy nodes | planned |
| H4 | Transfer paths: same context, same-backend peer, host-staged across vendors | planned |
| H5 | Copy plans recorded once and replayed through GRAPH_REPLAY; horizons bound directory state | planned |
| H6 | The distributed wire carries GlobalHandle; owner per handle; recovery by re-running the producer | planned |
| H7 | SushiBLAS moves to the handle and SushiEngine's runtime adapter follows it; a file-backed graph larger than any one device's memory runs across every device of the machine, the CPU included | planned |
| H8 | Cross-vendor numeric reproducibility: the same kernel gives the same bytes on every device | planned |
| H9 | The runtime places the whole nodes of any graph by declared node costs and measured device profiles so a step takes the least time within capacity, never above the floor plan's modelled time, and reports each device's share | planned |
| H10 | The partitioner splits declared-splittable nodes, rebalances at horizons through the placement journal, and returns to the floor plan when a measured step is slower than it | planned |

## Open questions

- Where a buffer lives before a plan exists. Today `Runtime::buffer()` allocates on a stated
  device, device 0 unless told otherwise, and a buffer a caller placed is fixed for the
  partitioner. A buffer the partitioner may place needs a size without a home; a buffer created
  from a file range has one, and a plain buffer does not. The owner rules on the interface before
  the partitioner's task starts.

- Whether the Ryzen 7600X's integrated GPU (gfx1036) is reachable from SYCL on Windows. The route
  is AMD's HIP SDK 6.x with a Unified Runtime HIP adapter built against it, as the CUDA adapter is;
  gfx1036 is not on AMD's Windows support list, and the DPC++ guide states that HIP on Windows is
  not supported. H0 probes it, with AdaptiveCpp as the second route. The partitioner does not wait
  on the answer: its acceptance runs on the RTX 3080 Ti and the CPU.
- Which route reaches a Radeon RX Vega 64 (gfx900). It is absent from AMD's Windows support list
  and no machine of the project holds one; when one does, the H0 probe steps are run for it.
- Whether CORE-MATH compiles as device code under SYCL. H8 is research until it does.
