# chain_run

**Describes:** `modules/engine/execution/source/chain_run.cpp`

Why `ChainRun`'s source behaves as it does. What ChainRun is, its zones and its errors are in the
[module README](../README.md), which links this page.

## The event buffer belongs to the plan

`ChainRun` reserves `pending_events` on the plan, not on itself. A fresh `ChainRun` binds to
the same plan every tick, so growing `graph_.chain.pending_events` once and reusing it keeps
later runs free of allocation. Reserving in the run would pay for a new buffer each time
this short-lived object is built.

## submit_node_discarding

The in-order queue carries the ordering a returned event would, and the run waits once in
`wait()`, so no caller reads a per-node event on this path.

## submit_profiled

Each submission keeps the steady reading taken before its submit call; finish() takes another after
the run's single wait, and `record_events()` hands both, with each dispatch's start and end, to the
ledger's narrow_pairing(), as the scheduler's poller does. Unlike the scheduler, this path never keeps an inline host node:
`node_kind_is_chainable` in `chain_plan.cpp` admits only a KERNEL node or a HOST node that
submits device work. An empty event here would be a rare, unexpected case, not the routine
one the scheduler guards against. The event is therefore kept unconditionally.
`record_events()` reads the timestamps through `read_dispatch_timestamps()`, across the events a body
handed over when it handed over any; an unreadable event is counted in the ledger as a dropped
dispatch.
