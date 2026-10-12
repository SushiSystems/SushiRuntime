# tests/benchmark

Performance benchmarks and stress tests, one `benchmark_*.cpp` file each. A benchmark
prints its figures and asserts correctness and forward progress; it asserts a speed only where
a section below says so. The helpers the files share are in `tests/common/`.

## benchmark_async_overlap.cpp

The benchmark measures CPU and GPU concurrency. Two kernels dispatched without a wait between
them run together, so the wall time approaches `max(T_cpu, T_gpu)` instead of the serial sum.
Overlap only shows when the two run times are comparable, so the GPU iteration count is
calibrated to the CPU kernel first. The figure covers device queue overlap only: the scheduler
and the host-to-device and device-to-host transfers are outside it.

`best_of` keeps the fastest of several runs, which strips scheduling and operating system
noise from the timing.

`launch_heavy` runs a non-linear recurrence, `acc = sin(acc) * 1.0000001 + 0.5`, once per
iteration. A linear body would let the compiler collapse the loop, and the kernel would stop
scaling with `iters`.

The two thresholds are fractions of the serial time. An overlap time under 0.75 of the serial
time is healthy concurrency: the bar leaves slack for dispatch overhead and contention, and a
run that serializes, at about 1.0x, still fails it. An overlap time at or above 0.95 of the
serial time means no overlap at all and fails on every host. That second check also catches
`CUDA_LAUNCH_BLOCKING=1`, under which every launch runs synchronously.

A run that lands between the two bars is skipped on WSL2 and nowhere else. The CUDA backend
under WSL2 adds dispatch latency and exposes no PCI topology, which inflates the overlap wall
time enough to miss the 0.75 bar on a Pascal card even while the devices overlap. The result
is inconclusive there, and bare-metal CI still enforces the bar. `running_under_wsl` exists to
relax the check on that host, not to remove it.

## benchmark_chain_dispatch.cpp

This is the baseline for the graph-replay design, `docs/design/GRAPH_REPLAY.md`, sections 0 and
G (P0). Node i reads buffer i and writes buffer i+1, so every node depends on the one before it
and the scheduler cannot dispatch two nodes at once. The run therefore pays the scheduler's
whole per-node dispatch path, which is dependency resolution, submit, poll and the hand-off to
the successor, with no fan-out to hide it behind. The benchmark sweeps the chain length K and
reports milliseconds per node. Layer 0, which removes the per-node allocation in
`SmallEventVector::to_vector()`, and the later layers are measured against that number.

## benchmark_distributed_offload.cpp

The benchmark measures the round trip of an offloaded task: dispatch, the worker's pull of the
input, the kernel, the output chunks written back into the master's buffer, and the result. It
is built only with `SR_ENABLE_DISTRIBUTED`. Master and workers run in one process, each worker a
`WorkerAgent` on a runtime of its own. The kernel writes each input element plus one, and every
task is recorded with `record_registered_task`, so the local fallback is the same kernel. The policy
is `LeastLoadedOffloadPolicy(0)`, which offloads a task whatever its size.

Each case runs once untimed, which pays the first touch of the buffers and the worker's cache
allocation, then five timed runs, of which the median is reported. Before a run the inputs are
rewritten and bumped, so a worker pulls them again instead of reading its cache. The timed
region is the `run()` call alone: the refill, the graph build and the check of the outputs stay
outside it.

`OffloadRoundTripThroughput` runs 256 independent tasks of 256 floats each on two workers over
the in-process fabric. It prints milliseconds per run, microseconds per task, tasks per second,
the dispatches offloaded and taken back in the timed runs, and the kernels each worker has run
since it started, which is the split the policy made. It asserts that every output is right and
that at least one dispatch was offloaded.

`RoundTripByBufferSize` runs one task with one input and one output of 64 KiB, 4 MiB and 64 MiB,
first over the in-process fabric and then over TCP on a loopback port the OS chooses, with one
worker. It prints milliseconds per round trip and MiB per second, counting the input going out
and the output coming back. It asserts that every output is right, that each of the five timed
runs was offloaded and that no task was taken back.

Neither test asserts a speed.

## benchmark_dynamic_graph.cpp

A scale probe for the dynamic region graph: 256 regions stream in, 16 of them mutate between
steps, and the world keeps running correctly. The test asserts the composition contract at
scale, one recompose per mutation and not one per region, and reports the streaming cost. It asserts no recompose speedup and guards
correctness and scale.

The final check rebuilds the expected count per cell. Every region ran the initial step plus
one step per run, and a mutated region gained one more increment on each run from the one that
streamed its second task in.

## benchmark_graph_compile.cpp

The file closes two gaps: how `TaskGraph::compile()` scales with graph size, and whether
overlapping host-to-device and device-to-host transfers with compute helps. Both tests do
identical work on each side and verify the result. They print figures and assert only that the
run made progress, because real overlap needs a discrete GPU with copy engines.

`min_compile_us` uses a rolling set of 16 buffers so that consecutive tasks form real
dependencies, which is what exercises the dependency tracker.

## benchmark_profiling_queue.cpp

This is the benchmark behind section E, item 7 of
`docs/archive/superpowers/specs/2026-09-15-g1-node-event-records-design.md`. That item decided
F2: a second, lazily built profiling queue per device, instead of always profiling and
filtering afterwards. The measured delta was about 24 microseconds per submission on an
RTX 3080 Ti under CUDA.

The warm-up round is untimed so that the first submission's driver and JIT costs reach neither
queue's timed rounds. The queue that goes first alternates each round, so a systematic drift,
thermal or driver warm-up, does not always favour the same queue.

