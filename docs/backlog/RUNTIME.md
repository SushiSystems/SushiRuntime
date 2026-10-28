# Runtime

## Remaining work

| ID | Pri | State | Title | Done when | Blocked by | Source | Opened |
| --- | --- | --- | --- | --- | --- | --- | --- |

## Known issues

| ID | Origin | Symptom | Workaround | Fix | Source | Opened |
| --- | --- | --- | --- | --- | --- | --- |
| KI-1 | own | A host read of a Shared buffer during a run faults on Windows with CUDA | keep any value read mid-run in `Residency::Host` | RW-4 |  | 2026-10-19 |
| KI-2 | own | A `Buffer` moved after it was recorded leaves the graph on the old address | do not move a `Buffer` a graph recorded; see the `@warning` on its move constructor | RW-7 |  | 2026-10-19 |
| KI-3 | own | A `cancel()` that races the end of its run can cancel the next run | wait for the run before starting the next one after a cancel | RW-28 |  | 2026-10-19 |
| KI-4 | own | A `DynamicGraph` keeps the regions it dropped until it is destroyed | run the graph once more, or destroy it | RW-30 |  | 2026-10-19 |
| KI-6 | own | A distributed worker runs one kernel at a time | run one worker per device | RW-17 |  | 2026-10-24 |
| KI-8 | own | `Cluster::detach()` during a run leaves the stopped coordinator installed | call `detach()` between runs | RW-25 |  | 2026-10-19 |
| KI-9 | own | Any host that reaches the master's port can hold every peer slot | keep the port behind a firewall | RW-19 |  | 2026-10-19 |
| KI-10 | own | An op recorded on a dynamic-graph region always runs on the master | record the op on a `Graph` | RW-24 |  | 2026-10-19 |
| KI-11 | own | Frames between master and worker are not protected after the handshake | run the cluster on a trusted network | RW-18 |  | 2026-10-19 |
| KI-17 | own | `StateStopsOnAWindowPastTheEnd` fails under four parallel test jobs | the test is quarantined; run it alone | RW-67 |  | 2026-10-26 |
