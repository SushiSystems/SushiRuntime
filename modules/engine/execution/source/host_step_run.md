# host_step_run

**Describes:** `modules/engine/execution/include/SushiRuntime/execution/host_step_run.hpp`, `modules/engine/execution/source/host_step_run.cpp`

The calling-thread path of [the RUN_PLACEMENT page](../../../../docs/design/LAYERED_EXECUTION/RUN_PLACEMENT.md):
a plan's steps run through each node's host form when a graph's run placement policy answers
`Host`. The [module README](../README.md) links this page.

## HostStepRun

One HostStepRun lives for one run, as a ChainRun does, and the step loop drives it through the
same `submit_step()`, `wait()` and `finish()`. Each `submit_step()` walks the plan's DAG from its
roots on the calling thread, in the order the dependencies free the nodes, and runs each enabled
node through `Node::run_on_host()`, the path `HostExecutionEngine` takes for each node. A disabled
late-bound node runs nothing and still frees its successors. A node that fails ends the step with
the error `name_failure()` gives it, and the nodes it would have freed do not run; the next step's
re-arm restores the counts. A timed run records one dispatch per node into the plan's ledger on
`K_HOST_DEVICE_INDEX`, stamped from the engine's clock. `wait()` returns at once, and `finish()`
releases the run's mark on the engine. The zone of a step is `host.step`.
