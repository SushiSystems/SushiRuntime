# Documentation

## Remaining work

| ID | Pri | State | Title | Done when | Blocked by | Source | Opened |
| --- | --- | --- | --- | --- | --- | --- | --- |
| RW-50 | P2 | open | Correct the `Doxyfile` comments | `.config/doxygen/Doxyfile` cites the architecture page at its path |  |  | 2026-10-19 |
| RW-51 | P2 | open | Correct the CMake comments that cite moved documents | `cmake/SushiRuntimeConfig.cmake.in`, `tests/package/CMakeLists.txt` and `cmake/SushiRuntimeRedistributables.cmake` cite documents at their paths |  |  | 2026-10-19 |
| RW-52 | P2 | open | Write `cmake/README.md` | the build functions and the tier table are described in `cmake/README.md` |  |  | 2026-10-19 |
| RW-53 | P2 | unmeasured | Verify the manual's C++ samples and CLI flags | every C++ sample compiles against the current headers and each flag in `docs/guides/CLI_GUIDE.md` matches the CLI |  |  | 2026-10-19 |

## Known issues

| ID | Origin | Symptom | Workaround | Fix | Source | Opened |
| --- | --- | --- | --- | --- | --- | --- |
| KI-14 | own | Three CMake comments cite documents by old paths | look the document up in `docs/README.md` | RW-51 |  | 2026-10-19 |
| KI-15 | own | The changelog lacks entries for three past changes | read `git log` | RW-34 |  | 2026-10-19 |
