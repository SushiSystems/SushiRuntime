# region

The module holds `BufferRegion`, the byte window of one allocation that a node declares it reads
or writes, and `ResourceID`, the address that names the allocation. It is header-only, and its
header is installed.

It depends on the C++ standard library and on no other module of the repository.

## Public entry points

| Header | Holds |
| --- | --- |
| `buffer_region.hpp` | `BufferRegion` and `ResourceID` |

Both names are declared in `SushiRuntime`. A consumer builds a region from a `Buffer` and passes it
to a recording verb; the dependency tracker of the engine orders nodes by it. The module sits in
the foundation tier so that both sides include the one definition.

## buffer_region.hpp

### BufferRegion

The dependency tracker keys ordering on regions, not bare pointers, so it can tell apart two
sub-ranges of the same allocation (an array's two halves, a grid's interior and its halo). Two
accesses are ordered if and only if their regions overlap; disjoint sub-ranges of the same buffer
stay parallel, and distinct allocations never serialize.

A bare pointer converts to the whole-buffer region implicitly. A whole region overlaps any region of
the same base, so the overlap test reduces to base-pointer equality.

### overlaps

overlaps() is the test the tracker uses to decide ordering: same base and intersecting half-open
intervals. K_WHOLE is treated as an unbounded upper end, so the comparison never overflows on the
sentinel. A region with a null base is not valid() and overlaps nothing, another null-based
region included; contains() answers false for it the same way.

### contains

contains() is true if `r` lies entirely within this region. It lets the tracker stop walking history
early: once a write that covers the accessed region is found, every earlier access to that range is
already ordered behind it transitively.

## Tests

`tests/test_buffer_region.cpp` builds `sr_region_tests`.
