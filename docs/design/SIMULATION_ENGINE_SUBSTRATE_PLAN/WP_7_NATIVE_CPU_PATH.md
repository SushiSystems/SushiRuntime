# WP-7: Native CPU execution path, fully abstracted

**Problem:** pure-CPU work (gameplay logic, AI, navigation, the CPU half of
broadphase) currently routes through `q.submit(host_task)` (`scheduler.cpp:561-563`),
adding SYCL submission overhead and requiring a live queue for plain C++.

**Design: one branch, not a second scheduler (L13 + maintainability principle):**
- A HOST task that does **not** submit device work runs directly on the worker
  thread instead of through `host_task`. The decision is internal to `execute_task`.
- Completion reuses the **existing** "empty event = ready inline" seam
  (`event_polling.hpp:63`, `scheduler.cpp:579`): the native task returns an empty
  event, the poller flags it ready, and successors fire through the *same*
  `trigger_successors` + ref-counting path. No new completion contract.
- **Portability preserved:** where a host task submits device work, or on a build
  without the native path, it transparently falls back to the SYCL host_task route.
- **User side is unchanged:** `g.add(...)` with a plain C++ lambda is identical;
  the runtime picks native vs SYCL.

**Files:** `scheduler.cpp` (`execute_task` branch on `ExecutionMode::HOST` +
"pure-CPU" flag on the task), `task.hpp`/`node.hpp` (a `submits_device_work` hint set
by the fluent layer or detected conservatively), no API change.

**Risks:** misclassifying a host task that *does* submit device work as "pure CPU"
would run device submission off the SYCL path: the flag must default to the safe
(SYCL) route and only opt into native when the caller asserts purity.

**Acceptance:** a CPU-only system runs without any SYCL submission (verified), its
successors release correctly, and a host task that submits a kernel still works via
the fallback.
