# process_environment tests

The tests of the `process_environment` module, mirroring `modules/extension/process_environment/`.
`tests/extension/process_environment/CMakeLists.txt` builds them into
`sr_extension_tests`. `sr test` runs them with the rest; `sr run
sr_extension_tests -- --gtest_filter='<pattern>'` runs one program alone.

A section is named after a test file, and a subsection after the test, helper or topic it describes.

## test_environment.cpp

The tests set and remove one variable of the test process, `SR_TEST_READ_ENVIRONMENT`, through a guard that
puts back the value it found, so they are medium and name no variable the machine may lack.

| Test | What it proves |
| --- | --- |
| `ASetVariableIsReturnedWithItsValue` | `read_environment` returns the text a variable was set to. |
| `AnUnsetVariableIsNothing` | `read_environment` returns nothing for a variable that is not set. |

## test_stop_signal.cpp

The tests install through an installer they pass in, so no real process handler changes and every test is
small.

| Test | What it proves |
| --- | --- |
| `RequestSetsTheFlag` | A `StopSignal` starts clear and `request()` sets the flag. |
| `ANewObjectAfterARequestStartsClear` | A `StopSignal` made after a request starts with the flag clear. |
| `LogsAHandlerThatCouldNotBeInstalled` | An installer that answers `SIG_ERR` makes the constructor log one `WARN` per stop signal in the `process_environment` module. |
| `TheFlagStillWorksAfterARefusedInstallation` | After every installation was refused, the flag still starts clear and `request()` still sets it. |

## compile_process_environment_log_category.cpp

The file includes `process_environment_log_category.hpp` alone, so the header carries every include it
needs. The header holds a constant, which has no behaviour of its own to test.
