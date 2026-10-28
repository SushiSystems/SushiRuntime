# KI-1 A host read of a Shared buffer during a run faults on Windows with CUDA

The row of this entry is in `docs/backlog/RUNTIME.md`. This file holds what did not fit the row.

- Origin: own
- Where: `modules/surface/api/include/SushiRuntime/api/data/buffer.hpp`
- Cause: the host touches a `Residency::Shared` buffer while a kernel runs on the same GPU
- Recognise: the process stops with `0xC0000006` during a print, a save or a mid-run read
- Workaround: keep any value read mid-run in `Residency::Host`
- Fix: RW-4
