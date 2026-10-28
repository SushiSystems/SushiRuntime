# process_environment tests

The tests of the `process_environment` module, mirroring `modules/extension/process_environment/`.
`tests/extension/process_environment/CMakeLists.txt` builds them into
`sr_extension_tests`. `sr test` runs them with the rest; `sr run
sr_extension_tests -- --gtest_filter='<pattern>'` runs one program alone.

A section is named after a test file, and a subsection after the test, helper or topic it describes.

## test_environment.cpp

| Test | What it proves |
| --- | --- |
| `ReadsASetVariableAndReportsAnUnsetOne` | `read_environment("PATH")` returns text; a name nobody sets returns nothing. |

## test_stop_signal.cpp

| Test | What it proves |
| --- | --- |
| `RequestSetsTheFlagAndANewObjectClearsIt` | A `StopSignal` starts clear, `request()` sets the flag, and the next `StopSignal` starts clear again. |
| `Small_StopSignal.LogsAHandlerThatCouldNotBeInstalled` | An installer that answers `SIG_ERR` makes the constructor log one `WARN` per stop signal in the `process_environment` module; the flag still starts clear and `request()` still sets it. |

## test_process_environment_log_category.cpp

| Test | What it proves |
| --- | --- |
| `TheCategoryIsNamedAfterTheModule` | `K_PROCESS_ENVIRONMENT_LOG_CATEGORY` names the module `process_environment` in the project `sushiruntime`, at compile time. |
