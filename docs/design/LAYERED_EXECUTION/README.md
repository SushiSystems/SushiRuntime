# Layered execution

**Status:** Open — designed and ruled on 2026-10-19; nothing built
**About:** What SushiRuntime adds so that SushiBLAS, SushiAI and SushiEngine can say where their work runs without writing it into their code: a placement vocabulary on every node, a description of...
**Modules:** modules/engine/scheduler, modules/engine/task

## Problem

What SushiRuntime adds so that SushiBLAS, SushiAI and SushiEngine can say where their work runs
without writing it into their code: a placement vocabulary on every node, a description of the
machines and devices, placement rules a person writes, and the needs of a training run over
several machines.

Each repository of the stack holds its own share of this work in a document of this name:

| Repository | Its share |
| --- | --- |
| SushiRuntime | This document: placement, topology, rules, the distributed layer's needs |
| SushiBLAS | `docs/design/LAYERED_EXECUTION/README.md`: an engine without its own graph, the per-index seam, two general operations, the cluster module |
| SushiAI | `docs/design/LAYERED_EXECUTION/README.md`: data-parallel training over five machines; it also holds the programme's backlog entry |
| SushiEngine | `docs/design/LAYERED_EXECUTION/README.md`: two lanes behind one contract (SushiRuntime with SushiBLAS, or the engine's own native runtime and BLAS), placement scopes, physics islands on several machines in the runtime lane |

The runtime today, read from the source on 2026-10-19 and 2026-10-20:

Read from the source on 2026-10-19 and 2026-10-20.

### One machine

A tracked node (`kernel()`, `task()`) runs on the device that owns the buffer of
its first write region, else of its first read region; a node that names no buffer runs on device
0 (`modules/engine/task/source/node_placement.cpp`, `placement_of()`; `K_DEFAULT_PLACEMENT = 0`).
A node whose buffers span two devices is refused with "its buffers span multiple device contexts"
(`modules/engine/scheduler/source/dispatch_placement.cpp`). Only the untracked verbs take an `On`,
whose default is `On(DeviceIndex{0})` (`api/graph/recorder.hpp`). `Runtime::buffer<T>(count,
device = DeviceIndex{0}, residency = Residency::Shared, alignment = 0)` places a buffer
(`api/runtime/runtime.hpp`). So on one machine the device of a tracked node already follows its
data, and the device of the data is written at each allocation.

### `NodeOptions`

holds `width`, `name`, `operation` and up to 8 `parameters`
(`api/graph/node_options.hpp`). It has no placement field. `TaskMetadata` has an internal
`priority` and a `numa_affinity` that graph placement does not read.

### Several machines

A node leaves the machine when it names an operation an alive worker
registered and every buffer it touches is shared. `ClusterOptions::policy` takes an
`OffloadPolicy` whose `pick_worker(const NodeOptions&, span<const WorkerLoad>, est_bytes)` returns
a worker id or nothing; `WorkerLoad` holds `worker_id`, `alive`, `inflight`, `free_mem` and
`rtt_micros` (`cluster_load/offload_policy.hpp`). `HELLO` carries a nonce, a build id and a node
name; `TASK_DISPATCH` carries the task id, the operation, the name, the parameters and the buffer
handles (`wire/wire_messages.hpp`). The protocol is version 3 (`wire/wire_frame.hpp`). A worker
runs every kernel on device 0, one at a time (KI-6, `worker_agent.cpp`, `K_DEVICE_INDEX = 0`).
Nothing in the layer is measured (RW-21, RW-22).

## Decision

### Roles

| Layer | Knows | Decides | Never |
| --- | --- | --- | --- |
| SushiRuntime | Machines, devices, memory, links, load | Where a node runs, by the rules it was given, else by its default | What a node means |
| SushiBLAS | What an operation is, its operands and their size | Nothing about place | Sees a machine's name |
| SushiAI, SushiEngine | Their own field: a replica, a physics island | The placement scope their work records under, and the default rules their configuration implies | Touch a transport |
| The person running it | The hardware and the priorities | The rules | |

The runtime is an orchestration layer and computes nothing of its own
([the architecture, section 1](../../architecture/ARCHITECTURE.md)). That stays true: everything
below is placement and transport.

### The owner's rulings

Made on 2026-10-19.

Made on 2026-10-19.

| # | Subject | Ruling |
| --- | --- | --- |
| 1 | Rule format | The runtime offers the rule model and an API that builds it. It reads no rule file; each application reads its own format and builds the rules through the API |
| 2 | Matching | A rule matches a node by placement class, class prefix, placement key and operation name. No regular expressions |
| 3 | Conflicts | The rule with the larger `priority` wins; between equals, the one added first |
| 4 | Absent target | A rule says what happens when its target is absent: `local`, `wait` with a bound, or `fail`. The default is `local` |
| 5 | Changing rules | A rule set is given between two runs and takes effect at the next compile of a graph |
| 6 | Topology | A worker reports its name, its devices and its operating system by itself; a person adds labels with `--label key=value` |
| 7 | Nested scopes | The inner scope wins |
| 8 | Protocol | The wire moves to version 4 |
| 9 | Local devices | Rules choose among the devices of one machine as well, from the first phase |
| 10 | Order | This document's work comes first; SushiBLAS follows, then SushiAI. SushiEngine moves in parallel |

### Out of scope

- Moving data between two devices or two machines on the caller's behalf. A copy is a node the
  caller records.
- A rule file format. Each application reads its own.
- Encryption of frames after the handshake (RW-18). Rules change nothing in the trust model.

## Design

### Placement class and key

A node carries two more facts in `NodeOptions`: a placement class, built from a dotted name
such as `sushiai.replica` or `sushiengine.physics`; and a placement key, a
`std::optional<std::uint64_t>` such as a replica or an island index, where 0 is a key like any
other. Both default to none.

`PlacementClass` is a value type built from the name at compile time, as an `OperationID` is.
It holds the 32-bit FNV-1a hash of the whole name and the hash of each prefix that ends before a
dot, at most `K_MAX_PREFIX_DEPTH` (4) of them: `sushiengine.matter.water` holds the hashes of
`sushiengine.matter.water`, `sushiengine` and `sushiengine.matter`. A rule for a class compares
the whole-name hash. A rule for the prefix `sushiengine.*` looks for the hash of `sushiengine`
among the prefixes, so it matches every class below `sushiengine` and not `sushiengine` itself.
The class stores no name, so it is trivially copyable and has no owner whose life a plan
depends on. A rule prefix with more than `K_MAX_PREFIX_DEPTH` segments is refused by the checks
of `PlacementRules`.

Set aside, on 2026-10-24: keeping the name beside its hash. It lets a prefix of any depth match,
but every node or every scope then owns a string that must outlive each plan built from it, for
a match the compile makes once per node.

The operation says what a node computes and comes from the library that records it. The class
and the key say which group it belongs to and come from the caller that owns the graph. The same
`gemm` is `sushiai.replica` inside a training step and `sushiengine.physics` inside a physics
tick.

### Scopes

```cpp
SushiRuntime::PlacementScope scope = runtime.placement_scope("sushiengine.physics", island);
auto positions = scope.buffer<float>(count);            // the device the rules give the scope
recorder.within(scope, [&](SushiRuntime::Recorder& r)   // every node recorded here carries it
{
    // physics recipes and SushiBLAS operations alike
});
```

`Runtime::placement_scope()` returns a value that names a class and a key and resolves, through
the rules in force, to a target. A buffer allocated through the scope lands on the scope's local
device. `Recorder::within()` stamps the scope's class and key on every node recorded inside the
callable that has none of its own. Scopes nest, and the inner one wins. Nothing is held in a
thread-local: the scope is passed, never ambient.

An explicit `DeviceIndex` or `On` always wins over a scope. A node recorded outside every scope
behaves as it does today.

### Topology

`NodeFacts` describes one machine: its name, its operating system, its labels, and one
`DeviceFacts` per device (kind, name, memory in bytes). `Runtime::topology()` returns the local
machine's facts, built once when the runtime is created.

The records live in the public module `machine`, beside `DeviceDescription` and `DeviceKind`,
since `api`, `wire` and `cluster_load` all read them and `machine` sits below the three.
`DeviceFacts` is not `DeviceDescription`: it leaves out the SYCL backend and the NUMA node, which
mean nothing on another machine, so it can cross the wire between two builds.
`OperatingSystem` is a closed enumeration (`Unknown`, `Linux`, `Windows`).

A worker sends its own facts in `HELLO`, and the master keeps one per worker, so a
policy sees names and labels that survive a reconnect, where a `PeerID` does not.

Labels are `key=value` pairs. A worker takes them from `--label` (repeatable) and from
`SR_WORKER_LABELS`; a master takes them from its `RuntimeConfig`, which also names the machine
(the host name when left empty). A label whose key is empty, holds `=` or repeats an earlier
key is refused by `Runtime::create()` with `ErrorCode::InvalidArgument`, the code the checks of
`PlacementRules` return too.

### Rules

```
PlacementRule
    match      class | class prefix ("sushiengine.*"), key (optional), operation name (optional)
    target     machine: a node name or a label; device: an index, a kind, or a least memory
    priority   integer, larger wins; equals resolve by insertion order
    if_absent  local | wait(bound) | fail
    deadline   optional; overrides the cluster's task_deadline for the nodes it places
```

`PlacementRules` builds a rule set and checks it: a rule with no match, a prefix without its
`.*`, a negative bound or a target with neither machine nor device is refused with a `Result`
error, since rules come from files the runtime does not read. `Runtime::set_placement_rules()`
takes a set between two runs and is refused while a run is in flight; the next compile of a
graph reads it. The compile resolves each node's class, key and operation to a target once, so a
replay reads no rule.

The rule model is the public module `placement` (foundation tier, after `core` and `machine`),
so that `api` resolves rules at compile time and the distributed layer picks workers by the
same code. A rule is plain data a caller fills from its own format:

| Part | Fields | Refused when |
| --- | --- | --- |
| Match | `placement_class` (`a.b` exact, `a.b.*` prefix, empty for any), `key`, `operation` (a name, empty for any) | all three empty; an empty segment; a `*` other than a trailing `.*`; a prefix deeper than `K_MAX_PREFIX_DEPTH` |
| Target | `machine_name` or `machine_label`; `device_index`, `device_kind` or `device_min_memory` | no machine and no device; two machine fields; two device fields; a label key empty or holding `=` |
| Policy | `priority`, `if_absent` (`Local`, `Wait` with a `bound`, `Fail`), `deadline`, `name` | a `Wait` bound or a deadline that is not positive |

`PlacementRules::add()` checks a rule, keeps it and its hashes, and returns
`ErrorCode::InvalidArgument` naming the rule. `resolve(class, key, operation)` returns the
position of the winning rule or none. A rule with a key matches only a node with that key. The
device index is a `std::size_t`, since `DeviceIndex` is declared in `api`, above `placement`.

With no rule set, or for a node no rule matches, the runtime does what it does today: the data's
device on one machine, `LeastLoadedOffloadPolicy` across machines.

### Local devices

On one machine the rules decide where data lives, and the existing rule that a tracked node runs
where its first write lives does the rest. A scope's buffer lands on the device its rule names.
An untracked verb takes `On(scope)`. A node whose buffers sit on two devices is refused as today:
moving data between two devices is a node the caller records, never a copy the runtime inserts.

A rule whose device is absent on this machine, such as device 1 on a one-device machine, follows
its `if_absent`.

A scope resolves its device when it is asked, by `PlacementScope::buffer()` or by `On(scope)`,
against the rules in force at that moment, so a scope opened before `set_placement_rules()`
follows the new set. The winning rule's machine part is matched against `Runtime::topology()`: a
machine name or label that is not this machine's makes the target absent here. Its device part
picks the device of that index, the first device of that kind, or the first with at least that
much memory. A scope no rule matches, or a rule with no device part, gives device 0, as today.
When the target is absent, `Local` gives device 0, `Fail` returns `ErrorCode::InvalidArgument`
naming the rule and the device, and `Wait` gives device 0 after its bound has passed: a local
device never appears during a run, and the bound is the rule writer's to keep short.

### Several machines

`RulePlacement` replaces the decision of the `OffloadPolicy` when a rule set is in force. The
decision a policy returns grows from "a worker or nothing" to one of: a worker and a device on
it, local, wait, or fail. `wait` holds the node until the target is alive or the bound passes,
then applies `local`; `fail` ends the run with a `Status` that names the rule and the target.
`TASK_DISPATCH` carries the device the decision named, and the worker runs the kernel through its
scheduler on that device, which closes KI-6.

### What a training run over several machines needs

From SushiAI's `docs/design/LAYERED_EXECUTION/README.md`, where a replica of a model is one registered
kernel that runs a whole compiled step:

- a registered kernel may run a compiled graph on the worker's runtime from the exec thread, and
  its local fallback may do the same inside a run of the master's graph; if a graph cannot run
  inside a task of another run, SushiAI runs its master replica on a second `Runtime`;
- a shared buffer that a task on the master writes gets a new version, so a worker's cache does
  not serve stale parameters;
- a rule's `deadline`, so a replica of a large model is not taken back while it works.

### Wire version 4

| Message | Gains |
| --- | --- |
| `HELLO` | the operating system, the device list (kind, name, memory), the labels |
| `TASK_DISPATCH` | the target device index |

The header and the first three fields of `REJECT` and `GOODBYE` stay frozen, so a version 3 peer
is refused in words. The golden bytes of the two messages and the version table of the module
README change in the same commit.

## Phases

Hierarchy first: the vocabulary, then what records it, then what decides on it, then the wire.
Each task has one acceptance criterion and one build. "Waits on" names tasks of this document
unless a repository is written. The guide pages and module READMEs a task makes false change in
the task's own commit.

| Id | Phase | State |
| --- | --- | --- |
| R1 | Placement class and key in `NodeOptions`; a node keeps them. Done when: a test records a node with a class and a key and reads both back from the plan. | building |
| R2 | `Runtime::placement_scope()`, `Recorder::within()`, inner scope wins, explicit options win. Done when: a test records nodes in two nested scopes and outside them and reads the class and key of each. Waits on R1. | building |
| R3 | `NodeFacts`, `DeviceFacts`, `Runtime::topology()`, labels from `RuntimeConfig`. Done when: a test reads the local machine's facts: device count, kinds, memory, the labels it was given. | building |
| R4 | `PlacementRule`, `PlacementRules` with its checks, the match by class, prefix, key and operation, priority and insertion order. Done when: a table test resolves a set of nodes against a rule set, including two conflicting rules and every refusal. Waits on R1, R3. | building |
| R5 | `Runtime::set_placement_rules()` between runs; the compile resolves each node once. Done when: a test changes the rules between two runs and sees the second compile place differently; a call during a run is refused. Waits on R2, R4. | building |
| R6 | Local placement: a scope's buffer on its rule's device; `On(scope)`; `if_absent` for an absent device. Done when: a test on a one-device machine sees a rule for device 1 apply `local`, `fail` and a bounded `wait`; on a two-device machine a scope's buffer and its nodes land on device 1. Waits on R5. | planned |
| R7 | Wire version 4: `HELLO` with operating system, devices and labels; `TASK_DISPATCH` with a device; `--label` and `SR_WORKER_LABELS`. Done when: the golden bytes of every message pass at version 4, and the worker options parser takes three labels from flags and the variable. Waits on R3. | building |
| R8 | The master keeps each worker's facts; `WorkerLoad` names the worker and holds its facts. Done when: a test on `InProcessFabric` reads a worker's name, labels and devices from the load a policy receives, before and after a reconnect. Waits on R7. | planned |
| R9 | A worker runs its kernels through its scheduler on the device a dispatch names. Done when: a worker with two devices runs two dispatches on the devices they name; KI-6 and RW-17 close. Waits on R7. | planned |
| R10 | `RulePlacement`: the decision a policy returns becomes worker and device, local, wait or fail; a rule's `deadline`. Done when: on `InProcessFabric` a rule pins a class and key to a named worker, its `wait` holds a node until the worker returns, its `fail` ends the run with a `Status` naming the rule. Waits on R5, R8, R9. | planned |
| R11 | Probe: a registered kernel runs a compiled graph on the worker's runtime, and its local fallback runs one inside a master run. Done when: the test answers yes or no for each of the two, and the answer is written in "What a training run over several machines needs". | planned |
| R12 | Probe and, if needed, fix: a local task's write to a shared buffer moves its version. Done when: a test writes a shared buffer from a local task and sees a worker pull the new version. | planned |
| R13 | Measure the TCP transport between two of the owner's machines: 4 MiB, 64 MiB and 256 MiB, throughput and latency; restore the offload benchmark under `applications/probes/`. Done when: the guide holds the measured numbers with the two machines named; RW-21 and RW-22 close. Waits on R10. | planned |
| R14 | Worker-to-worker links and a ring all-reduce. Done when: designed only if SushiAI's five-machine measurement shows the master's link bounds the step (RW-14). Waits on SushiAI phase A7. | planned |

R13 is a measurement and comes after the labels and the rules, as the owner ordered on
2026-10-20: it is taken once the placement it is measured under exists.

R1 and R3, then R11 and R12, have
disjoint files and could share a wave.

### Work

<!-- work:begin -->
- [2026_10_24_LAYERED_EXECUTION](../../agent/2026_10_24_LAYERED_EXECUTION/PLAN.md): R1, R2, R3, R4, R5, R6, R7, R8, R9, R10, R11, R12, R13
<!-- work:end -->

## Open questions

- Does the plan run inline or in waves? The owner chooses.
- R11 and R12 are probes: the answers are written in "What a training run over several machines
  needs".
