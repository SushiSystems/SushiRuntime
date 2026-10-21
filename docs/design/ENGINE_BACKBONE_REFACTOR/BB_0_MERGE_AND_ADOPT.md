# BB-0: Merge and adopt (the free win)

**Runtime side: done.**
1. Merged and tagged `v0.2.0`; `CMakeLists.txt` and
   `modules/foundation/core/include/SushiRuntime/core/version.hpp` both read 0.2.0.
2. TODO #33 (EventPolling `Task*`→`Node*` downcast) is closed: `Task::on_complete(ReadySink&)` +
   `record_completion()`, called from `modules/engine/scheduler/include/SushiRuntime/scheduler/event_polling.hpp:426,457`.
3. The doc truth pass ran. `TODO.md` (now the todo document in the version history) and these pages were reconciled against the tree;
   the code and architecture review document in the version history carries per-finding status.

**Engine side (filed here because the backbone-head relationship is one program).**
4. **Essentially done; the instruction as written was wrong.** The two-node reduction and
   `MOTION_SEGMENT` are gone: the solver calls `Graph::add_reduce` directly
   (`runtime_graph_builder.hpp:1240`). Do **not** delete `blas_placeholder`'s fold operators:
   `Maximum<T>{}` from that header is the combiner handed to `add_reduce`, so deleting it breaks
   the build. Only `fold_range` (`blas_placeholder.hpp:555`) has no callers and is a candidate for
   removal. `add_segmented_reduce` is still unadopted; whether the engine has a segmented case at
   all was not established in this pass.
5. Correct `physics_system.md` §18 (the "must be read as open" note) with the merge commit as
   evidence.
6. Adopt per seam, each with its instantiating TU (P5): `sized_from_device` → the P8
   broadphase→narrowphase→solver chain in one composition (retires the per-tick body-transfer
   pair); R3 + `DynamicGraph` regions → one region per island, waking/sleeping via
   `region()`/`drop()` (the partition in `islands.hpp` is already built); `run_async` → overlap
   the solve with the engine's own extract work inside the tick (bounded scope; full pipelining
   is BB-7).

**Proof.** Engine conformance suite green against tagged `v0.2.0`; substep counts bit-identical
across the reduction swap; `compile_count()==1` invariants hold; a grep proves
`blas_placeholder`'s fold operators have no remaining physics callers.
