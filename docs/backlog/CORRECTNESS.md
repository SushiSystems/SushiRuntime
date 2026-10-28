# Correctness

## Remaining work

| ID | Pri | State | Title | Done when | Blocked by | Source | Opened |
| --- | --- | --- | --- | --- | --- | --- | --- |
| RW-7 | P2 | open | Detect a `Buffer` moved after it was recorded | the liveness guard reports a recorded `Buffer` that moved (finding H4 of the code and architecture review document in the version history) |  |  | 2026-10-19 |
| RW-8 | P2 | unmeasured | Run the topology code on a machine with several NUMA nodes and processor groups | NUMA placement, processor groups and the POSIX affinity branch have run on such a machine (QC-02, QC-29, QC-33) |  |  | 2026-10-19 |
| RW-67 | P2 | open | Make `StateStopsOnAWindowPastTheEnd` pass under parallel test jobs | the cause of KI-17 is found, the test passes ten runs of the medium label at four jobs, and it leaves `SR_QUARANTINED_TESTS` |  |  | 2026-10-26 |

## Known issues

| ID | Origin | Symptom | Workaround | Fix | Source | Opened |
| --- | --- | --- | --- | --- | --- | --- |
