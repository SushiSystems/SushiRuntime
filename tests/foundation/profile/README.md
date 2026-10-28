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
| `AUnitCompiledAtLevelZeroWritesNothing` | Level 0 writes nothing. |
| `AUnitCompiledAtLevelThreeWritesEveryZone` | Level 3 writes a zone of each level. |
| `AClosedCollectorWritesNothing` | A closed collector keeps even a level-3 unit silent. |
| `ThisUnitCompilesAtTheDefaultLevel` | The compiled level of a unit that sets none is the build's `SR_PROFILE_LEVEL`. |

## test_profile.cpp

The file tests the `SR_ZONE` family at the level this build compiles, and the two thread helpers of the front.

| Test | What it proves |
| --- | --- |
| `Small_Profile.EachMacroWritesItsKind` | A stage zone, a value, a counter and an instant each write their own kind. |
| `Small_Profile.TheThreadIndexIsTheCollectorsOnceTheThreadIsRegistered` | `current_thread_index()` reads the collector's index of a registered thread. |
| `Small_Profile.AThreadThatNeverRecordedReadsZero` | A thread that never recorded reads 0, which stands for unknown. |
| `Small_Profile.NamingAThreadPutsTheNameInTheDrain` | `name_this_thread()` registers the thread and the drain carries the name. |
| `Small_ProfileLockWait.TheLockIsReleasedAtTheEndOfItsScope` | A lock taken through `SR_LOCK_WAIT` is released at the end of its scope. |

## test_profile_keys.cpp

| Test | What it proves |
| --- | --- |
| `EveryKeyIsListedOnce` | `K_ALL_KEYS` holds no empty name and no name twice. |
| `TheNamesAreSpelledAsTheProfileReaderExpects` | The source, track and layer names are the strings a profile reader looks for. |
