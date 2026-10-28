# region

The module holds `BufferRegion`, the byte window of one allocation that a node declares it reads
or writes, `ResourceID`, the address that names the allocation, and the liveness token that tells
whether the allocation still exists. It is header-only, and its headers are installed.

It depends on the C++ standard library and on no other module of the repository.

## Public entry points

| Header | Holds |
| --- | --- |
| `buffer_region.hpp` | `BufferRegion` and `ResourceID` |
| `liveness.hpp` | `Liveness`, `LivenessToken`, `LivenessObserver` and `make_liveness()` |

Every name is declared in `SushiRuntime`. A consumer builds a region from a `Buffer` and passes it
to a recording verb; the dependency tracker of the engine orders nodes by it. The module sits in
the foundation tier so that both sides include the one definition.

## buffer_region.hpp

### BufferRegion

The dependency tracker keys ordering on regions, not bare pointers, so it can tell apart two
sub-ranges of the same allocation (an array's two halves, a grid's interior and its halo). Two
accesses are ordered if and only if their regions overlap; disjoint sub-ranges of the same buffer
stay parallel, and distinct allocations never serialize.

`BufferRegion(pointer)` names the whole buffer at that base. The constructor is explicit, so a
pointer becomes a region only where the code spells the type. A whole region overlaps any region of
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

## liveness.hpp

### Why the liveness token exists

A LivenessToken is a token a data handle owns and a graph weakly observes.

A fluent graph records the base pointers of the Buffers and States its tasks touch, but it does not
own them. If a handle is destroyed before the graph runs, those pointers dangle and a run is silent
undefined behaviour. A handle holds one shared LivenessToken; the graph keeps a weak observer of it
per recorded resource. When the handle dies the token expires, so the graph can detect a stale
resource at run time and fail cleanly instead of dispatching against freed memory. The token carries
no state: its existence is the signal, so observing it costs a weak_ptr lock and never touches the
dependency hot path.

## Tests

`tests/foundation/region/` builds `sr_region_test_objects`, linked into `sr_foundation_tests`.
