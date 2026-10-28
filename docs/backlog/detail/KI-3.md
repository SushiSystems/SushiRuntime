# KI-3 A `cancel()` that races the end of its run can cancel the next run

The row of this entry is in `docs/backlog/RUNTIME.md`. This file holds what did not fit the row.

- Origin: own
- Where: `modules/surface/api/include/SushiRuntime/api/graph/plumbing/step_driver.hpp`
- Cause: the cancel request is not tied to the run it was made for
- Recognise: a run started right after a late cancel ends cancelled
- Workaround: wait for the run before starting the next one after a cancel
- Fix: RW-28
