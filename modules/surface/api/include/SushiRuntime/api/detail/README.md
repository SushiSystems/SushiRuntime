# api/detail

Internals of the fluent API: callable traits, the fixed-order reduction and scan plans, the per-step
machinery and report building.

The sections below record why the code is the way it is. Each one is named after a header and the
symbol or passage it explains, and that header's file block cites this page.

## callable_traits.hpp

### The three callable traits

The fluent add() overloads accept three callable shapes, and callable_traits.hpp expresses each as a
C++17 SFINAE trait over std::is_invocable_v. The C++20 concepts they replace read the same, only as
`requires`-clauses. enable_if on each add() selects exactly one. A wrong signature removes every
overload, so the compiler reports "no matching call", which is less pointed than a concept
diagnostic but still names the offending callable.

### views_current

views_current() reads each coupled-input view's current pointer into a plain tuple, host-side at
submit time. A StateView holds the live current/next pointers for the step, so dereferencing in
views_current(), and not in the kernel, lets the device lambda capture raw pointers and still follow
the swap.

### read_keys_of

read_keys_of() gathers the read keys of a tuple of State pointers, in order. It is called at compile
time so the dependency tracker labels every coupled input consistently with the canonical
single-State overload.

## fixed_order_reduce.hpp

### REDUCE_TILE

REDUCE_TILE is the single number the whole determinism guarantee rests on: the tile boundaries, and
therefore which values meet which, are a function of the element count and this constant alone. It
is deliberately not derived from the device's work-group size, the worker count, or anything else
that varies between runs or machines.

### ReduceLevel

A level folds `in_count` values into `out_count` = ceil(in_count / REDUCE_TILE) partials. Offsets
are in elements, into the reduction's scratch allocation; the first level reads the caller's input
instead of scratch and the last writes the caller's output instead of scratch, so those two offsets
are unused on those levels.

### plan_reduction

plan_reduction() repeatedly divides the count by REDUCE_TILE until one value remains and assigns
each level a disjoint scratch window. Consecutive levels therefore never alias, which is what lets
the ordinary region tracker order them with no hand-written edges. A count of zero yields no levels.
The function is pure, so the layout can be asserted in a test without a device.

### reduce_scratch_count

Every level but the last parks its partials in scratch; the last writes the caller's output, so its
window is never allocated. At REDUCE_TILE = 256 this is under half a percent of the input.

### make_reduce_scratch

make_reduce_scratch() allocates on the same device and with the same residency as the data being
reduced, so the reduction never straddles a device context and a device-resident field is not
dragged into host-addressable memory. The returned pointer owns the allocation through a deleter
that keeps the context alive, so scratch cannot outlive the runtime that owns it.

### fold_tile

fold_tile() holds the whole ordering contract in one function: the accumulator is seeded with the
tile's first value and every later value is combined onto it in increasing index order. Seeding from
the first element and not from an identity keeps the identity out of the arithmetic entirely, so a
floating-point sum is exactly ((v0+v1)+v2)+… and nothing else.

## fixed_order_scan.hpp

### plan_tiled_levels

plan_tiled_levels() reuses add_reduce's level plan so scan, radix and reduce all answer "how many
levels, which slice each folds" from one source.

### fill_live_tile_levels

fill_live_tile_levels() walks the same number of physical levels `level_count` as
plan_tiled_levels(capacity) planned, folding `live_n` at each level exactly as
plan_reduction(live_n) would. Once the running count reaches one it stays there. Levels beyond what
plan_reduction(live_n) would have produced degenerate to a single-element pass-through fold, and the
boundaries up to that point are byte-identical to plan_reduction(live_n).

It writes only through `out`, so a caller that already owns the levels (a graph's per-run resolver,
sized once at registration) can call it every run with no allocation.

### live_tile_levels

live_tile_levels() is the allocating, vector-returning wrapper over fill_live_tile_levels(). It is
fine for tests and other callers off the hot path, but a per-run resolver should call
fill_live_tile_levels() directly into scratch it already owns.

### fill_radix_plan

Digits at or above radix_digit_count(`live_key_range`) get zero work counts. Active digits alternate
between the indices and the scratch so the last active one always writes the indices.

## run_report_builder.hpp

### fill_run_report

fill_run_report() maps scheduler counters and device placement onto the public RunReport structure.
`before` and `after` are the scheduler execution metrics snapshotted before the run started and
after it completed. It writes into a caller-owned report so a steady-state tick can hand back the
same object every run: `report` is reset first, which keeps the vector storage the previous run
allocated.

### Device class attribution

fill_run_report() attributes the tasks to the device class the default queue targets. Per-task
placement and the reserved steal/byte counters are filled in as the engine exposes them.

It asks the runtime's own inventory for the device class, not SYCL. fill_run_report() runs on every
tick, including the steady-state loop that run(RunReport&) promises will not allocate, and asking
SYCL could not keep that promise portably: queue::get_device() returns a device by value, and the
cost of that construction belongs to the SYCL runtime. On oneAPI's libsycl (the Linux CI toolchain)
it allocated once per call, which is why the steady-state test measured 65 allocations over 64 ticks
there and 0 on Windows, where the intel/llvm nightly does not allocate. The device class of a queue
is fixed for the life of the context, so the answer was never worth re-deriving per tick on any
platform.

## step_machinery.hpp

### StepMachinery

Both the static Graph and the DynamicGraph composer drive the same compile-once, replay-many loop,
and both need the identical four per-step hook lists run in the identical order. StepMachinery owns
those lists, plus the State double-buffer bookkeeping that produces them. The ordering invariant is:
re-point distributed live IO, refresh late-bound controls, execute, then advance every State. It
lives in one place instead of being copied into each driver.

The building side (view_for / register_advance / register_binding_updater) is used by Graph's add()
overloads as they record tasks. The driving side (resync / before_step / after_step) is used by both
drivers. The aggregation side (reset_updaters / aggregate_from) lets DynamicGraph gather every live
region's hooks into one before a run.

### StepHook

A StepHook is a per-step hook stored without heap allocation. Its capacity is derived from the
largest hook and not written as a literal: a binding updater captures a whole Dynamic (one provider
slot per late-bound control) plus a LateBindings shared pointer, so adding a control widens the
capacity automatically instead of tripping Delegate's static_assert in every translation unit.

### view_for

view_for() returns the shared view for a State, creating it and registering its resync on first use.
Every kernel touching a State captures the same view, so one advancer keeps all references current.
Correct coupled (multi-field) updates rest on that.

### register_advance

register_advance() registers the once-per-step advance for a written State. It deduplicates, so a
State advances exactly once per step (one update rule per field).

### register_binding_updater

register_binding_updater() wires a Dynamic's providers to a node's bindings: it pushes a per-step
updater that polls when()/sized() on the driver thread and stores the results into the shared block
the kernel and scheduler read. It is skipped when the Dynamic carries no providers, because the
defaults (always enabled, full range) already hold, so an unused tag costs nothing.

### reset_updaters

reset_updaters() clears this machinery's aggregation of other machineries. A composer calls it and
then re-aggregates the live regions before a run. A region's own hooks, and the State bookkeeping
that owns the views, are untouched, since the region still owns them.

### aggregate_from

aggregate_from() references another machinery's per-step hooks from this one, so a composer drives
every live region's hooks without copying them (the hooks are move-only). The referenced machinery
must outlive every run driven through this one. It is the region's own, owned for the region's
lifetime.
