# observability

The module holds what a run reports about itself.

Today it holds one header, `transfer_counters.hpp`. The run report, the profiling ledger, the
event timestamps and the dispatch width still live in `include/SushiRuntime/core/`; they join
this module when the tree moves to modules.

`transfer_counters.hpp` depends on the C++ standard library only (`<atomic>`, `<cstddef>`).
