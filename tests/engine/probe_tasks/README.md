# probe_tasks tests

The tests of the `probe_tasks` module, mirroring `modules/engine/probe_tasks/`.
`tests/engine/probe_tasks/CMakeLists.txt` builds them into `sr_probe_tasks_test_objects`, linked into `sr_engine_tests`. `sr test` runs
them with the rest; `sr run sr_engine_tests -- --gtest_filter='<pattern>'` runs one program
alone.

A section is named after a test file, and a subsection after the test, helper or topic it describes.

## test_atomic_counter_task.cpp

The file tests AtomicCounterTask: a host task that counts each run and never splits.

| Test | What it proves |
| --- | --- |
| `EachRunAddsOne` | Each run adds one, and the task is a host task that does not split. |

## test_order_recorder.cpp

The file tests make_order_recorder(): the callable appends its id under the mutex.

| Test | What it proves |
| --- | --- |
| `AppendsItsIdWhenCalled` | Each recorder appends its own id, in the order they are called. |

## test_priority_tracking_task.cpp

The file tests PriorityTrackingTask: it reports its priority and records it when it runs.

| Test | What it proves |
| --- | --- |
| `RecordsItsPriorityWhenItRuns` | The task records its priority in run order. |
