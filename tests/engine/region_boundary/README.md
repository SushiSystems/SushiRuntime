# region_boundary tests

The tests of the `region_boundary` module, mirroring `modules/engine/region_boundary/`.
`tests/engine/region_boundary/CMakeLists.txt` builds them into `sr_region_boundary_test_objects`, linked into `sr_engine_tests`. `sr test`
runs them with the rest; `sr run sr_engine_tests -- --gtest_filter='<pattern>'` runs one
program alone.

A section is named after a test file, and a subsection after the test, helper or topic it describes.

## test_boundary_access.cpp

The `Small_BoundaryAccess` cases build the nodes of one region on a `TaskGraph` with `add_native`
and read the pins `compute_boundary_access()` gives; they start no runtime.

### `AReadPastItsPredecessorsWriteKeepsAnEntryPin`

`p` writes bytes `[0,10)` and `node` reads `[5,20)`. `node` has an intra edge from `p`, yet `p` covers
only part of what `node` reads, so `node` keeps an entry pin whose read interval contains `[15,20)`.

### `AReadInsideItsPredecessorsWriteHasNoEntryPin`

`p` writes `[0,20)` and `node` reads `[5,20)`. `p` contains everything `node` reads, so the base has one
entry pin, that of `p`.

### `AWriteBehindAReadingPredecessorKeepsItsWritePin`

`p` reads `[0,10)` and `node` writes `[0,10)`. A reader takes no edge from the readers of earlier
regions, so it cannot stand in for a write: `node` keeps an entry pin with its write interval.

### `OnlyAContainingSuccessorWriteShadowsAnExit`

A write of `[5,10)` followed by a write of `[0,20)` leaves one exit pin, the later one. In the
reverse order the wide write keeps its exit pin, because the narrow one rewrites only part of it.
