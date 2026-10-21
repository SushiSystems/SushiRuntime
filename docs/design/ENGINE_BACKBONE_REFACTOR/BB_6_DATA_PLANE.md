# BB-6: Data-plane maturation

The call-site survey's category B: things the engine built that the runtime should own. Ordered
by leverage.

1. **Batched transfer.** The engine hand-coalesces every host↔device movement (dirty-span
   staging, per-colour band scatter/gather: `runtime_graph_builder.hpp:966-991, 1132-1155`)
   because `read_range`/`write_range` are one synchronous `memcpy(...).wait()` each. Add
   `Buffer::write_ranges(span<const RangeWrite>)` / `read_ranges(...)` (one submit, many
   ranges, one wait) and graph-resident `add_upload`/`add_download` nodes so boundary transfers
   can live *inside* the composition and overlap with compute. The engine's staging layer then
   shrinks to bookkeeping (its dirty tracking is domain knowledge and stays).
2. **Growable buffers, honestly scoped.** In-place resize is impossible by construction (nodes
   capture raw pointers) and the fail-fast overflow contract is *correct*: keep both. What the
   runtime can give is the blessed reallocation path the animation evaluator hand-rolls today
   (reallocate → rebuild → recompile on every `bind_clip`): `Buffer::rebind(new_count)`
   returning a fresh allocation plus a graph-side invalidation that turns the next `run()` into
   a rebuild instead of a use-after-free. One primitive, two current call sites (animation clips,
   any future streaming table), and the failure mode changes from "silent stale pointer" to
   "explicit recompile".
3. **Report-free and reusable runs**: folded into BB-2 (the `run(RunReport&)` overloads); listed
   here because it is data-plane hygiene as much as RT hygiene.
4. **The pool decision: done, by deletion.** The choice was
   between routing the fluent handles through the allocator and demoting the pool. Measurement
   settled it (the record is in the evidence): instrumenting every USM entry point showed allocation happens once per
   `Buffer`/`State` the caller constructs, at setup, and never during a run; 1000 steps, a
   reduction, and 32 regions streaming in and out all allocate nothing. Pooling amortises
   high-frequency small allocation and there is none, and the pool's 4 KB ceiling sat below the
   buffers the runtime actually allocates, so routing through it was never viable. Deleted rather
   than demoted, since demoting hides dead code instead of removing it. `Memory::USMRegistry` now
   owns pointer ownership, which is the part that had a real invariant. Design in
   the usm allocation seam design spec in the version history.
5. **Deterministic device sort.** The engine's contact ordering rule ("sorted by
   `(body_a, body_b, feature_id)` before solving; broadphase order must never reach the solver")
   currently runs as a host sort and will otherwise be hand-built in the graph like the
   reduction was. Same argument as R2, same home: a fixed-order `add_sort(keys, values, n)`
   (bitonic or radix with layout-only ordering), WP-4's second item in all but name. Schedule
   behind BB-0 adoption: it is the next "delete two engine nodes" in waiting.
6. **`State<T>`: adopt or demote.** Zero engine references. Either the ECS's double-buffered
   columns adopt it (unlikely: the engine built chunk-level buffering with different semantics)
   or it is demoted from the front page of the API to a convenience wrapper before the freeze.
   An API frozen at v1 should not lead with a type its only real consumer never uses.
