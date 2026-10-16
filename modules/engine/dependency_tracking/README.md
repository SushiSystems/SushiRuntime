# dependency_tracking

The module holds the lock-free tracker that turns the reads and writes of the nodes added to a graph
into edges: read after write, write after read and write after write, per byte range of each
allocation. It is an internal module: its headers reach the modules that name it and the tests, and
are never installed. Its names are in `SushiRuntime::Internal::Graph`.

It depends on `core`, `task` and `region`, and its source on `memory`.

## Public entry points

| Header | Holds |
| --- | --- |
| `dependency_tracker.hpp` | `DependencyTracker` |

## dependency_tracker.hpp

A private header: it sits in `source/`, nothing outside the module includes it, and it is
not installed.

### get_shard_idx

shard_index() shards on the owning allocation (its base), not the sub-region, so every access to
one allocation lands in the same chain and can be compared by interval overlap. It applies a fast
SplitMix64 bitwise hash to distribute allocations uniformly.

### ~DependencyTracker

compile() normally calls reset() explicitly on its success path. A graph destroyed without ever
compiling, or one whose compile() failed and was then abandoned, would otherwise leak its per-access
AccessNodes. reset() is idempotent, so running it in the destructor as well is safe.

## dependency_tracker.cpp

### resolve

`resolve()` pushes the access onto the shard's lock-free history and then walks backwards,
ordering the node after every earlier access whose region overlaps. That resolves
read-after-write, write-after-read and write-after-write hazards. A write is ordered after
every overlapping prior access, read or write. A read is ordered after every overlapping
prior write, and readers stay parallel to each other.

The walk stops early only at a prior writer that fully covers the accessed region. Every
earlier access to that range is then already ordered behind that writer transitively, so it
needs no direct edge. A writer that overlaps only part of the region does not end the walk.
An earlier access could touch the part it leaves uncovered, and stopping there would drop a
real edge and race silently on sub-regions.

### reset

`reset()` retires the access history of every shard. It is safe because it is called only
when graph compilation is completely done.

### add_dependency

`add_dependency()` checks the inline successor list and then the spillover list for the
edge before adding it, so an edge is recorded once. It registers the successor through the
atomic successor list, without a lock, and raises the target's in-degree atomically.
