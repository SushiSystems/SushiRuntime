# log tests

The tests of the `log` module, mirroring `modules/foundation/log/`.
`tests/foundation/log/CMakeLists.txt` builds them into `sr_log_tests`. `sr test` runs them with the
rest; `sr run sr_log_tests -- --gtest_filter='<pattern>'` runs one program alone.

A section is named after a test file, and a subsection after the test, helper or topic it describes.

## test_log_level.cpp

The file tests LogLevel: the order of its values and the level the process keeps.

| Test | What it proves |
| --- | --- |
| `LevelsRiseFromOffToDebug` | Each level is more verbose than the one before, from Off to Debug. |
| `TheLevelSetIsTheLevelRead` | Set_log_level() is what log_level() returns, and restores the level. |

## test_log_sink.cpp

The file tests LogSink: the callable it holds, and that a sink can be installed and removed.

| Test | What it proves |
| --- | --- |
| `ASinkReceivesTheLevelAndTheLine` | A sink is called with the level and the line it was given. |
| `ASinkIsInstalledAndRemoved` | A sink is installed, replaced by the default, and synchronous mode set. |
