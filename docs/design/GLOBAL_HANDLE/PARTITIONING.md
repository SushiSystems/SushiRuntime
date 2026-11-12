# Partitioning

How the runtime shares a recorded graph across devices of different speeds. The ruling is decision
7 of [the topic](README.md); this page holds the shape. Phases H9 and H10 build it.

## What the partitioner sees

The runtime runs tasks and does not know what they compute. The partitioner reads four things and
nothing else:

| Input | Comes from |
| --- | --- |
| The cost of each node: bytes read, bytes written, operations, operation kind | `NodeCost`, declared by the consumer |
| The size of each buffer | Its declared size. A consumer whose data grows declares its upper bound, so a plan admitted at compile holds for the whole run |
| The edges of the graph | The dependency tracker |
| The speed and capacity of each device, and the cost of each transfer path | `DeviceProfile`, per `DeviceKey` |

A layer, a token, a batch and a model are words of the consumer. SushiAI records them as nodes and
buffers, and the partitioner treats that graph as it treats any other.

An operation kind is a value built from a name, as an `OperationID` is. The consumer names its
kinds; the runtime holds no list of them.

## What the partitioner may place

A buffer or a state created with `Residency::Planned` has a size and no home. It allocates nothing
when it is created. Its first instance is allocated where the plan puts it; a `write_range()`
before any plan gives it an instance in host memory, which the plan then treats as its one valid
copy. A buffer created from a file range is planned in the same way, with the file range as its
first instance. Creating a planned buffer with a stated device is refused with
`ErrorCode::InvalidArgument`.

The host cannot index a planned buffer: `is_host_accessible()` is false, and `read_range()` and
`write_range()` reach a valid instance through the directory. A kernel reaches it through the
pointer resolved at its dispatch.

A buffer of any other residency stays on the device it was given. A node that writes one is fixed
to that device, as a node the consumer placed is, and the planner copies such a buffer only to
serve a reader elsewhere. A graph with no planned buffer and no file-backed one therefore runs
where it runs today.

## The objective

The partitioner minimises the modelled time of one step, with every instance inside the measured
capacity of its pool.

- **A step** is one run of the compiled graph in its steady state, the run GRAPH_REPLAY replays.
  Loading a buffer from a file happens before the first step and is not part of it.
- **The time of a step** is the finish time of its last node when each device runs its nodes one
  after another. An edge that crosses two spaces delays its reader by the bytes that cross over the
  bandwidth of the path, plus the path's latency. A copy occupies no device's timeline in the
  model; what that leaves out reaches the plan through the measured throughput.
- **The time of a node** on a device is its operations over the device's throughput for its kind,
  plus its read and written bytes over the device's memory bandwidth.

Two shapes follow from the one objective, and neither is a rule of its own:

- **Independent nodes** go to devices in proportion to their speed, so the devices finish together.
- **A chain**, where each node waits on the one before it, takes the sum of its links. The fastest
  device takes as much of it as shortens the step, and the rest goes to the next fastest.

A device may receive no node. A slow device whose share would cost more in copies than it saves in
compute stays idle, and an idle device is a correct plan.

## Capacity

Capacity is counted per memory pool. A space draws from one pool, and spaces that share memory
share a pool: the CPU's space, the staging ring and an integrated GPU that carves its memory from
RAM all draw from system memory.

A plan fits when, in every pool, the peak of the live instances over the step stays under the
pool's capacity less its measured reserve. An instance is live from the node that allocates it to
the node that frees it, and a buffer carried from one step to the next is live for the whole step.
Admission counts no eviction: a plan that fits only by evicting is not admitted.

A job that fits in no plan is refused before loading, with the shortfall per pool. In H9 a node
whose instances exceed every pool is refused. In H10 a declared split is also taken when it is the
only way the node fits.

## The floor

Adding a device must never make a job slower. The partitioner holds its plan against the floor,
which is built from fill plans.

A fill plan over a set of devices orders them by the modelled time each would take to run every
node of the job alone, shortest first, equal devices by `DeviceKey`. It walks the nodes in
recording order and places each on the current device; when the current device's pool cannot hold
the next node's instances, the walk moves to the next device and never returns. A node the
consumer placed stays where it was placed.

The floor is the fill plan with the least modelled step time among the fill plans of every subset
of the devices that the job fits on. A machine holds few devices, so the subsets are few. Every
plan open to a smaller machine is open to a larger one, so adding a device cannot raise the floor.

