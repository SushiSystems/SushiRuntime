# partition_session

The module keeps the partitioning of each graph of a runtime over its runs: the problem of a recorded
graph, the assignment put on the graph, one session per graph with its plan in force, its ledger and
journal, the two sources a session takes its plans from, the profiles every session shares, the
rebalancing decision and the replay of a journal. It is an internal module: its headers reach the modules
that name it and the tests, and are never installed. Its names are in
`SushiRuntime::Internal::Coherence`.

It depends on `core`, `handle`, `log`, `memory`, `report`, `task`, `transfer`, `device_profile`
and `partitioning`, and its sources on `coherence` for `byte_range()`.

## Public entry points

| Header | Holds |
| --- | --- |
| `graph_partition_problem.hpp` | `make_graph_partition_problem()` and `GraphPartitionProblem`, the problem of a recorded graph |
| `partition_apply.hpp` | `apply_partition()`, which puts the movable nodes where an assignment says |
| `partition_ledger.hpp` | `PartitionLedger`, the dispatches, steps and seconds of a plan's runs |
| `partition_session.hpp` | `PartitionSession` and `PartitionSessionParts`, one graph's plan in force, ledger and journal |
| `partition_sessions.hpp` | `PartitionSessions` and `PartitionSessionsParts`, which open one session per graph and hold what the sessions share |
| `plan_source.hpp` | `PlanSource`, `PartitionPlan` and `HorizonOutcome`, the port a session takes its plans from |
| `live_plan_source.hpp` | `LivePlanSource`, which plans with the partitioners and judges each horizon |
| `journal_plan_source.hpp` | `JournalPlanSource`, which answers from the entries of a placement journal |
| `profile_store.hpp` | `ProfileStore`, the one owner of the runtime's profiles in memory and in the profile file |
| `partitioner_factory.hpp` | `PartitionerFactory` and `PartitionerFactories`, which build the scheduler and the floor over a cost model |
| `rebalance_decision.hpp` | `decide_rebalance()`, `RebalanceAction`, `RebalanceState` and `K_DEFAULT_REBALANCE_MARGIN` |
| `placement_journal.hpp` | `PlacementJournal`, `PlacementJournalEntry`, `PlacementReason` and `PlannedBy`, the record of every plan put in force |
| `journal_replay.hpp` | `replay_placement()`, which answers a problem again from a journal entry |
| `transfer_path_tally.hpp` | `TransferPathTally`, `PathTally` and `PathTallies`, the copies counted per destination and `TransferPathID`, each with its path name for the report |
| `partition_session_log_category.hpp` | `K_PARTITION_SESSION_LOG_CATEGORY`, the log category of the module |

## Partitioning a recorded graph

`make_graph_partition_problem()` turns a graph's nodes into a `PartitionProblem` over the runtime's
devices, device i standing for device index i. A node's cost is its `NodeCost`, none counting as
zero, and its split is the axis, extent and regions function of its `declared_split`. A `Borrowed` or `HostOnly` buffer is left out; a buffer a device owns keeps that device, and
any other is planned. An edge joins the last earlier writer of a buffer to each reader and carries
the bytes the reader's region names; the edges are ordered by writer, then reader. A node with a
stated device, or a `FixedToHome` node whose anchor a device owns, is placed where `placement_of()`
puts it (`modules/engine/task/README.md`, "Placement rule order"); the partitioner may move every
other node. A node in the `assigned` set counts as unstated: an earlier partition of the same graph
gave it its device, so partitioning the graph again gives the same problem.

`apply_partition()` states the assigned device on each movable node, or changes nothing and returns
`InvalidArgument` when the assignment does not fit the problem. The coherence planner then
allocates each planned buffer's first instance on the device of its first writer, which is the home
the assignment gives it. Rebalancing calls the same function with a new assignment.

