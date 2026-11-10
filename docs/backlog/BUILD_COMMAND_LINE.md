# Build command line

## Remaining work

| ID | Pri | State | Title | Done when | Blocked by | Source | Opened |
| --- | --- | --- | --- | --- | --- | --- | --- |
| RW-133 | P2 | open | Register the cases of each test program as CTest shards per size | Each program runs its Small_, Medium_ and Large_ cases in shards writing XML to test_results/gtest; small and medium run in 600 s |  | `cmake/SushiRuntimeTests.cmake` | 2026-11-10 |

## Known issues

| ID | Origin | Symptom | Workaround | Fix | Source | Opened |
| --- | --- | --- | --- | --- | --- | --- |
| KI-13 | own | Configuring leaves empty `CXX-DetectStdlib-*.h` files in the root | delete them | RW-56 |  | 2026-10-19 |
