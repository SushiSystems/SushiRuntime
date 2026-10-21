# Engine backbone refactor: evidence

**Status:** Shipped — 2026-10-21

The surveys and counts behind [the engine backbone refactor](../../design/ENGINE_BACKBONE_REFACTOR/README.md),
which took them out when it took the design format.

## Origin

The plan was written on 2026-07-30 on the branch `feature/physics-substrate-seams`, which has
since merged. What landed in the first wave is recorded in
`docs/archive/slop/BACKBONE_WAVE_STATUS.md`. It rests on four full surveys taken on 2026-07-30:
the runtime's public API and execution model, its build, packaging and deployment posture, every
SushiRuntime call site in SushiEngine, and SushiEngine's design-document corpus and forward
requirements. The file and line citations in the pages come from those surveys.

## Ground truth at the time

Three facts discovered in the surveys reorder every plan written before them.

**1. R1–R7 exist, and the engine does not know.** The engine's own requirement register,
`sushiengine/docs/slop/physics_system.md` §18, was corrected on 2026-07-29 to read *"R1, R2, R3
and R5 must be read as open"*: a check made against `main`, which carries only doc commits on
top of the pre-seams state. The seams landed on `feature/physics-substrate-seams` on 2026-07-28
(`800cb5c` add_untracked, `db3904b` add_reduce, `eaa390d` sized_from_device, `01c84f4` interval
boundaries, `5536b02` run_async + based_at, `d46cd7f` magazine teardown). Functional suite
154 → 190, ASan-clean. Both documents were right on their own day; together they describe a
delivery that never arrived. **The cheapest unit of value in this whole plan is a merge.**

**2. The engine has adopted part of the new surface: this fact is now half stale.** The original
"call-site count: all zero" claim does not hold. Re-counted 2026-08-17 against
`D:/Projects/sushiengine` (grep over `engine/`, `applications/`, `tests/`, excluding `build/`):

- `add_reduce`: **has callers.** The physics solver calls
  `graph_->add_reduce(*motion_, *motion_maximum_, capacity, Maximum<T>{}, T(0))` at
  `engine/domain/physics/include/SushiEngine/physics/solver/runtime_graph_builder.hpp:1240`,
  wrapped by the execution seam at
  `engine/foundation/execution/include/SushiEngine/execution/backend/runtime_backend.hpp:377`
  (Graph) and `:479` (Region).
- `DynamicGraph`: **has callers.** `SushiRuntime::API::DynamicGraph` is wrapped by the engine's
  own `DynamicGraph` in `runtime_backend.hpp:513-569`.
- `RunReport::node_timings`: **has callers.** `physics_node_timings_from_report()` iterates
  `report.node_timings` at
  `engine/domain/physics/include/SushiEngine/physics/core/statistics_from_report.hpp:101`.
- `based_at*`: **has one caller**, not zero: `dynamic.and_based_at(...)` at
  `runtime_backend.hpp:138`. The plain `based_at()` spelling has none.
- Still zero: `add_segmented_reduce`, `sized_from_device`, `add_untracked`, `run_async`, and
  `SushiRuntime::API::State<T>` (the `State<...>` hits in the engine are its own domain types,
  e.g. `JointAngularState<T>`).

The hand-built stand-ins this fact listed are also mostly gone. `MOTION_SEGMENT` **does not exist
anywhere in the engine**: the two-node fixed-order reduction it belonged to has been replaced by
the `add_reduce` call above. What remains of the list is unverified here and should be re-checked
before it is acted on: the island partition and the CPU-only broadphase/narrowphase transfers
were not re-confirmed in this pass, and the file:line citations for them
(`islands.hpp:48-53`, `physics_simulation.hpp:72-79`) are stale; no `islands.hpp` was found at
that path.

`core/blas_placeholder.hpp` is **not** a duplicate to delete. Its fold operators are live: `Sum`
(line 504), `Product` (511), `Minimum` (525) and `Maximum` (532), and `Maximum<T>{}` is the
combiner passed to `add_reduce`. Deleting them breaks the build. The one member with no callers
is `fold_range` (line 555), re-exported as `placeholder::fold_range` in `core/types.hpp:111`.

**3. Where the runtime was absent, the engine routed around it, and recorded why.** Audio's
real-time mix sidesteps the runtime entirely (`audio/accelerator.hpp:31-37`). The atmosphere tier
chose Vulkan compute and wrote the reason down so it would not be re-proposed
(`atmosphere_system.md` §11). Cosmetic particles and skinning shipped as Vulkan compute, with the
runtime's SYCL kernel surviving only as a correctness oracle (`render/shaders/skinning.comp:87`).
The cross-platform plan reduces the runtime's blast radius from 52 files to ~3 behind an `Exec`
seam. Every one of these is a boundary lesson this plan takes as *input*, not as damage to undo.

## The pool decision

Measurement settled it, on 2026-08-18: instrumenting every USM entry point showed allocation
happens once per `Buffer` or `State` the caller constructs, at setup, and never during a run; 1000
steps, a reduction, and 32 regions streaming in and out all allocate nothing. The pool was deleted
in commit `6e4b87b`.
