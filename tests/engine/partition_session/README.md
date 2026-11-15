# partition_session tests

The tests of the `partition_session` module, mirroring `modules/engine/partition_session/`.
`tests/engine/partition_session/CMakeLists.txt` builds them; `sr test` runs them with the rest.

`rebalancing_parts.hpp` holds the rebalancing tests' builders: `runtime_factories()` gives the list
scheduler and the floor, as the runtime does; `speed_store()` builds a `ProfileStore` over one
`round_profile()` per speed; `session_parts()`, `live_session()` and `replay_session()` build a session
that plans live or from a journal; `registry_parts()` builds the parts of a `PartitionSessions`;
`step_dispatches()` gives one dispatch per listed device; and `measure_step()` adds one measured step of
the plan in force to a session.

## test_rebalance_decision.cpp

`Small_RebalanceDecision` is one row per case against a plan modelled at one second and a floor at 1.5:
nothing measured or a time within the 10% margin keeps; a slower device replans; a step above the
floor tries the floor unless the floor is in force or the pair was tried; a floor on trial stays when it
measured faster and leaves when it did not.

## test_placement_journal.cpp

Appending numbers the entries in order. Parsing the text gives the same entries and writing them gives
the same bytes; an unknown reason is refused with its line. `Medium_PlacementJournal` appends to a file,
which holds exactly the text of the entries and reads back to them.

## test_journal_replay.cpp

A session over three devices partitions, replans for a throttled device and tries the floor. Every
journal entry replays to the assignment it records, a journal read back from its text gives the same
bytes and the same placements, and another problem is refused with `InvalidArgument`.

## test_graph_partition_problem.cpp

`Small_GraphPartitionProblem` builds a writer of a planned buffer and a `FixedToHome` reader that writes
a buffer device 1 owns: the owned buffer keeps its device and the other is planned, the reader waits on
the writer for the bytes it reads, the reader stays on its owner's device, a node that declares a split
carries its axis and extent, and a `Borrowed` buffer is left out.

## test_partition_apply.cpp

`Small_PartitionApply` partitions a three-node chain of planned buffers over two devices: each node is
put on its assigned device, which is the home the assignment gives the buffer it writes first. An
assignment of another graph, or one naming a device the problem lacks, is refused and changes no node.
A cut node gets each part on the index of its device with its byte ranges; a part on a device the
problem lacks is refused and changes no node; a node an earlier partition placed counts as free in
the next problem.

## test_partition_ledger.cpp

Figures come per step: the measured step, each device's dispatch time and the copies into a device by
path; a copy into a space with no device is left out, and a ledger with no step measures nothing.
`take_horizon()` gives the dispatches since the last take, once.
`Medium_PartitionLedger` writes a profile file: a second run reads the throughput the first one folded,
and a ledger with nothing unfolded leaves the file alone.

## test_partition_session.cpp

A session with no plan reports nothing and drops a run. An unfitting job is refused with each pool's
shortfall, in the error and in the report. The report of the plan in force carries the measured step,
each device's measured time and the floor's step; a run of another problem is dropped and counted; a new
plan starts an empty ledger; and the report counts only the copies made since its plan was put in force.

Rebalancing runs over two equal devices with one independent node each. A device measured at five
times its model loses its node at the first horizon, and the horizon leaves the measured throughput in
the profiles in memory. A step three times the floor's model, with every device at its model, puts the
floor on trial and keeps it at the second horizon; a floor that measures slower gives the plan back and
is not tried again. Every plan put in force is journalled and raises the generation. A replaying
session puts each journalled plan in force at the horizon it was made at, an entry with no horizon at the
next one, and refuses another problem naming the entry.

## test_partition_sessions.cpp

A registry with no profiles and no journal opens nothing. Two graphs keep their own plans and reports,
and the runtime's report is the one of the graph that partitioned last. A run of one graph handed to the
other's session is dropped there. A journal is replayed by the graph whose first problem it opens with;
a journal given after that graph opened, or one with no first partition, is refused. A graph's journal
file is named by its first problem hash and its occurrence. `Medium_PartitionSessions` writes two
journals for two graphs, each holding its own plan.

## test_live_plan_source.cpp

The first plan is the scheduler's answer priced by the profiles in memory; a horizon of a throttled device
replans and folds into the shared profiles; a source plans with what another session folded after it was
built.

## test_journal_plan_source.cpp

The first plan is the one the first entry records; a horizon no entry names keeps the plan; the horizon
an entry names puts its plan in force with its reason; a first plan from an entry of another reason, or
from an exhausted journal, is refused.

## test_profile_store.cpp

A store over no model holds none; a fold replaces the model every reader sees and leaves the old one
whole; a fold of nothing keeps the model; two threads folding at once both reach the model.

## test_transfer_path_tally.cpp

Copies are counted by destination and `TransferPathID`, two paths of one name apart, each copy reaches the next observer, and `clear()` forgets
every tally.

## compile_partition_session_log_category.cpp, compile_partitioner_factory.cpp and compile_plan_source.cpp

Each file includes one header alone.
