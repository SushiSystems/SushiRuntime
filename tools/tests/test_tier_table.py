# test_tier_table.py
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026-present Mustafa Garip & Sushi Systems
# Licensed under the Apache License 2.0. See LICENSE.
"""Tests that the layering checker's tier table matches cmake/SushiRuntimeTiers.cmake."""

from __future__ import annotations

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
from layering.tier_table import parse_tier_list, parse_tier_list_text  # noqa: E402
from settings import K_TIER_ORDER  # noqa: E402

K_TEST_SIZE = "small"
K_ROOT = Path(__file__).resolve().parents[2]
K_TIERS_FILE = K_ROOT / "cmake" / "SushiRuntimeTiers.cmake"


def _pairs(items: list[str]) -> list[tuple[str, str]]:
    """Returns a list of module and tier items as consecutive pairs."""
    return list(zip(items[0::2], items[1::2], strict=True))


class TierRulesTest(unittest.TestCase):
    """Checks the rule table of the layering checker."""

    K_TEST_SIZE = "small"

    def test_is_registered(self) -> None:
        """Runs the rules that read a build table as part of the checker's rule table."""
        self.assertIs(K_CHECKER.rules["rule_same_tier_includes"], rule_same_tier_includes)
        self.assertIs(K_CHECKER.rules["rule_declared_dependencies"], rule_declared_dependencies)
        self.assertIs(K_CHECKER.rules["rule_public_headers"], rule_public_headers)


class TierListTextTest(unittest.TestCase):
    """Checks how the tier table reader takes one named list out of the text of a CMake file."""

    def test_parse_tier_list_text_reads_only_the_named_list(self) -> None:
        """Returns the items of the list it names, and not those of another list in the same text."""
        text = "set(SUSHIRUNTIME_TIER_ORDER\n    foundation\n    platform)\nset(OTHER a b)\n"
        self.assertEqual(parse_tier_list_text(text, "SUSHIRUNTIME_TIER_ORDER"), ["foundation", "platform"])

    def test_parse_tier_list_text_ignores_a_set_line_in_a_comment(self) -> None:
        """Skips a commented set(...) line, so only the live list is read."""
        text = "# set(LIST ghost)\nset(LIST real)\n"
        self.assertEqual(parse_tier_list_text(text, "LIST"), ["real"])

    def test_parse_tier_list_text_refuses_a_text_with_no_such_list(self) -> None:
        """Raises ValueError for a name the text does not set, rather than returning an empty list."""
        with self.assertRaises(ValueError):
            parse_tier_list_text("set(OTHER a)\n", "SUSHIRUNTIME_TIER_ORDER")

    def test_pairs_read_module_and_tier_items_as_consecutive_pairs(self) -> None:
        """Pairs the items of a module list two by two, module first and tier second."""
        items = parse_tier_list_text("set(MODULE_TIERS core foundation report foundation)\n", "MODULE_TIERS")
        self.assertEqual(_pairs(items), [("core", "foundation"), ("report", "foundation")])


class TierTableTest(unittest.TestCase):
    """Checks the tier order in settings.py and the module folders against the CMake file."""

    K_TEST_SIZE = "medium"

    def test_tier_order_matches_the_cmake_table(self) -> None:
        """Holds the tiers of SUSHIRUNTIME_TIER_ORDER in the same order."""
        declared = parse_tier_list(K_TIERS_FILE, "SUSHIRUNTIME_TIER_ORDER")
        self.assertEqual(
            list(K_TIER_ORDER), declared,
            msg=f"K_TIER_ORDER is {list(K_TIER_ORDER)}; {K_TIERS_FILE.name} declares {declared}",
        )

    def test_module_folders_match_the_cmake_table(self) -> None:
        """Finds each module of SUSHIRUNTIME_MODULE_TIERS under its tier, and no other folder."""
        items = parse_tier_list(K_TIERS_FILE, "SUSHIRUNTIME_MODULE_TIERS")
        declared = {f"{tier}/{module}" for module, tier in _pairs(items)}
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