## benchmark_recompose_churn.cpp

The gate benchmark for the "allocation-per-node churn on the recompose path" theme, audit
Tier B. The substrate commits work proportional to the changed region every step: a dirty
region is rebuilt, and each rebuilt node pays a `Node` allocation, two IO-region vectors, an
`AccessNode` per access, event constructors and build list nodes. The benchmark isolates that
cost in two ways.

Commit wall time is the portable headline. A commit is one `region().add()` that marks a
region dirty and one `run(1)` that recomposes and steps. Compared with a `run(1)` that has no
mutation, which is pure replay, the delta is the recompose cost.

Allocations per commit come from a gated global `operator new` counter. The count is
authoritative on a single-CRT build, Linux and CI, where it sees allocations inside
libsushiruntime too. On a Windows DLL build it counts only allocations on the executable's
side, from headers and templates, so trust the wall-time delta there and the allocation count
on the CI lane.

The counter is off by default and during every other test, so the process-wide replacement
costs one relaxed load per allocation outside the measured windows. One replacement serves the
whole benchmark binary and routes everything through `malloc` and `free`. This replacement is
also why an AddressSanitizer build leaves the benchmark suite out; see `cli/README.md`.

The test asserts correctness and the composition contract, one recompose per mutation, and
never a speed or allocation threshold. The numbers are reported for the owner to compare
across the B2 changes. If the recompose allocations or milliseconds dominate, the node,
`AccessNode` and event churn is the target. If the pure-replay step already allocates heavily,
the effort belongs in steady-state execution.

The closing loop is a smoke check. Every region ran the warm step, the replay-baseline steps
and one step per commit, and a mutated region gained an extra increment on and after the
commit that streamed its second task in. The test asserts forward progress and the composition
contract and does not rebuild the exact count per cell, which the `Integration_DynamicGraph`
tests own. A nonzero value that is uniform across a region is enough here.

## benchmark_root_fanout.cpp

The gate benchmark for the batched root injector, audit ds-1. Each replayed step re-injects
every root node. `submit_batch` takes a step's whole root set in one lock acquisition, one
`inflight_` increment and one wake burst, so the injection cost per step does not grow with
root fan-out.

The benchmark builds R independent roots, each writing its own buffer, replays a fixed number
of steps, and sweeps R. It reports milliseconds per step and the amortized cost per root and
step. Injection is O(1) per step whatever R is, so as R grows the per-root cost should fall
towards the cost of executing and completing one task, and should not stay pinned to a lock
and a notify per root. Correctness and forward progress are asserted; speed is not.

## benchmark_scheduler_dispatch.cpp

The benchmark measures dispatch overhead per task. Both sides do the same work, one relaxed
atomic increment, so the only difference is the path: inline in a tight loop, which is the
floor, or wrapped in a task and run through the work-stealing scheduler. The delta is the
amortized cost of submit, steal and dispatch. The test reports it and asserts correctness and
forward progress, not speed.

## benchmark_steal_locality.cpp

A real-hardware benchmark for NUMA-aware hierarchical stealing. It drives the scheduler with a
large batch of host tasks and reports throughput and the split between local and remote
steals. Locality exists only across real NUMA nodes, so the test skips on a single-node
machine or under WSL; `Unit_NumaStealTest` holds the logic check.

Locality is asserted only above 1000 steals. Below that the ratio means little, and a
perfectly balanced drain can legitimately produce few steals.

## benchmark_steal_numa_effect.cpp

The benchmark compares NUMA-aware with NUMA-blind stealing on the same hardware. One workload
runs once on the real, flat topology and once on a synthetic two-node topology, and the test
reports both throughputs and the aware side's steal split. On a UMA machine, single socket or
WSL, there is no real speedup to claim, so it asserts correctness and that the aware path
keeps steals node-local.

The steal split is gathered while the workload is timed, so it reflects locality only when the
cores sit on separate NUMA nodes. On a single-socket machine the two synthetic nodes share one
set of cores, and OpenMP and runtime worker threads contending for them drag the local
fraction below the 0.5 bar with every toolchain; 0.46 to 0.48 was observed.
`Unit_NumaStealTest` verifies the locality ordering deterministically, so the assertion made
during timing fires only on real multi-socket hardware.

## benchmark_wide_fanout.cpp

The gate benchmark for the deque-steal fast path, audit steal-1, and the global quiescence
counter, audit sched-1. It profiles them in the substrate's characteristic shape: many small
steps, not one long sustained stream. Each step submits a modest batch of independent tasks
and drains to quiescence. The tail of every step is where workers spin idle, stealing from
empty peers, and steal-1 removes a `seq_cst` fence from each empty attempt. It is also where
`inflight_` and the shared counters are hammered, which is the contention sched-1 targets.

The benchmark reports drain latency per step and nanoseconds per task. That isolates the fixed
overhead per step that dominates the substrate, and complements the single sustained stream of
`benchmark_scheduler_dispatch.cpp`. Attributing steal-1's fence removal and sched-1's line
contention in full needs hardware counters, `perf stat` and `perf c2c`. This benchmark is the
portable guard on throughput and latency, and the harness those counters attach to. It asserts
correctness and forward progress only.

## benchmark_epoch_stress.cpp

A high-churn stress test for epoch-based reclamation. Many threads enter and leave critical
sections and retire objects, while threads spawn and exit to exercise the orphan handoff.
Every retired object must be destroyed once, and most must be reclaimed. Under ASan and TSan
the same run surfaces races and use-after-free.
