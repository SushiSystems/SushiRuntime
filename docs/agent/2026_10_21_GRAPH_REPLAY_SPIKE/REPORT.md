# Graph replay: the feasibility spike

**Status:** Shipped — 2026-10-21

The measurements behind [the graph replay design](../../design/GRAPH_REPLAY.md), which took them
out when it took the design format. The consumer is SushiEngine's matter device solver, whose
device arm is launch-bound.

Evidence: the feasibility spike under sushiengine-30's scratchpad (`graph_spike/`, run
2026-09-19), and the engine's measurement report in SushiEngine's
`docs/design/MATTER_DEVICE_SOLVER.md`, R28 and R29.

## The settled tick

On an RTX 3080 Ti a settled tick of the matter solver's 129-particle scene issues 1,484 node
dispatches through `API::Graph::run` and costs 42 ms. Its kernels do almost no work: summed
kernel execution is 24 ms of the 42, and the mean dispatch costs 28 us of wall time. The host
solver runs the same tick in under 5 ms. Grid sort alone is 53 of the 67 dispatches a substep
issues, because each prefix scan, counting sort and reduce lowers to several nodes.

## The probe

A bare SYCL probe on the same machine, same kernels in a dependency chain of K nodes:

| Device | Wait after every kernel | Chain on one in-order queue, one wait | Recorded graph, replayed |
|---|---|---|---|
| RTX 3080 Ti (CUDA) | ~12 us/node | ~6.5 us/node | ~1.1 us/node |
| Ryzen 5 7600X (OpenCL CPU) | ~11.4 us/node | ~6.0 us/node | ~4.7 us/node |

Per node, `Graph::run` costs more than twice the probe's worst column. Replaying a recorded graph
would take the 1,484-dispatch tick from about 18 ms of launch overhead to about 1.6 ms.

## Other spike findings

- Without profiling, a run that submits through `sycl::ext::oneapi::experimental::submit` keeps the
  per-node cost near 10 us from 1 to 128 nodes on a chain of trivial kernels; keeping the events
  grew it from 15 us to 266 us.
- The OpenCL CPU device reports only `ext_oneapi_limited_graph` yet records, finalizes and replays
  correctly.
- One small graph replayed N times against one graph holding N copies: equal on the GPU, the large
  graph 18% faster on the CPU, and the large graph's finalize 40 ms on the GPU against 0.5-2.5 ms.
- Level Zero, HIP and native CPU could not be run on the machine of the spike.