A node the assignment cuts gets a `Graph::PlacedSplit` (`modules/engine/task/README.md`, "The parts
of a split node"): each part's `DeviceKey` becomes its device index, and each part's read and written
`BufferRange` becomes a `BufferRegion`. A part on a device the problem does not hold is refused with
`InvalidArgument`, before any node changes. A node the assignment does not cut loses any parts it held.

`PartitionSessions` is what the runtime keeps: the `ProfileStore`, the partitioner factories, the devices
with their pools and copy paths, the `TransferPathTally` every transfer path reports to, the journals
waiting to be replayed and a weak reference to the session that partitioned last. A graph's first
problem opens its session with `open()`, and the graph's `GraphPartition` owns it, so two graphs never
share a plan, a ledger or a journal. A registry with no profiles and no journal opens nothing.

`PartitionSession` holds one graph's plan in force (a `PartitionPlan`: problem, answer and the model that
priced it), its `PartitionLedger` and its `PlacementJournal`. `partition()` takes the plan from the
session's `PlanSource`, puts it in force, starts an empty ledger and journals it; a source's error is
returned as the refusal and leaves the plan in force as it was. `measure()` adds a run only when the
problem the graph was placed by hashes to the plan in force; any other run is dropped and counted in
`dropped_runs()`. `report()` gives `make_partition_report()` of the plan in force with what its runs
measured. The copy tally counts every graph's copies, so a session reads the tallies counted since its
plan was installed and clears nothing.

`PartitionLedger::measurement()` divides by the steps added: the measured step, the summed dispatch
seconds of each device and the copies into each device by path, keyed by `TransferPathID`. `TransferPathTally` takes a lock per
copy, since paths report from several workers, and forwards each copy to the observer a caller
installed.

### Concurrency

A session belongs to one graph and is touched between that graph's runs only, so it takes no lock. Two
graphs may run at once: the profiles they share sit in the `ProfileStore`, whose every fold, in memory or
into the profile file, takes its lock, and `PartitionSessions` takes its own lock to open, replay and
remember the last session. `last_report()` reads the last session, so it is called between that graph's
runs. Copies two graphs make at the same time reach both reports, since a transfer path does not know
the graph it copies for.

## Rebalancing at a horizon

A horizon passed by a graph's run rebalances that graph's session only. `PartitionSession::rebalance()`
takes the dispatches of the horizon with `PartitionLedger::take_horizon()` and hands them, with the report
of the plan in force, to its source. `LivePlanSource` folds them into the `ProfileStore` and builds the
scheduler again over the folded model; its next `first()` also takes what another graph folded. It then
hands the report to `decide_rebalance()`, a pure function of the report, the margin and a
`RebalanceState`, in this order:

| Case | Action |
| --- | --- |
| A floor is on trial | `KeepFloor` when its measured step is below the step of the plan it replaced, else `LeaveFloor` |
| A device that holds nodes measured above its modelled time by more than the margin | `Replan`: the problem is partitioned again with the folded profiles |
| The plan is not the floor, its pair was not tried, and its measured step is above the floor's modelled step by more than the margin | `TryFloor`: `PartitionerFactories::floor` answers, and the floor runs for one horizon |
| Anything else, or nothing measured | `Keep` |

The margin is `PartitionSessionsParts::margin`, `K_DEFAULT_REBALANCE_MARGIN` (10%) unless set. `LeaveFloor`
puts the replaced plan back with the model that priced it, and the pair is not tried again until a `Replan`.
Every action but `Keep` appends one entry to the journal; every change of plan clears the ledger and the tally
and raises `generation()`. The API's region composer reports a graph whose answer moved as
`coherence_replan_due()`, and the next commit rebuilds the regions of the moved nodes. A refused replan or floor
keeps the plan in force.

## Placement journal

