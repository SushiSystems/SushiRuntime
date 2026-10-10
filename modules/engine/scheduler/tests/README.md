# scheduler tests

The tests of the `scheduler` module, built into one program, `sr_scheduler_tests`, which
`modules/engine/scheduler/CMakeLists.txt` declares. `sr test` runs it with the rest;
`sr run sr_scheduler_tests -- --gtest_filter='<pattern>'` runs it alone.

A section is named after a test file, and a subsection after the test, helper or topic it describes.

## test_event_polling.cpp

`ActiveEventList` must never drop an in-flight event. A dropped event leaves the successors of a
task untriggered and deadlocks the run. The checks pin the inline-then-spill contract on the data
structure, without a live scheduler: they push far past the inline capacity, remove in every order,
and assert that nothing is lost and that swap-with-last identity holds across the boundary between
inline and spilled storage.

### `RemoveAtDrainsEveryEventExactlyOnce`

Always removing index 0 relocates the tail to the front on every step, which crosses the boundary
between inline and spilled storage repeatedly.

### `RemoveAcrossInlineSpillBoundary`

The only spilled entry is the last one, with tag N. Removing an inline slot must move that tail into
the freed slot and shrink the spill to empty.

## test_range_task.cpp

The file covers `RangeTask::split`, the primitive for divisible workloads: the grain threshold, that
`split()` yields a distinct sub-task while work remains, that repeated splitting shrinks the owner
and terminates, and that `split()` returns `nullopt` below the grain. All tests are deterministic
and run on the host.

### `SplitHandsOffDistinctChunksUntilExhausted`

The test keeps every chunk alive. An address that was freed and reused could otherwise look like a
duplicate, and the test could not assert that the chunks are distinct.

## test_thermal_rebalance.cpp

The file drives the real `ThermalProxyStrategy` with a fake telemetry and a fake migrator. The rule
of the strategy is to migrate work away from a throttled worker, whose queue backs up while its
completion crawls, toward the least-loaded one, and to leave alone a long queue that still drains
fast. `get_completion_rate()` is cumulative, so on the first `evaluate()` the delta equals the
reported total.

## test_work_stealing_deque.cpp

The Chase-Lev deque must never drop a task on overflow. A dropped task leaves the `in_degree` of its
successors above zero for good and deadlocks the run. The checks drive the ring past its fixed
capacity, single-threaded for exactness and with concurrent stealers for the lock-free path, and
assert that every pushed task comes back out exactly once.

## test_workload_coordinator.cpp

The file covers the heartbeat loop of `WorkloadCoordinator`. Fakes for all three collaborators
(telemetry, migrator and strategy) keep the test deterministic and on the host. The loop must drive
the strategy, start and stop cleanly, and swap strategies at runtime.

### `wait_until_at_least`

`wait_until_at_least` spins until the value reaches the target or the budget elapses. The tests use
it to synchronize their assertions with the background threads of the coordinator.
