# api tests: graph

The tests of `api/graph/`, in one folder per subject. They are part of `sr_api_tests`;
[the page of the program](../README.md) says how to run it.

| Folder | Tests | Notes |
| --- | --- | --- |
| `recording/` | What a `Graph` records: the `add()` verbs, `Recorder`, `NodeOptions` and the plan | [recording/README.md](recording/README.md) |
| `regions/` | `DynamicGraph` and `Region`: adding, dropping and composing regions | [regions/README.md](regions/README.md) |
| `stepping/` | Running a recorded graph: `run()`, `run_async()`, `RunHandle` and the report | [stepping/README.md](stepping/README.md) |

`refusing_fill.hpp` is in this folder because a test of `regions/` and a test of `stepping/`
include it.

## refusing_fill.hpp

### `RefusingFill`

`Testing::RefusingFill` is a host task that fills a range, and whose copy constructor throws a
`std::runtime_error` while a flag the test holds is set. Recording moves the task, and every
build of a plan copies it into the node, so the flag decides whether a build throws.
`test_run_driver.cpp` and `test_region_commit.cpp` include the header by its relative path.