A `PlacementJournalEntry` records one plan put in force: its sequence, its `PlacementReason`, the
`PlannedBy` partitioner that answered, the horizon it was made at, `problem_hash()` of the problem, `assignment_hash()` of the answer,
the modelled, measured and floor step of the report that was judged (zero for `First`), the measured time
of each device, and the `DeviceProfileSet` that priced the answer. The journal is kept in memory. A
runtime with a profile file also appends each graph's entries to that graph's own file,
`PartitionSessions::journal_path()`: `<device_profile_file>.<first problem hash>.journal`, the hash in
sixteen lower-case hexadecimal digits. The hash of the graph's first problem is the graph's stable id: it
depends on the recorded nodes and the devices, never on an address or a counter. The n-th graph of a
runtime opened with the same first problem, n from 1, writes `<device_profile_file>.<hash>-<n>.journal`.
The first entry of a session starts its file, so the file holds exactly `serialise(entries())`. A failed
write is logged and the entry stays in memory.

`replay_placement()` builds the named partitioner over the entry's profiles and answers the problem; a
partitioner is a pure function of both, so the replay gives the recorded assignment, and the bytes a run
leaves follow from the placement. It refuses another problem with `InvalidArgument` and an answer of
another hash with `InvalidState`.

Live planning and replay are two plan sources behind one port. `PartitionSessions::replay()` keeps a
journal for the graph whose first problem hashes to the journal's first entry; the API reaches it through
`AdvancedControls::replay_placements()`, which reads one file per call. The session that graph opens takes
a `JournalPlanSource`, with or without a cost model; the n-th graph with that first problem takes the
n-th journal kept for it. A journal that opens with no `First` entry is refused with `InvalidArgument`,
and one given after its graph opened its session with `InvalidState`.

`JournalPlanSource::first()` takes the next entry, which must be a `First` entry; an entry of another
reason, an exhausted journal, or `replay_placement()`'s refusal is returned as the refusal, naming the
entry, and nothing is put in force. Each horizon folds nothing and judges nothing: it takes the next entry
when that entry is not a `First` one and was made at this horizon, puts the plan it replays in force (a
`KeepFloor` entry changes no plan), and returns the action the entry's reason names; a replay that fails
there is logged as an error and the plan in force stays. A horizon whose number no entry names keeps the
plan, so each change is put in force at the horizon it was made at. The session counts a horizon when a
plan with an answer is in force, the same way in both runs. An entry with no horizon, from a version 1
journal, applies at the next horizon. The replaying session journals what it puts in force as it would
have planned it.

```
placement_journal_version 2
entry 0 first scheduler
horizon 0
problem_hash 0123456789abcdef
assignment_hash 00000000000000ff
modelled_step 0
measured_step 0
floor_step 0
profiles 9
format_version 2
...
end_entry
```

| Line | Holds |
| --- | --- |
| `placement_journal_version <integer>` | The first line. The writer writes version 2. |
| `horizon <integer>` or `horizon next` | Version 2: the horizons the session had judged when the plan was put in force; `next` for an entry read from version 1. |
| `entry <sequence> <reason> <planner>` | Opens an entry. The reason is `first`, `replan`, `try_floor`, `keep_floor` or `leave_floor`; the planner `scheduler` or `floor`. |
| `problem_hash`, `assignment_hash` | Sixteen lower-case hexadecimal digits. |
| `modelled_step`, `measured_step`, `floor_step` | Seconds, 17 significant digits in the classic locale. |
| `measured_device <key> <seconds>` | One per device of the judged report, in `DeviceKey` order; a key appears once. |
| `profiles <count>` | The count of the lines that follow, which are the device profile file text of the entry's profiles. |
| `end_entry` | Closes the entry. |

A malformed line is `InvalidArgument` and names its number.

| Version | Reader |
| --- | --- |
| 1 | Reads it. Its entries have no `horizon` line, and each is upgraded in memory to an entry with no horizon, which applies at the next horizon. |
| 2 | Reads it; the current version. |
| Any other | Refuses it with `Unsupported`, naming the file's version and the versions the reader knows. |
