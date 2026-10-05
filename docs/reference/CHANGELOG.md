# Changelog

## Unreleased

- 2026-10-05 — licence: Changed the licence from Apache-2.0 WITH LLVM-exception to PolyForm Noncommercial 1.0.0 and removed `docs/LICENSE` (`LICENSE`, `COMMERCIAL.md`, `NOTICE.md`, source headers).
- 2026-09-21 — Fixed the late-bound `add_reduce` overload's and `add_prefix_scan`'s zero-capacity emits reporting `WIDTH_UNKNOWN` (`graph.hpp`).
- 2026-09-21 — Fixed the scheduler path snapshotting dispatch widths with profiling off (`scheduler.cpp`).
- 2026-09-21 — Documented `width_last_node`'s single-threaded-compilation precondition (`task_graph.hpp`).
- 2026-09-22 — Described this module in its own `sushi-module.toml`, so `hub` knows it from the checkout (`sushi-module.toml`).
- 2026-09-22 — Took `sushicore` from PyPI instead of a sibling checkout (`cli/pyproject.toml`).
- 2026-09-22 — Drew `sr --help` as sushicore's grouped help page, with examples on every command (`cli/sushiruntime/cli.py`, `console.py`).
- 2026-09-22 — Took the theme's style names for the CLI's colours in place of raw Rich colour names (`services/select.py`, `services/project.py`).
- 2026-09-22 — Raised the `sushicore` lower bound to 0.4.0 (`cli/pyproject.toml`).
- 2026-10-04 — cli: Named sr setup --toolchain as the way to install a missing toolchain (`cli/sushiruntime/services/select.py`).
- 2026-10-04 — cli: Added setup, doctor, link and unlink from sushicore and stopped reading a repo-local dependencies folder (`cli/sushiruntime/cli.py`, `cli/sushiruntime/config.py`).
