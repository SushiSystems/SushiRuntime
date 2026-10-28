# RW-4 Choose how a host read of Shared memory during a run is made safe

The row of this entry is in `docs/backlog/GENERAL.md`. This file holds what did not fit the row.

- State: decision
- Done when: the owner has chosen a read that waits on the owning queue, a debug-build guard that reports the access with its call site, or the documented rule that a value read mid-run lives in `Residency::Host`
