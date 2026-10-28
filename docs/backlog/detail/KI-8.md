# KI-8 `Cluster::detach()` during a run leaves the stopped coordinator installed

The row of this entry is in `docs/backlog/RUNTIME.md`. This file holds what did not fit the row.

- Origin: own
- Where: `modules/extension/distributed/source/cluster.cpp`
- Cause: the poller holds the coordinator as a plain pointer
- Recognise: the coordinator stays until the next `detach()`, `attach()` or the end of the runtime
- Workaround: call `detach()` between runs
- Fix: RW-25
