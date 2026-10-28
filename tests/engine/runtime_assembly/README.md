# runtime_assembly tests

The tests of the `runtime_assembly` module, mirroring `modules/engine/runtime_assembly/`.
`tests/engine/runtime_assembly/CMakeLists.txt` builds them into `sr_runtime_assembly_test_objects`, linked into `sr_engine_tests`. `sr
test` runs them with the rest; `sr run sr_engine_tests -- --gtest_filter='<pattern>'` runs
one program alone.

A section is named after a test file, and a subsection after the test, helper or topic it describes.

## test_context_factory.cpp

The file tests make_context(): a context over every device, with its settings.

| Test | What it proves |
| --- | --- |
| `BuildsAContextWithADevice` | The context is built and holds at least one device. |

## test_scheduler_factory.cpp

The file tests make_scheduler(): a scheduler over a context that runs what it is given.

| Test | What it proves |
| --- | --- |
| `TheSchedulerRunsWhatItIsGiven` | The scheduler runs every task submitted, then stops. |
