# probe_tasks tests

The tests of the `probe_tasks` module, mirroring `modules/engine/probe_tasks/`.
`tests/engine/probe_tasks/CMakeLists.txt` builds them into `sr_probe_tasks_test_objects`, linked into `sr_engine_tests`. `sr test` runs
them with the rest; `sr run sr_engine_tests -- --gtest_filter='<pattern>'` runs one program
alone.

A section is named after a test file, and a subsection after the test, helper or topic it describes.

## test_atomic_counter_task.cpp

The file tests AtomicCounterTask: a host task that counts each run. The program's queue comes from
`tests/support/runtime_environment.hpp`, so the test makes none.

| Test | What it proves |
| --- | --- |
| `Medium_AtomicCounterTask.EachRunAddsOne` | Each run adds one to the counter. |

## test_order_recorder.cpp

The file tests make_order_recorder(): the callable appends its id under the mutex.

| Test | What it proves |
| --- | --- |
| `Medium_OrderRecorder.AppendsItsIdWhenCalled` | Each recorder appends its own id, in the order they are called. |

## test_priority_tracking_task.cpp

The file tests PriorityTrackingTask: it records its priority when it runs.

| Test | What it proves |
| --- | --- |
| `Medium_PriorityTrackingTask.RecordsItsPriorityWhenItRuns` | The task records its priority in run order. |
