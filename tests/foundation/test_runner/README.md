# test_runner tests

The tests of the `test_runner` module, mirroring `modules/foundation/test_runner/`.
The folder's CMake file builds them into `sr_test_runner_test_objects`, linked into `sr_foundation_tests`.
`sr run sr_foundation_tests -- --gtest_filter='<pattern>'` runs them alone.

## test_log_capture.cpp

The file tests `LogCapture`, the sink the runtime's tests share. The tests log under the module
`capture_test` of the runtime's project.

| Test | What it proves |
| --- | --- |
| `Small_LogCapture.RecordsOnlyWhatTheLevelPasses` | A `Warn` record arrives with its level, project, module and message; a `Debug` record does not. |
| `Small_LogCapture.RestoresTheSinkAndTheLevelOnDestruction` | After the destructor the project has the level it had and a new capture starts empty. |
