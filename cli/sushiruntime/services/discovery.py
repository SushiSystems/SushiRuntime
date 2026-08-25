"""Locate built executables under SushiRuntime's build tree.

The walk itself is shared -- see :class:`sushicore.discovery.ExecutableIndex`.
This CLI adds nothing to the skip list: sushiruntime is the bottom of
the stack and builds no sibling module in-tree.
"""

from __future__ import annotations

from pathlib import Path

from sushicore.discovery import ExecutableIndex

from .. import console

_INDEX = ExecutableIndex()

find_executables = _INDEX.find
match_by_name = _INDEX.match


def select_interactive(build_root: Path) -> Path | None:
    """Show a table of executables and prompt the user to pick one."""
    return _INDEX.select(build_root, console)
