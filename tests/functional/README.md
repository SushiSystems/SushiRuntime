# Functional tests

The functional suite is split into three folders. Each folder has a README that records what
its tests prove and why they are built the way they are.

| Folder | Holds | Reasoning |
| --- | --- | --- |
| `unit` | Tests of one component at a time: the deque, the dependency tracker, the registries, the planners | [unit/README.md](unit/README.md) |
| `integration` | Tests that run a graph through a `Runtime` or the execution engine | [integration/README.md](integration/README.md) |
| `regression` | One sentinel test per defect that was found and fixed | [regression/README.md](regression/README.md) |

A source comment keeps one line. The `@see` line in a file's header gives the path of its
folder's README, and a `//` line that ends in `see README.md` refers to that same file.

The test names in the READMEs are the second argument of the `TEST` macro, so a heading can be
found in its source file by a plain search.
