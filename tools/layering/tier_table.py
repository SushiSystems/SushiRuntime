# tier_table.py
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026-present Mustafa Garip & Sushi Systems
# Licensed under the Apache License 2.0. See LICENSE.
"""Reads the named lists of the CMake tier table, which the tier-table test compares with the settings."""

from __future__ import annotations

import re
from pathlib import Path

#: A CMake comment, from its hash to the end of the line.
K_CMAKE_COMMENT = re.compile(r"#.*")


def parse_tier_list_text(text: str, name: str) -> list[str]:
    """Returns the items of one set(<name> ...) list in the text of a CMake tier file, in file order.

    Raises ValueError when the text holds no such list.
    """
    match = re.search(rf"\bset\s*\(\s*{re.escape(name)}\s([^)]*)\)", K_CMAKE_COMMENT.sub("", text))
    if match is None:
        raise ValueError(f"the tier table has no set({name} ...) list")
    return match.group(1).split()


def parse_tier_list(path: Path, name: str) -> list[str]:
    """Returns the items of one set(<name> ...) list in a CMake tier file read from disk."""
    return parse_tier_list_text(path.read_text(encoding="utf-8"), name)