The partitioner's plan is discarded when its modelled step time is above the floor's, and the
floor is taken. H9 holds this in the model. H10 holds it in measurement as well; see "Rebalancing".

## The algorithm

Sharing a graph over devices of unequal speed with transfer costs is the heterogeneous scheduling
problem, and no exact answer is affordable at compile time. The partitioner uses list scheduling
in the manner of HEFT, which StarPU's `dmda` policy and the XKaapi comparison both use as their
cost-model scheduler:

1. **Rank.** Each node gets an upward rank: its mean time over the devices plus the largest rank
   among its successors with the mean transfer between them. Nodes are taken in descending rank;
   equal ranks in recording order.
2. **Place.** Each node goes to the device that finishes it earliest, counting when the device
   becomes free, the delay of the edges that cross, and whether the pool holds its instances. Equal
   finish times resolve by `DeviceKey`. A node the consumer placed is not moved.
3. **Refine.** A node with an edge to a node on another device is moved to that device when the
   modelled step time falls and the plan still fits. The moves are tried in recording order until
   none helps.
4. **Check.** The result is held against the floor.

The plan is a pure function of the graph, the declared costs, the device set and the profile file.
With the same profile file it hashes equal across runs and across worker counts.

## The device profile

`DeviceProfile` holds, per `DeviceKey`: the capacity and reserve of the device's pool, the
bandwidth and latency of each transfer path that leaves it, its memory bandwidth, a base
throughput, and a throughput per operation kind. `CostModel` turns a `NodeCost` and a profile into
a time by the formula above.

- **First start on a device.** The runtime measures the paths, the memory bandwidth and the base
  throughput with its own copy and arithmetic kernels, and writes them to the profile file. A kind
  with no entry of its own takes the base throughput.
- **Every run.** The profiling ledger holds the device time of each dispatch. At a horizon the
  runtime divides that time by the operations the node declared and folds the result into the
  entry of its kind. The file a run reads is frozen for that run; what the run measured is written
  when it ends, and is a new input to the next one.
- **A device with no profile receives no node.** It is measured first. StarPU falls back to an
  eager policy for an unmodelled task; that fallback is set aside here because it breaks the floor.

The profile is what admits an old or a slow device. A Radeon RX Vega 64 beside an RTX 3080 Ti, an
integrated GPU and the CPU are four profiles to the partitioner, and no code names any of them.
The CPU is the first slow device the acceptance runs on, since every machine has one.

## Splitting

A node that declares a split (an axis, a slice kernel and a combine node) may be cut into parts,
at most one per device, sized by device speed. The partitioner splits when the modelled step is
shorter with the parts and their copies than with the node whole, or when the node fits no pool
whole. The floor of a plan with splits is the floor of the same graph with its nodes whole.

## Rebalancing

At each horizon the partitioner reads the median step time of the horizon and the median time per
device. The margin is one setting, 10% unless the consumer states another.

- **A slower device.** When a device's measured time is above its modelled time by more than the
  margin, its profile entries are corrected from the ledger and the plan is made again. A device
  that slows down, by heat or by another tenant, loses nodes this way.
- **The measured floor.** When the plan in force is not the floor and its measured step time is
  above the floor's modelled time by more than the margin, the floor runs for the next horizon and
  the faster of the two measured plans is kept. The journal keeps the pair and its result, and the
  same pair is not tried again until the profile changes.

A change of plan happens only at a horizon. It makes the partition and the copy plan again, which
is the one step that runs planner code, and GRAPH_REPLAY records the new plan. Each change is an
entry of the placement journal. A switch depends on measured times, so two live runs may switch
differently; a replay of the journal takes the same switches and gives the same bytes.

## The partition report

`Runtime::partition_report()` returns, for the plan in force, one row per device: the nodes and
bytes it holds, its modelled and its measured time per step, and the path each of its copies took.
For the whole plan it gives the modelled and measured step time, the modelled step time of the
floor, and the devices the floor uses. A device with no node appears with zeros. The report is how
a person sees what a second card added.

## Set aside

- **Equal time per device as the goal.** It is the answer for independent nodes and the wrong one
  for a chain, where it moves links onto the slow device to keep it busy.
- **Work stealing between devices.** The XKaapi comparison finds it competitive only with locality
  annotations, and a steal decided by timing gives a placement that differs between runs.
- **A floor from the fastest device alone.** It says nothing when the job fits on no one device,
  and a new device with a poor transfer path could change it for the worse.
- **Eviction as part of admission.** A plan that fits only by evicting pays copies the model would
  have to predict step by step; such a job is refused, or split.
