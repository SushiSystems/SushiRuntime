# test_check_device_sources.py
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026-present Mustafa Garip & Sushi Systems
# Licensed under the Apache License 2.0. See LICENSE.
"""Tests the rules of tools/layering/check_device_sources.py on trees given as text."""

from __future__ import annotations

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from layering.check_device_sources import (  # noqa: E402
    TextFiles,
    UnitTree,
    rule_device_source_missing,
    rule_device_source_unneeded,
)

K_TEST_SIZE = "small"
K_ROOT = Path("repository")
K_API_INCLUDE = "#include <SushiRuntime/api/graph/graph.hpp>"


def _test_build_file(sources: list[str], device: list[str]) -> str:
    """Returns a module test build file listing the given sources and device sources."""
    lines = ["sushiruntime_add_module_tests(", "    MODULE api", "    SOURCES"]
    lines += [f"        {name}" for name in sources]
    if device:
        lines.append("    DEVICE_SOURCES")
        lines += [f"        {name}" for name in device]
    lines[-1] += ")"
    return "\n".join(lines)


class DeviceSourcesTest(unittest.TestCase):
    """Holds a repository with an API module and the tests of it, as text under its paths."""

    def setUp(self) -> None:
        self.root = K_ROOT
        self.texts: dict[Path, str] = {
            self.root / "modules/surface/api/include/SushiRuntime/api/graph/graph.hpp": "struct Graph;",
            self.root / "modules/surface/api/source/graph.cpp":
                K_API_INCLUDE + "\nvoid f(Pins& pins) { pins.add(1); }",
            self.root / "modules/surface/api/CMakeLists.txt":
                "sushiruntime_add_module(NAME api TIER surface SOURCES graph.cpp)",
            self.root / "tests/surface/api/test_recorded.cpp":
                K_API_INCLUDE + "\nvoid t(Graph& g) { g.add(4, Reads(), Writes(b), [](std::size_t) {}); }",
            self.root / "tests/surface/api/test_launched.cpp": '#include "launch_helper.hpp"',
            self.root / "tests/surface/api/launch_helper.hpp":
                "inline void l(sycl::queue& q) { q.single_task([] {}); }",
            self.root / "tests/surface/api/test_hooks.cpp":
                K_API_INCLUDE + "\nvoid h(Hooks& hooks) { hooks.add([](std::size_t) {}); }",
            self.root / "tests/surface/api/test_plain.cpp": "int plain() { return set.add(1); }",
        }

    def _declare(self, sources: list[str], device: list[str]) -> UnitTree:
        """Keeps the test build file beside the sources and returns the tree over the text."""
        self.texts[self.root / "tests/surface/api/CMakeLists.txt"] = _test_build_file(sources, device)
        return UnitTree(self.root, TextFiles(self.texts))

    def _messages(self, rule, tree: UnitTree) -> list[str]:
        """Returns the message of each issue a rule yields."""
        return [issue.message for issue in rule(tree)]


K_ALL = ["test_recorded.cpp", "test_launched.cpp", "test_hooks.cpp", "test_plain.cpp"]
K_DEVICE = ["test_recorded.cpp", "test_launched.cpp"]


class PassingTreeTest(DeviceSourcesTest):
    """Covers a tree whose device lists are exactly the units that instantiate a kernel."""

    def test_reports_nothing_when_the_lists_match(self) -> None:
        """Yields no issue from either rule when DEVICE_SOURCES lists exactly the kernel units."""
        tree = self._declare(K_ALL, K_DEVICE)
        issues = self._messages(rule_device_source_missing, tree) + self._messages(rule_device_source_unneeded, tree)
        self.assertEqual(issues, [])


class RuleDeviceSourceMissingTest(DeviceSourcesTest):
    """Covers the rule that every unit instantiating a kernel is listed."""

    def test_reports_each_unit_that_instantiates_a_kernel_and_is_not_listed(self) -> None:
        """Names the unit that records a kernel through the API and the unit whose quoted header launches one."""
        messages = self._messages(rule_device_source_missing, self._declare(K_ALL, []))
        self.assertEqual(len(messages), 2)
        self.assertIn("test_recorded.cpp instantiates a kernel recorded through the API", messages[0])
        self.assertIn("test_launched.cpp instantiates launch_helper.hpp -> a kernel launch", messages[1])

    def test_reads_no_recording_call_in_a_library_source(self) -> None:
        """Reports nothing for a library source that calls add, since only tests and applications record."""
        self.assertEqual(self._messages(rule_device_source_missing, self._declare(K_ALL, K_DEVICE)), [])


class RuleDeviceSourceUnneededTest(DeviceSourcesTest):
    """Covers the rule that a listed unit instantiates a kernel."""

    def test_reports_each_listed_unit_that_instantiates_no_kernel(self) -> None:
        """Names the listed unit that only adds to a container and the listed unit that reaches no API."""
        tree = self._declare(K_ALL, [*K_DEVICE, "test_hooks.cpp", "test_plain.cpp"])
        self.assertEqual(self._messages(rule_device_source_unneeded, tree), [
            "test_hooks.cpp is in DEVICE_SOURCES and instantiates no kernel; list it in SOURCES only",
            "test_plain.cpp is in DEVICE_SOURCES and instantiates no kernel; list it in SOURCES only",
        ])


if __name__ == "__main__":
    unittest.main()
