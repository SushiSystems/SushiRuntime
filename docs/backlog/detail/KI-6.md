# KI-6 A distributed worker runs one kernel at a time

The row of this entry is in `docs/backlog/RUNTIME.md`. This file holds what did not fit the row.

- Origin: own
- Where: `modules/extension/distributed/source/worker_agent.cpp`, `modules/extension/distributed/source/dispatch_executor.cpp`
- Cause: one exec thread calls each kernel on the queue of the device its dispatch names, outside the worker's scheduler, and the cache allocates its shared USM on device 0
- Recognise: two dispatches for two devices of one worker do not overlap
- Workaround: run one worker per device
- Fix: RW-17
