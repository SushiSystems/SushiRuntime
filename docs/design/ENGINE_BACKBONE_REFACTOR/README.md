# SushiRuntime: engine backbone refactor (BB)

**Status:** Open — BB-0 runtime side, BB-1a, BB-2, BB-3, BB-4 landed; BB-1b unbuilt; BB-5, BB-7, BB-8 not re-checked
**About:** The runtime's simulation core is already the right machine: dependency-inferred DAG, compile-once replay, late-bound sizes and bases, fixed-order deterministic reductions, interval-exact...
**Modules:** none

The reductions, the prefix scan and the counting sort this page names are SushiBLAS's;
[the design map](../README.md#names-that-left-the-runtime) says how to read them.

## Problem

The runtime's simulation core is already the right machine: dependency-inferred DAG, compile-once
replay, late-bound sizes and bases, fixed-order deterministic reductions, interval-exact region
boundaries, an async step; R1–R7 all built and verified on the branch. The gaps that remain are
not in the compute model; they are at the **edges**: the branch is unmerged and unadopted
(BB-0), simulation results cannot reach Vulkan without a host round-trip (BB-1), the runtime
behaves as the machine's sole tenant (BB-2), the build flags undermine the determinism the API
promises (BB-3), no out-of-tree consumer can link it (BB-4), and no player machine can run it
(BB-5). Those six, in that order, are the backbone refactor. BB-6/7/8 are the maturation tail.

Three facts reorder every plan written before this one. R1 to R7 exist and the engine did not
know. The engine has adopted part of the new surface, and the call-site counts and the hand-built
stand-ins it listed are in the evidence.
Where the runtime was absent, the engine routed around it and recorded why: audio's real-time mix
sidesteps the runtime, the atmosphere tier chose Vulkan compute and wrote the reason down, and
cosmetic particles and skinning shipped as Vulkan compute with the runtime's SYCL kernel surviving
only as a correctness oracle. Every one of these is a boundary lesson the plan takes as input, not
as damage to undo.

## Decision

**Mandate.** SushiEngine is the head; SushiRuntime is the backbone. This document plans the
runtime refactor that makes that relationship the fastest, strongest, most operable one available:
shaped by what the engine *actually does and actually needs*, not by what a general compute
runtime might want to become. It extends, and does not replace,
[`SIMULATION_ENGINE_SUBSTRATE_PLAN/`](../SIMULATION_ENGINE_SUBSTRATE_PLAN/README.md) (WP-0…WP-7) and
`PHYSICS_SUBSTRATE_IMPLEMENTATION.md` (R1–R7). New work
packages are numbered **BB-0…BB-8** to avoid colliding with the WP series.

### Principles

**P1: Usage shapes the API; v0.x is the time to break.** The owner's decision: no
`supports()`-style capability hedging, no compatibility shims before v1.0.0. The API is shaped
once, from the real consumer's call sites, and then frozen. Every package below states its API
changes plainly as changes.

**P2: The boundary rule, kept.** The engine's governing rule (its `ARCHITECTURE.md` §1): *"A
feature that only composes the runtime's public API belongs in the engine. A feature that needs
the runtime to do something it cannot yet do belongs in SushiRuntime."* Corollaries already
locked: graph colouring stays engine-side (L8); DSP math stays engine/SushiBLAS-side; the runtime
owns ordering, memory, scheduling, and determinism of the *mechanism*.

**P3: Determinism is the product.** Same-binary, same-device determinism is the one property
that makes this runtime a better backbone for SushiLoop than raw Vulkan compute. Anything that
weakens it silently (build flags, default-on rebalancing, schedule-dependent arithmetic) is a
defect even when it is faster.

**P4: The sim domain, not render resources.** The atmosphere rejection is scope guidance:
*"[the runtime's] value is in the deterministic simulation domain, where the data does not have
to become something the rasteriser samples."* BB-1 exists to let simulation *output* flow to the
renderer; it is not an invitation to move render-resource computation into SYCL.

**P5: A claim is only real if a TU instantiates it.** The `add_reduce` incident: the engine
called an API that did not exist, and `-fsyntax-only` blessed it for weeks because nothing
instantiated the template. Both repos adopt the build rule: every public runtime API the engine
leans on has at least one engine-side TU that instantiates it, and every runtime feature ships
with a consumer-shaped example compiled in CI.

**P6: Co-tenant, not sole tenant.** The runtime today assumes it owns the machine: workers
pinned to cores 0…N−1, `hardware_concurrency()` threads, a background rebalancer on by default, a
process-global allocation mutex. Inside a game it shares the machine with a render thread, an
audio callback, asset I/O, and the OS compositor. Every scheduling default gets re-examined under
that assumption.

### What this plan deliberately does not do

Recorded so it is not re-proposed: the same discipline the atmosphere doc uses.

- **No render-resource workloads.** Cloud noise, LUT bakes, PSO-adjacent work stay in Vulkan
  compute. BB-1 moves simulation *output* toward the renderer, never the reverse (P4).
- **No cross-architecture determinism.** L3 ruled it out; the engine's conformance tolerance
  (`1e-9`, FMA codegen differences) is the accepted boundary. BB-3 hardens *same-binary,
  same-device* determinism only.
- **No DSP math, no BLAS.** FFT/convolution/filters belong to the engine and the future
  SushiBLAS; the runtime provides the data plane and the batch offload path. The audio descope
  stands.
- **No capability-flag hedging.** Per the owner's direction: the API is shaped to the sim
  profile and broken freely until the freeze; `supports()`-style surface is explicitly rejected.
- **No engine-side colouring or islanding in the runtime.** L8's split is working: the tracker
  orders, the domain colours. R3+R6 gave the mechanism; the policy stays above.
- **No distributed-layer work in the game path.** The master/worker layer stays optional,
  zero-cost when off, and untouched by this program except where BB-4's define hygiene brushes
  it.

## Design

The plan is nine work packages. Each page below holds one package; this page holds what they
share.

### The packages

| Pkg | Title | Size | What it unblocks |
|---|---|---|---|
| BB-0 | Merge & adopt R1–R7 | S (runtime) + M (engine) | P8 collision-in-graph, islands, engine deletes its reduction; ends the two-truths state |
| BB-1 | Vulkan interop: external memory import + sync export | L | Zero-copy palettes, soft-body vertices, VFX sim, render extract: the four recorded consumers |
| BB-2 | Co-tenancy: thread & scheduling policy | M | Editor/game co-existence, audio thread safety, jitter floor |
| BB-3 | Determinism contract: flags, fp64, conformance | S–M | SushiLoop rollback guarantee; kills the silent `-march=native`/`/fp:fast` hazard |
| BB-4 | Consumer linkage & packaging | M | `find_package(SushiRuntime)`, out-of-tree engine builds, versioned pinning |
| BB-5 | Ship path: multi-vendor, AOT, cache, zero-device | L | A game binary a player can run |
| BB-6 | Data-plane maturation | M, incremental | Batched transfer, buffer growth, report-free runs, pool decision, device sort |
| BB-7 | Async depth: pipelined steps, deadlines | M–L | Sim/render overlap, audio batch-DSP on the fluent API |
| BB-8 | Platform posture: decide, document, spike | S (decide) | The cross-platform plan stops guessing |

### Sequencing

```
BB-0  merge & adopt          ──►  everything (it is the current-truth reset)
BB-2  co-tenancy      ┐
BB-3  determinism     ├── parallel, small, no cross-deps; land before or beside BB-1
BB-4  linkage         ┘
BB-1a interop import         ──►  BB-1b sync export ──► engine zero-copy milestones
BB-5a fat binary  ──► BB-5c AOT/cache ──► BB-5d redist   (BB-5b any time; BB-5e with 5a)
BB-6  continuous behind BB-0 (item 5 sort after adoption; item 4 decision before freeze)
BB-7  after BB-1 + BB-2 (overlap needs both halves)
BB-8  decision + docs now; spikes whenever a desktop lull allows
v1.0.0 freeze: after BB-4 + BB-3 land and BB-6 items 4/6 are decided — the freeze list in
docs/backlog/README.md gains exactly those entries.
```

The dependency worth stating twice: **v1.0.0 must not be tagged before BB-3 and BB-4.** A frozen
API whose build flags still leak `-march=native` to consumers, or that still cannot be consumed
out of tree, freezes the two things most worth fixing.

### Proof obligations

Every package lands with the test that would fail without it, per house rule.

| Pkg | Test | Fails without the change |
|---|---|---|
| BB-0 | engine conformance vs tagged v0.2.0; substep counts bit-identical across reduction swap | engine still solving on hand-built reduce, or drift between the two reducers |
| BB-1a | `interop_demo`: kernel writes imported Vulkan buffer, contents verified, zero copies | no import entry point exists |
| BB-1b | Vulkan stub waits on exported semaphore, reads without host sync | only host-side `wait()` orders the two APIs |
| BB-2 | reserved-core test: no runtime thread on cores the config excludes; zero allocs in steady-state tick with `run(RunReport&)` | worker 0 pins to core 0; report allocates every run |
| BB-3 | graph-level byte-equality at worker counts {1,2,max}; red if `/fp:fast` restored | reassociation changes host folds between builds |
| BB-3 | `require_fp64` filters devices; empty-list error is precise | fp64-less device fails mid-kernel with a driver error |
| BB-4 | out-of-tree `find_package` consumer builds and runs on CPU lane; inherited-flags assertion | no Config package; consumer inherits `-Werror`/`-march` |
| BB-5a | one `cuda;cpu` binary passes suite on NVIDIA lane and POCL lane unmodified | one vendor per binary |
| BB-5b | `try_create` returns error (not throw) with device filter emptied | `Runtime::create()` throws; game cannot boot |
| BB-5c | cold-start ≤ stated budget with warm persistent cache | first-run JIT stall unbounded and unmeasured |
| BB-5d | redist component matches manifest exactly | shipped binary missing `ur_loader`/adapter/hwloc |
| BB-6.1 | N-range write is one submit (asserted via profiling counters) | N synchronous memcpys |
| BB-6.2 | `rebind` + next `run()` rebuilds; stale-pointer path impossible by construction | animation-style manual realloc + silent capture hazard |
| BB-7.1 | `wait_for` returns at deadline with step still running; later `wait` completes it | only blocking `wait()` exists |

The request register lives in the engine (`sushiengine/docs/slop/physics_system.md` §18, to be
corrected under BB-0). The runtime-side histories are
[`PHYSICS_SUBSTRATE_REQUIREMENTS.md`](../LAYERED_EXECUTION/PHYSICS_SUBSTRATE_REQUIREMENTS.md) and
the physics substrate implementation document in the version history. The long-horizon substrate program is
[`SIMULATION_ENGINE_SUBSTRATE_PLAN.md`](../SIMULATION_ENGINE_SUBSTRATE_PLAN/README.md), whose WP-4
(reductions, delivered) and WP-5 (multi-device, long horizon) this plan slots around, and WP-7
(native CPU path) is referenced by BB-5b.

## Phases

| Id | Phase | State |
| --- | --- | --- |
| BB-0 | [Merge and adopt](BB_0_MERGE_AND_ADOPT.md): the runtime side is done; the engine side adopts the seams | building |
| BB-1 | [Vulkan interop](BB_1_VULKAN_INTEROP.md): external memory import (1a) is built; completion export (1b) is not | building |
| BB-2 | [Co-tenancy](BB_2_CO_TENANCY.md): thread and scheduling policy | built |
| BB-3 | [Determinism contract](BB_3_DETERMINISM_CONTRACT.md): flags, fp64, conformance | built |
| BB-4 | [Consumer linkage and packaging](BB_4_CONSUMER_LINKAGE.md) | built |
| BB-5 | [The ship path](BB_5_SHIP_PATH.md): multi-vendor, AOT, cache, zero-device | planned |
| BB-6 | [Data-plane maturation](BB_6_DATA_PLANE.md): the pool decision is done, the rest is open | building |
| BB-7 | [Async depth](BB_7_ASYNC_DEPTH.md): pipelined steps, deadlines | planned |
| BB-8 | [Platform posture](BB_8_PLATFORM_POSTURE.md): decide, document, spike | planned |

### Work

<!-- work:begin -->
No open work.
<!-- work:end -->

## Open questions

- BB-5, BB-7, BB-8 and the rest of BB-6 have not been re-checked against the tree; their states
  above are the plan's, not a verified reading.
- BB-1b: a timeline-semaphore signal (`run_async(SignalDescriptor)`) or the run's final events per
  imported buffer. The design decides against both toolchains at implementation.
- BB-0 engine side: whether the engine has a segmented-reduce case at all.
