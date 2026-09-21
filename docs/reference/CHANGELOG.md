# Changelog

## Unreleased

- 2026-09-21 — Fixed the late-bound `add_reduce` overload's and `add_prefix_scan`'s zero-capacity emits reporting `WIDTH_UNKNOWN` (`graph.hpp`).
- 2026-09-21 — Fixed the scheduler path snapshotting dispatch widths with profiling off (`scheduler.cpp`).
- 2026-09-21 — Documented `width_last_node`'s single-threaded-compilation precondition (`task_graph.hpp`).
- 2026-09-22 — Described this module in its own `sushi-module.toml`, so `hub` knows it from the checkout (`sushi-module.toml`).
- 2026-09-22 — Took `sushicore` from PyPI instead of a sibling checkout (`cli/pyproject.toml`).
