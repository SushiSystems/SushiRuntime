# KI-4 A `DynamicGraph` keeps the regions it dropped until it is destroyed

The row of this entry is in `docs/backlog/RUNTIME.md`. This file holds what did not fit the row.

- Origin: own
- Where: `modules/surface/api/include/SushiRuntime/api/graph/dynamic_graph.hpp`
- Cause: dropped regions are freed at the next run only
- Recognise: memory held by regions a graph dropped and never ran again
- Workaround: run the graph once more, or destroy it
- Fix: RW-30
