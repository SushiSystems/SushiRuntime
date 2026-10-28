# Distributed layer

## Remaining work

| ID | Pri | State | Title | Done when | Blocked by | Source | Opened |
| --- | --- | --- | --- | --- | --- | --- | --- |
| RW-17 | P2 | open | Run a worker's kernels through its scheduler and overlap them | a worker dispatches through its runtime's scheduler, and two dispatches on two devices overlap in a test |  |  | 2026-10-24 |
| RW-18 | P2 | open | Protect frames after the handshake | every frame is encrypted or carries a MAC |  |  | 2026-10-19 |
| RW-19 | P2 | open | Cap the peers that have not registered | unregistered connections have a cap of their own below `max_peers` |  |  | 2026-10-19 |
| RW-20 | P2 | open | Remember every nonce of a connection | `MasterLink` checks every `CHALLENGE` on a connection, the sixth `HELLO` included |  |  | 2026-10-19 |
| RW-21 | P2 | unmeasured | Measure the per-dispatch allocation and the release observer | the TCP benchmark shows whether `PendingTasks::insert()` needs a pooled entry and the release observer a lock-free test |  |  | 2026-10-19 |
| RW-22 | P2 | unmeasured | Run the distributed verification | 100 repeats of the TCP fault suite, the TCP rows of the distributed benchmark and an ASan run of the layer have passed |  |  | 2026-10-19 |
| RW-23 | P2 | open | Read cluster endpoints from a nodes file | a cluster's addresses can come from a file, not only from flags and the environment |  |  | 2026-10-19 |
| RW-24 | P2 | open | Offer an op recorded on a dynamic-graph region to a worker | the dynamic composer stamps offloadable nodes as `TaskGraph::compile` does |  |  | 2026-10-19 |
| RW-25 | P2 | open | Release the stopped coordinator at `detach()` | a `detach()` during a run leaves no coordinator installed (QC-84) |  |  | 2026-10-19 |
| RW-65 | P2 | open | Carry out the runtime's share of layered execution | tasks R1 to R13 of `docs/design/LAYERED_EXECUTION/README.md` are closed |  | `docs/design/LAYERED_EXECUTION/README.md` | 2026-10-20 |

## Known issues

| ID | Origin | Symptom | Workaround | Fix | Source | Opened |
| --- | --- | --- | --- | --- | --- | --- |
