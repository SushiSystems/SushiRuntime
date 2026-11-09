# profile tests

The tests of the `profile` module, mirroring `modules/foundation/profile/`.
The test list of the module builds them into `sr_profile_test_objects`, linked into `sr_foundation_tests`. `sr test` runs them
with the rest; `sr run sr_foundation_tests -- --gtest_filter='<pattern>'` runs one program alone.

A section is named after a test file, and a subsection after the test, helper or topic it describes.
The tests link SushiProfiler, which the module itself exposes to the runtime's other modules.

## test_profile_gate.cpp

The file tests that the `SR_ZONE` family writes a zone only at or below the level its translation unit
compiles. Four small units compile the same three calls at levels 0 to 3 (`gate_unit_level_zero.cpp`,
`gate_unit_level_one.cpp`, `gate_unit_level_two.cpp`, `gate_unit_level_three.cpp`, declared in
`gate_units.hpp`), and the test file calls each against an open collector and reads back the site names
this thread wrote. A level is set by a macro defined before the front is included, so each level needs a
unit of its own, as `tests/foundation/log/test_log_gate.cpp` does for logging.

The disabled build (`SR_ENABLE_PROFILER` off) is not tested here: the front defines its helper functions
differently in that build, so a unit compiled that way would break the one-definition rule inside one
program. `PF-15` checks that build by configuring the tree with the option off.

| Test | What it proves |
| --- | --- |
| `AZoneAtTheCompiledLevelWritesOneEvent` | A stage zone and a value compiled at level 1 each write one event. |
| `AZoneAboveTheCompiledLevelWritesNothing` | A fine zone at level 1 and a detail zone at level 2 write nothing and register no site. |
| `AFineZoneOfAUnitCompiledAtLevelTwoWritesOneEvent` | The control of the test above: a fine zone at level 2 writes one event. |
| `AUnitCompiledAtLevelZeroWritesNothing` | Level 0 writes nothing. |
| `AUnitCompiledAtLevelThreeWritesEveryZone` | Level 3 writes a zone of each level. |
| `AClosedCollectorWritesNothing` | A closed collector keeps even a level-3 unit silent. |

## test_profile.cpp

The file tests the `SR_ZONE` family at the level this build compiles, and the two thread helpers of the front.
`ProfileTest` is the fixture base that opens and empties the collector; `Small_Profile` and `Medium_Profile` derive from it, the second for the tests that start a thread.

| Test | What it proves |
| --- | --- |
| `Small_Profile.AZoneWritesAZoneEvent` | A stage zone writes one event of the zone kind. |
| `Small_Profile.AValueWritesAValueEvent` | A value writes one event of the value kind. |
| `Small_Profile.ACounterWritesACounterEvent` | A counter writes one event of the counter kind. |
| `Small_Profile.AnInstantWritesAnInstantEvent` | An instant writes one event of the instant kind. |
| `Small_Profile.TheThreadIndexIsTheOneItsDrainedEntryCarries` | `current_thread_index()` of a registered thread equals the index its drained entry carries. |
| `Medium_Profile.NamingAThreadPutsTheNameInTheDrain` | `name_this_thread()` registers the thread and the drain carries the name. |

## test_collector_lock.cpp

The guard behind `SR_LOCK_WAIT`, on a `BusyMutex` that refuses a set number of `try_lock` calls, and a collector of
its own on a `ManualTickSource`. No test starts a thread; the lock a real contention takes is tested in
`tests/surface/profiling` (decision 70).

| Test | What it proves |
| --- | --- |
| `Small_CollectorLock.TheLockIsReleasedAtTheEndOfItsScope` | A lock taken through `SR_LOCK_WAIT` is released at the end of its scope. |
| `Small_CollectorLock.AFreeMutexWritesNothing` | A mutex granted at the first `try_lock` writes no event. |
| `Small_CollectorLock.ABusyMutexWritesOneWaitAndStillLocks` | A mutex that refuses the first `try_lock` is locked and writes one wait event. |

## test_profile_keys.cpp

The layer names are checked on the reader side, in `tests/surface/profiling`. The host track name has no
reader-side check, so the one test here states it.

| Test | What it proves |
| --- | --- |
| `TheHostTrackIsSpelledAsTheProfileReaderExpects` | The host track name is the string a profile reader looks for. |
