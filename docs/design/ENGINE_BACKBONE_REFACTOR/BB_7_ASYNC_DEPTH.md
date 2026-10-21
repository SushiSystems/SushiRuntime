# BB-7: Async depth

`run_async()` is one step, one in flight, and `~RunHandle` blocks: deliberately minimal (R4's
scope). What the head will ask for next, in order of real demand:

1. **Deadline-aware waiting.** `RunHandle::try_wait()` exists (non-blocking poll);
   `wait_for(duration)` / `wait_until(time_point)` complete the set so a frame loop can spend
   its budget elsewhere and make a rollback decision at the deadline instead of parking on a
   condvar. Small: the completion latch is already a mutex+condvar.
2. **Pipelined steps.** Allow step N+1 to be *submitted* while N drains, bounded depth 2: the
   double-buffered plan problem (in-degree re-arming under a live DAG is the documented hazard,
   so this is plan cloning or per-step in-degree arrays, not a flag flip). The payoff is hiding
   the submit latency of small graphs, and it is what makes the engine's
   sim-tick/render-extract overlap complete rather than half (BB-1b gives the render side; this
   gives the sim side).
3. **RT-shaped submit.** Audio's indictment is specific: *"per-step submit takes a mutex and may
   allocate."* After BB-2's report reuse and the shadow-counter fast path, the remaining hot-path
   work is bounded; measure whether the single `pending_mutex` acquire per step is actually a
   problem at audio's k-block cadence before designing around it. If it is, a wait-free submission
   ring for pre-armed plans is the shape, but only the measurement can justify it. (The
   real-time *mix* stays off the runtime regardless, that decision is closed and correct.)

BB-7 is deliberately after BB-1/BB-2: overlap without interop just moves the stall, and
pipelining under a sole-tenant scheduler overlaps with the wrong threads.
