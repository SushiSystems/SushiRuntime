# log tests

The tests of the `log` module, mirroring `modules/foundation/log/`.
`tests/foundation/log/CMakeLists.txt` builds them into `sr_log_tests`. `sr test` runs them with the
rest; `sr run sr_log_tests -- --gtest_filter='<pattern>'` runs one program alone.

A section is named after a test file, and a subsection after the test, helper or topic it describes.

## test_log_level.cpp

The file tests LogLevel: the fixed numbers of its values.

| Test | What it proves |
| --- | --- |
| `LevelsRiseFromTraceToOff` | The levels are numbered Trace 0 to Off 6, in one byte, as the C interface carries them. |

## test_log_project.cpp

The file tests LogProject: the level each project keeps.

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
| `TheFieldsFollowTheDocumentedOrder` | A record is trivially copyable and its fields come in the order the C record keeps. |

## test_log_format.cpp

| Test | What it proves |
| --- | --- |
| `EachPlaceholderTakesTheNextArgument` | Each `{}` takes the next argument through `operator<<`. |
| `DoubledBracesAreLiteral` | `{{` and `}}` are written once, with and without arguments. |
| `AMismatchedCountIsIgnored` | Extra arguments are dropped and extra placeholders stay as written. |

## test_log_sink.cpp

The file tests LogSink: the record it receives, installing it, and `log_forward_sink()`. It logs
through a macro on `SR_LOG_FRONT_AT`, as a front does. The forwarded records are shaped as a second
logger sends them: project `sushidsp`, module `mixer`, thread 0 and no line.

| Test | What it proves |
| --- | --- |
| `ASinkReceivesEveryFieldOfTheRecord` | A sink is called with every field of the record it was given. |
| `ASinkIsInstalledAndRemoved` | A sink is installed, replaced by the default, and synchronous mode set. |
| `AForwardedRecordKeepsItsFieldsAndOrder` | Forwarded records keep their level, project, module, file, line, time and order among this logger's own, in both modes; a `Debug` one passes although the receiver's level is `Info`; each takes the thread number of the calling thread and a line this logger writes. |

## test_log_c_sink.cpp

The file tests the C sink through `sr_set_log_sink` and `sr_set_log_synchronous`.

| Test | What it proves |
| --- | --- |
| `TheCSinkReceivesEveryField` | The C function receives `size` equal to `sizeof(SushiLogRecord)` and every field, each string with its length. |
| `ARecordIsReadSafelyAcrossVersions` | A host whose struct ends earlier reads the fields it knows; a host whose struct has a field more copies only `size` bytes and finds that field at zero. |

## test_log_category.cpp

The file tests LogCategory: its project and module, and the level each category keeps.

| Test | What it proves |
| --- | --- |
| `ACategoryHoldsItsProjectAndModule` | A category holds the project and module it was made with, at compile time. |
| `TheLevelSetForACategoryIsReadBack` | A level set for one category is read back for it alone, and outlives a project level set later. |

## test_log_front.cpp

The file builds a front as a project built on the runtime would, with its own project and macros
on `SR_LOG_FRONT_AT`.

| Test | What it proves |
| --- | --- |
| `TheFloorFollowsTheSwitch` | `log_level_floor(false)` is Error and `log_level_floor(true)` is Debug or lower. |
| `AFrontLogsUnderItsProject` | A front's `WARN` reaches the sink as `front_test.kernel` and its `INFO` is dropped at `Warn`. |
