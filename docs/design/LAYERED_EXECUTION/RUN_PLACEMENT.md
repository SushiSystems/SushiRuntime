# Run placement

A caller's policy decides, run by run, whether a graph's step runs on the device or on the calling
thread. The runtime offers the question and the host path; the caller owns the answer.

## Problem

A trace of a client's `DynamicGraph` under `allow_chain(true)`, 154 s long, shows a step whose only
device work is two kernels of about 0.007 ms each, some 0.03 ms of device time per step. The calling
thread still waits 3.6 ms per step in `chain.wait`, because the CUDA work queues behind graphics work
on the same GPU. For work that small the submission and the wait cost more than running the kernels'
host forms on the calling thread.

Where the line between "small" and "large" falls depends on the client's hardware, its other work on
the GPU and how much latency it can take. The runtime cannot know any of that.

## Decision

The owner ruled on 2026-11-14:

| # | Subject | Ruling |
| --- | --- | --- |
| 1 | Who decides | Whether a run goes to the host or to the device is a mechanism of the runtime. The threshold is the client's policy, set from its own benchmarks. The runtime never names its client |
| 2 | Default | With no policy set, every run behaves as it does today, bit for bit |
| 3 | Granularity | The policy may answer differently at every run; the runtime keeps no hysteresis, and a client policy may add its own |
| 4 | Shape | Additive: an interface the client implements, a description it reads, a setter on the shared step driver. Nothing existing is deleted |

## Design

### The interface

`modules/foundation/placement/include/SushiRuntime/placement/run_placement_policy.hpp` holds three
types, all in `SushiRuntime`:

| Type | Role |
| --- | --- |
| `RunPlacement` | `Device` (the runtime's own path) or `Host` (the calling thread) |
| `RunDescription` | What the runtime knows of a run before its first step, read-only |
| `RunPlacementPolicy` | `virtual RunPlacement place(const RunDescription& run) = 0;` |

`RunDescription` holds only what the runtime measures without knowing what a node means:

| Field | Meaning |
| --- | --- |
| `node_count` | Nodes one step runs |
| `steps` | Steps the run asks for: the count `run(steps)` passed, 1 for `run_for()` and `run_until()` |
| `launched_items` | Sum of the launched widths of the nodes whose width is known, as they stand before the run's first step |
| `unsized_nodes` | Nodes whose width is unknown, such as a host node or a raw command group |
| `declared_flops` | Sum of the `NodeCost::flops` the recorder stated |
| `declared_bytes` | Sum of the `NodeCost::bytes` the recorder stated |
| `costed_nodes` | Nodes that carry a `NodeCost` |

A policy reads that struct and nothing else; it never sees a node, a plan or a queue.

`Graph` and `DynamicGraph` take a policy through their shared step driver:
`set_run_placement_policy(std::shared_ptr<RunPlacementPolicy>)`, where null removes it. A move keeps
it, as it keeps `allow_replay()`.

### The step loop

A blocking run with a policy asks it once, before its first step, under the profile zone
`run.placement`. An answer of `Device`, or no policy, takes the layer GRAPH_REPLAY's step loop
already picks. An answer of `Host` runs the steps through `HostStepRun`
(`modules/engine/execution/`), when the plan can run there:

- each step walks the plan's DAG from its roots on the calling thread, in the order the
  dependencies free the nodes, and runs each enabled node through `Node::run_on_host()`, the path
  a host runtime's engine already takes;
- a disabled late-bound node runs nothing and still frees its successors;
- a node that fails ends the run with the error `name_failure()` gives it, as on the chain;
- a timed run records one dispatch per node into the plan's ledger on `K_HOST_DEVICE_INDEX`,
  stamped from the engine's clock;
- each step runs under the profile zone `host.step`;
- `RunReport::layer` and the observation name `ExecutionLayer::Host`.

`host_step_refusal()` decides whether the plan can run there. Any doubt keeps it on the device. It
refuses a plan when a node:

- has neither native host work nor a host form, such as an `NdRange`, a raw command group or a queue
  body;
- is one an offload coordinator acts on, or carries live address lists;
- names no region, so the memory its kernel touches cannot be checked;
- touches an allocation the host cannot reach: device USM, or memory the runtime's allocator holds no
  kind for.

A refused `Host` answer runs on the device and logs the reason at `DEBUG`. The run reports the
layer that ran, so the policy's answer and the outcome can differ only in that one case.

The question reads memory through the port `HostReachableMemory`. Its USM adapter, which answers
from the allocator's records, lives in the source file, and a test passes its own.

`run_async()` keeps today's path and does not ask the policy.

### Why it is an open-closed extension point

The step loop depends on `RunPlacementPolicy`, not on any rule. A client adds a threshold, a
moving average or a hysteresis band by writing a class, and the runtime does not change. A new fact
for policies is a new field of `RunDescription` with a default, so existing policies still compile.
A plan the host path cannot run is refused in one function, so a new kind of node that cannot run
there adds one refusal to it. The host path reuses `Node::run_on_host()` and adds no second way to
run a node on the host.

### Out of scope

- A threshold of the runtime's own. The client measures and owns it.
- Splitting one step between the host and the device.
- Choosing per step inside one run: the answer holds for every step of the run.
