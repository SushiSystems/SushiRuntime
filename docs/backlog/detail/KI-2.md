# KI-2 A `Buffer` moved after it was recorded leaves the graph on the old address

The row of this entry is in `docs/backlog/RUNTIME.md`. This file holds what did not fit the row.

- Origin: own
- Where: `modules/surface/api/include/SushiRuntime/api/data/buffer.hpp`
- Cause: the graph records the handle by address and the liveness token moves with the value
- Recognise: a run reads or writes freed memory and the liveness guard says nothing
- Workaround: do not move a `Buffer` a graph recorded; see the `@warning` on its move constructor
- Fix: RW-7
