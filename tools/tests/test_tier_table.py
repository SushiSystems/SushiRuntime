# test_tier_table.py
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026-present Mustafa Garip & Sushi Systems
# Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.
# Commercial use requires a licence from Sushi Systems.
"""Tests that the layering checker's tier table matches cmake/SushiRuntimeTiers.cmake."""

from __future__ import annotations

import re
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from layering.check_layering import (  # noqa: E402
    K_CHECKER,
    rule_declared_dependencies,
    rule_public_headers,
    rule_same_tier_includes,
)
from settings import K_SAME_TIER_EDGES, K_TIER_ORDER  # noqa: E402

K_ROOT = Path(__file__).resolve().parents[2]
K_TIERS_FILE = K_ROOT / "cmake" / "SushiRuntimeTiers.cmake"
K_COMMENT = re.compile(r"#.*")


def _cmake_list(name: str) -> list[str]:
    """Returns the items of one set(<name> ...) list in the tiers file, in file order."""
    text = K_COMMENT.sub("", K_TIERS_FILE.read_text(encoding="utf-8"))
    match = re.search(rf"\bset\s*\(\s*{name}\s([^)]*)\)", text)
    if match is None:
        raise AssertionError(f"{K_TIERS_FILE.name} has no set({name} ...)")
    return match.group(1).split()


def _cmake_pairs(name: str) -> list[tuple[str, str]]:
    """Returns one set(<name> ...) list of the tiers file as consecutive pairs."""
    items = _cmake_list(name)
    return list(zip(items[0::2], items[1::2], strict=True))


def _edges(pairs: set[tuple[str, str]]) -> list[str]:
    """Returns each pair as `consumer -> dependency`, sorted."""
    return sorted(f"{consumer} -> {dependency}" for consumer, dependency in pairs)


class TierTableTest(unittest.TestCase):
    """Checks the checker's copies of the tier table against the CMake file."""

    def test_is_registered(self) -> None:
        """Runs the rules that read a build table as part of the checker's rule table."""
        self.assertIs(K_CHECKER.rules["rule_same_tier_includes"], rule_same_tier_includes)
        self.assertIs(K_CHECKER.rules["rule_declared_dependencies"], rule_declared_dependencies)
        self.assertIs(K_CHECKER.rules["rule_public_headers"], rule_public_headers)

    def test_tier_order_matches_the_cmake_table(self) -> None:
        """Holds the tiers of SUSHIRUNTIME_TIER_ORDER in the same order."""
        declared = _cmake_list("SUSHIRUNTIME_TIER_ORDER")
        self.assertEqual(
            list(K_TIER_ORDER), declared,
            msg=f"K_TIER_ORDER is {list(K_TIER_ORDER)}; {K_TIERS_FILE.name} declares {declared}",
        )

    def test_same_tier_edges_match_the_cmake_table(self) -> None:
        """Holds exactly the pairs of SUSHIRUNTIME_SAME_TIER_EDGES."""
        declared = set(_cmake_pairs("SUSHIRUNTIME_SAME_TIER_EDGES"))
        difference = (
            f"only in K_SAME_TIER_EDGES: {_edges(K_SAME_TIER_EDGES - declared)}; "
            f"only in {K_TIERS_FILE.name}: {_edges(declared - K_SAME_TIER_EDGES)}"
        )
        self.assertEqual(set(K_SAME_TIER_EDGES), declared, msg=difference)

    def test_module_folders_match_the_cmake_table(self) -> None:
        """Finds each module of SUSHIRUNTIME_MODULE_TIERS under its tier, and no other folder."""
        declared = {f"{tier}/{module}" for module, tier in _cmake_pairs("SUSHIRUNTIME_MODULE_TIERS")}
        folders = {
            f"{folder.parent.name}/{folder.name}"
            for folder in (K_ROOT / "modules").glob("*/*") if folder.is_dir()
        }
        difference = (
            f"only under modules/: {sorted(folders - declared)}; "
            f"only in {K_TIERS_FILE.name}: {sorted(declared - folders)}"
        )
        self.assertEqual(folders, declared, msg=difference)


if __name__ == "__main__":
    unittest.main()
