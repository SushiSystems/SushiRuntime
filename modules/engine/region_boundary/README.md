# region_boundary

The module summarises how the nodes of one region touch each shared allocation: the first node that
reads or writes each byte range on entry and the last on exit, which the region composer links
across regions. It is an internal module: its headers reach the modules that name it and the tests,
and are never installed. It is header-only, and its names are in `SushiRuntime::Detail`.

It depends on `core`, `region` and `task`.

## Public entry points

| Header | Holds |
| --- | --- |
| `boundary_access.hpp` | `compute_boundary_access()` and `Pin` |

## boundary_access.hpp

A private header of `source/`.

### Pin

A Pin is one node's participation in a base's boundary, with the exact byte intervals it touches.
Carrying the intervals, and not just the node, is what lets two regions writing disjoint slices of
one allocation stay parallel. Mapping one region to one simulation island depends on that: islands
are index ranges of a shared column, not separate allocations.

### BaseAccess

A BaseAccess is the boundary access summary of one region for one shared allocation. It is computed
once when the region is built and consulted, read-only, by the cross-region wiring of every base the
region shares. Entry pins receive incoming cross-region edges; exit pins are the sources for the
next region's edges. Each pin carries both its read and its write intervals, so the wiring can apply
the right hazard rule (RAW, WAW or WAR) to the right bytes, and two regions that only read the same
buffer still never gain an edge.

### for_each_intra_successor

for_each_intra_successor() walks a node's intra successors (inline buffer, then lock-free spillover)
and calls fn(succ) for each. It is used only at commit, single threaded, so a relaxed load of the
spillover head is fine.

### covered_by_any

covered_by_any() is true when one interval in `covers` fully contains `r`. Every byte of `r` is
then rewritten by that writer, and any later access to `r` is transitively ordered behind it.

### compute_boundary_access

compute_boundary_access() returns a region's per-base boundary summary. For each allocation the
region touches it gives every accessor node an entry pin and an exit pin, each holding the intervals
of the node that the rules below keep. A node whose intervals are all dropped has no pin. Both rules
look at direct intra neighbours only and count an interval as covered only when one interval of a
neighbour fully contains it; an interval is never split.

Entry (entry_pin()). A read of the node is dropped when a direct intra predecessor reads or writes
an interval of the base that contains it. A write of the node is dropped only when a direct intra
predecessor writes an interval that contains it. The predecessor then carries the cross-region edges
the node needs: its containing access is pinned itself, or is covered in turn by one of its own
predecessors, and the node runs after all of them. A predecessor that only reads does not cover a
write, because a write must also follow the readers of earlier regions and a reader has no edge
from them. With `p` writing `b[0,10)` and `node` reading `b[5,20)` in one region, `node` keeps a read pin
for `[5,20)`, so a writer of `b[15,20)` in an earlier region gains an edge to `node`.

Exit (exit_pin()). A read or a write of the node is dropped when a direct intra successor writes an
interval that contains it. A later region that overlaps the dropped interval overlaps the
successor's write too and is ordered behind the successor, which already runs after the node.

Both rules keep a pin when in doubt: an indirect predecessor, or two neighbours that cover an
interval only between them, leave the pin in place. That adds an edge at most. A pin dropped in
error would remove an edge and leave two tasks racing on the same bytes.

For one base the function costs O(E) to list the neighbours, with E the intra edges that leave the
base's accessor nodes, and then compares every interval of a node with every interval of each direct
neighbour. It reads the nodes of one region and nothing else.
