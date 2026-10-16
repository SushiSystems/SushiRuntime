# log tests

The tests of the `log` module, mirroring `modules/foundation/log/`.
`tests/foundation/log/CMakeLists.txt` builds them into `sr_log_tests`. `sr test` runs them with the
rest; `sr run sr_log_tests -- --gtest_filter='<pattern>'` runs one program alone.

A section is named after a test file, and a subsection after the test, helper or topic it describes.

## test_log_level.cpp

| Test | What it proves |
| --- | --- |
| `LevelsRiseFromTraceToOff` | `LogLevel` is SushiLog's, numbered Trace 0 to Off 6. |

## test_log_project.cpp

| Test | What it proves |
| --- | --- |
| `TheLevelSetIsTheLevelRead` | `set_log_level(project, level)` is what `log_level(project)` and its categories return. |
| `OneProjectLeavesAnother` | Setting one project leaves another, even for a module of the same name. |

## test_sushiruntime_log_project.cpp

| Test | What it proves |
| --- | --- |
| `TheProjectIsTheRuntimeReadFromItsVariable` | `K_SUSHIRUNTIME_LOG_PROJECT` is `sushiruntime` with `SR_LOG_LEVEL`, at compile time. |

## test_log_record.cpp

| Test | What it proves |
| --- | --- |
| `TheRecordIsSushiLogs` | `LogRecord` is SushiLog's record type. |

## test_log_format.cpp

| Test | What it proves |
| --- | --- |
| `EachPlaceholderTakesTheNextArgument` | Each `{}` takes the next argument through `operator<<`. |
| `DoubledBracesAreLiteral` | `{{` and `}}` are written once, with and without arguments. |
| `AMismatchedCountIsIgnored` | Extra arguments are dropped and extra placeholders stay as written. |

## test_log_sink.cpp

| Test | What it proves |
| --- | --- |
| `ASinkReceivesEveryFieldOfTheRecord` | A sink is called with every field of the record it was given. |
| `ASinkIsInstalledAndRemoved` | A sink is installed, replaced by the default, and synchronous mode set. |
| `TheSinkIsSushiLogs` | `LogSink` is SushiLog's sink type. |

## test_log_c_sink.cpp

| Test | What it proves |
| --- | --- |
| `TheCSinkReceivesEveryField` | `sr_set_log_sink` installs a C function that receives `size` and every field, each string with its length. |

## test_log_category.cpp

| Test | What it proves |
| --- | --- |
| `ACategoryHoldsItsProjectAndModule` | A category holds the project and module it was made with, at compile time. |
| `TheLevelSetForACategoryIsReadBack` | A level set for one category is read back for it alone, and outlives a project level set later. |
