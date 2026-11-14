# chain_replay

**Describes:** `modules/engine/execution/include/SushiRuntime/execution/chain_replay.hpp`, `modules/engine/execution/source/chain_replay.cpp`

Layer 2 of [the GRAPH_REPLAY topic](../../../../docs/design/GRAPH_REPLAY/README.md): a chained plan
recorded once into a command graph and replayed once per step. The
[module README](../README.md) links this page.

## ChainReplay

One ChainReplay lives for one run of a plan that `prepare_replay()` recorded. Each submit_step()
submits the recorded step once with `queue::ext_oneapi_graph`, on the same untimed in-order queue a
ChainRun of the plan uses, and finish() is the run's single wait. A run is never timed: the step
loop sends a timed run to ChainRun, because a replay yields one event and not one per node. Like
ChainRun it returns `Result<void>` through `Internal::SYCL::translate_exceptions()`, drains the queue
in its destructor when finish() was not reached, and marks itself on the engine with a
`ChainedRunMark`. The zones are `chain.replay_step` and `chain.wait`.

A recorded step whose late-bound windows read host words keeps the event of its last replay, and
submit_step() waits on it before the next replay writes the words, because the device reads them
while the replay executes. A step without such a window replays back to back.

## prepare_replay and replay_refusal

`prepare_replay()` settles once per classification whether a chained plan replays. A plan whose
`ChainPlan::recorded` is set replays; one whose `ChainPlan::not_replayable` is set does not; any
other is checked and recorded. `replay_refusal()` walks the chain's order and refuses a task body, a
node whose `SubmitForm` is `StepBound` or `DeviceSized`, and a `LateWindow` with a device count or
base slot. `replays_on()` admits the CUDA and OpenCL backends. A refusal is kept in
`not_replayable` and logged at `DEBUG` under `sushiruntime.execution`, as classify_chain() logs its
own.

The recording runs on a private in-order queue of the plan's device, so it captures nothing another
thread submits to the shared queue, and submits each node with no event dependency through
`Internal::Graph::submit_task()`. A `LateWindow` node is submitted while its
`LateBindings::replay_words` points at its three words in one host-USM block, and the pointer is
cleared after it, so layer 1 never lowers the node to the recorded form. What recording or
finalizing throws is logged once at `WARN` and sets `not_replayable`; the run continues on layer 1.
The recording is held behind `Internal::Graph::RecordedSteps`, so the graph module owns it without
naming the graph extension (`docs/design/GRAPH_REPLAY/README.md`, "Layer 2: record and replay").
