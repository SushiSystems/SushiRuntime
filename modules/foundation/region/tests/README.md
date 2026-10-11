# region tests

The tests of the `region` module, built into one program, `sr_region_tests`, which
`modules/foundation/region/CMakeLists.txt` declares. `sr test` runs it with the rest;
`sr run sr_region_tests -- --gtest_filter='<pattern>'` runs it alone.

A section is named after a test file, and a subsection after the test, helper or topic it describes.

## test_buffer_region.cpp

The file covers the overlap and containment queries of `BufferRegion`. Two integers stand in for two
allocations, `first_allocation` and `second_allocation`. Only their addresses are used.

### `ADefaultRegionOverlapsNothing`

A default region is not valid and overlaps no region, itself included. Two regions with a null base,
of 8 bytes at offsets 0 and 4, do not overlap.

### `ADefaultRegionContainsNothingAndIsContainedByNothing`

A default region contains neither another default region nor a region of an allocation. The whole
allocation does not contain a default region.

### `RegionsOfOneAllocationOverlapWhenTheirIntervalsIntersect`

The whole allocation overlaps every region of it. `[0, 8)` overlaps `[4, 12)`, and `[4, 12)` overlaps
`[8, 16)` and the tail from offset 12 to the end. The intervals are half-open, so `[0, 8)` overlaps
neither `[8, 16)` nor the tail.

### `RegionsOfDifferentAllocationsNeverOverlap`

Regions of two different allocations neither overlap nor contain each other.

### `ARegionContainsWhatLiesEntirelyInside`

The whole allocation contains itself and `[0, 8)`. `[0, 8)` contains itself and `[2, 6)`. `[0, 8)`
contains neither the whole allocation nor `[4, 12)`, and `[2, 6)` does not contain `[0, 8)`.
