# SushiRuntime

SushiRuntime is a compute runtime built for simulation and game workloads. Its two
design constraints are throughput and operating cost. Everything else — the
scheduling model, the memory design, the toolchain choices — derives from those.

---

## The context

The project exists because of SushiVerse: a physics-driven virtual universe where
the world evolves from simulation rules rather than hand-authored content. That kind
of workload is open-ended in scale. Running it well demands a runtime that can grow
horizontally — more machines, more devices — without requiring changes to the
simulation code above it.

SushiRuntime is developed in the open so other builders can use it. SushiVerse is
the internal consumer that sets the requirements.

---

## What it does

SushiRuntime decides **when**, **where**, and **in what order** work runs. It does
not decide what is computed — that belongs to the simulation code above it.

A caller describes work as a graph: tasks with their data dependencies declared
explicitly. The runtime figures out which tasks can run in parallel, routes them to
the right hardware (CPU or GPU), and manages the execution. The same graph can be
run for thousands of steps without being rebuilt, which is the core performance
property for simulation loops.

Three workload shapes drive the design:

- **Field and grid updates** — stencil operations, differential equation stepping,
  cellular automata. The runtime's double-buffered state primitive maps directly onto
  the read-current/write-next pattern these require.
- **Entity/Component/System (ECS) scheduling** — entities carry components; systems
  operate on component arrays. Systems with disjoint data run in parallel
  automatically; conflicting ones are ordered. The dependency mechanism that handles
  this is the same one used everywhere else in the runtime.
- **Multi-machine worlds** — a simulation too large for one machine partitions its
  spatial domain across nodes. Each machine owns a region and exchanges only boundary
  data with its neighbors each step. This is the long-horizon model for SushiVerse
  at full scale.
- **Deterministic stepping** — the same input sequence must produce bit-identical
  output across runs and across machines. This is the foundation for multiplayer
  lockstep and replay. A deterministic execution mode — fixing task order and
  floating-point reduction order, separate from the default throughput-optimized
  path — is a planned goal (WP-4 on the roadmap), not yet implemented: there is
  currently no such mode, API, or flag in the runtime.

```
One step of a simulation loop
──────────────────────────────────────────────────────────────

  [ declared task graph ]
        │
        ▼
  resolve dependencies ──► which tasks can run in parallel?
        │
        ▼
  dispatch to hardware
  ┌─────────────────────────────────────────────┐
  │  CPU workers          GPU queues            │
  │  (host logic,         (field kernels,       │
  │   ECS systems)         BLAS operations)     │
  └─────────────────────────────────────────────┘
        │
        ▼
  advance state ──► next step
```

---

## Why LLVM and SYCL

LLVM provides a portable compiler infrastructure that targets NVIDIA, AMD, and Intel
GPUs from the same codebase. The current toolchain is intel-llvm (nightly bundle),
with oneAPI as an opt-in for Intel-specific paths.

SYCL 2020 is the programming model. It is an open ISO standard on top of standard
C++, with no vendor-specific syntax at the API boundary. The primary toolchain is
intel/llvm `clang++ -fsycl`; AdaptiveCpp (formerly hipSYCL) and Intel oneAPI are
also supported. Any of them compiles a single SYCL source to CUDA, HIP/ROCm, Intel,
and CPU targets, so the same kernel runs across vendors without a rewrite.

The practical consequence: a simulation kernel written once runs on a gaming GPU
today and on a different vendor's data-center hardware tomorrow without a rewrite.
For a project whose hardware targets will shift over years, that portability is
non-negotiable.

---

## Where it fits in the Sushi Systems stack

SushiRuntime is the bottom of the stack. Three libraries sit above it, and
applications — including SushiVerse — sit above those.

```
┌──────────────────────────────────────────────────────┐
│                    Applications                       │
│           (simulations, games, SushiVerse)            │
└───────────────┬──────────────┬───────────────────────┘
                │              │
        ┌───────▼──────┐  ┌───▼──────────┐
        │  SushiEngine │  │   SushiAI    │
        │  (simulation │  │  (ML/AI      │
        │   engine)    │  │  framework)  │
        └───────┬──────┘  └───┬──────────┘
                │             │
        ┌───────▼─────────────▼──────────┐
        │           SushiBLAS            │
        │    (math and linear algebra)   │
        └───────────────┬────────────────┘
                        │
        ┌───────────────▼────────────────┐
        │          SushiRuntime          │
        │  (scheduling, memory, SYCL,    │
        │   distributed dispatch)        │
        └────────────────────────────────┘
```

**SushiBLAS** provides the math operations — matrix products, reductions, stencil
kernels. It has no knowledge of the runtime; it exposes plain functions that return
SYCL events. The application wraps those calls in tasks and hands them to the
runtime for scheduling.

**SushiEngine** owns the simulation loop: physics, entity state, scene structure.
It uses SushiBLAS for compute-heavy steps and relies on SushiRuntime to schedule
them correctly across hardware.

**SushiAI** is the machine-learning framework. Same pattern: stateless operations
through SushiBLAS, scheduled by SushiRuntime.

None of the three libraries depend on the runtime directly, and none depend on each
other. The application is the only place they meet. This keeps each layer testable
and replaceable independently.

---

## Further reading

- [sushisystems.io](https://sushisystems.io) — company context and project overview
- `docs/guides/` — API guides, contributing, C++ standard migration notes
- `INTRODUCTION.md` — how to use the API
- `CONTRIBUTING.md` — build instructions and coding standards
