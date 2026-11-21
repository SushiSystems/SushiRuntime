# Build command line

## Remaining work

| ID | Pri | State | Title | Done when | Blocked by | Source | Opened |
| --- | --- | --- | --- | --- | --- | --- | --- |
| RW-153 | P1 | open | Carry the Windows ROCm route to a consumer of the runtime package | A consumer built against a --rocm runtime package finds the ROCm paths and links with lld, and the route becomes the default when present |  | `cmake/gpu/Rocm.cmake` | 2026-11-15 |
| RW-162 | P2 | open | Fail sr test when its filter selects no case, and describe --filter as a case filter | sr test --filter with no match exits non-zero, and the --filter help and example name GoogleTest case patterns |  | `cli/sushiruntime/cli.py` | 2026-11-17 |

## Known issues

| ID | Origin | Symptom | Workaround | Fix | Source | Opened |
| --- | --- | --- | --- | --- | --- | --- |
| KI-13 | own | Configuring leaves empty `CXX-DetectStdlib-*.h` files in the root | delete them | RW-56 |  | 2026-10-19 |
