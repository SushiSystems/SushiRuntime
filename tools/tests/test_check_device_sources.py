# test_check_device_sources.py
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026-present Mustafa Garip & Sushi Systems
# Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.
# Commercial use requires a licence from Sushi Systems.
"""Tests the rules of tools/layering/check_device_sources.py on trees built on disk."""

from __future__ import annotations

import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from layering.check_device_sources import (  # noqa: E402
    UnitTree,
    rule_device_source_missing,
    rule_device_source_unneeded,
)

K_API_INCLUDE = "#include <SushiRuntime/api/graph/graph.hpp>"


def _write(root: Path, relative: str, text: str = "") -> None:
    """Writes a file and its folders below root."""
    path = root / relative
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text + "\n", encoding="utf-8")


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
    """Builds a temporary repository with an API module and the tests of it."""

    def setUp(self) -> None:
        self._folder = tempfile.TemporaryDirectory()
        self.root = Path(self._folder.name)
        _write(self.root, "modules/surface/api/include/SushiRuntime/api/graph/graph.hpp", "struct Graph;")
        _write(self.root, "modules/surface/api/source/graph.cpp",
               K_API_INCLUDE + "\nvoid f(Pins& pins) { pins.add(1); }")
        _write(self.root, "modules/surface/api/CMakeLists.txt",
               "sushiruntime_add_module(NAME api TIER surface SOURCES graph.cpp)")
        _write(self.root, "tests/surface/api/test_recorded.cpp",
               K_API_INCLUDE + "\nvoid t(Graph& g) { g.add(4, Reads(), Writes(b), [](std::size_t) {}); }")
        _write(self.root, "tests/surface/api/test_launched.cpp", '#include "launch_helper.hpp"')
        _write(self.root, "tests/surface/api/launch_helper.hpp",
               "inline void l(sycl::queue& q) { q.single_task([] {}); }")
        _write(self.root, "tests/surface/api/test_hooks.cpp",
               K_API_INCLUDE + "\nvoid h(Hooks& hooks) { hooks.add([](std::size_t) {}); }")
        _write(self.root, "tests/surface/api/test_plain.cpp", "int plain() { return set.add(1); }")

    def tearDown(self) -> None:
        self._folder.cleanup()

    def _declare(self, sources: list[str], device: list[str]) -> UnitTree:
        """Writes the test build file and returns the tree."""
        _write(self.root, "tests/surface/api/CMakeLists.txt", _test_build_file(sources, device))
        return UnitTree(self.root)

    def _messages(self, rule, tree: UnitTree) -> list[str]:
        """Returns the message of each issue a rule yields."""
        return [issue.message for issue in rule(tree)]


K_ALL = ["test_recorded.cpp", "test_launched.cpp", "test_hooks.cpp", "test_plain.cpp"]
K_DEVICE = ["test_recorded.cpp", "test_launched.cpp"]


class PassingTreeTest(DeviceSourcesTest):
    """Covers a tree whose device lists are exactly the units that instantiate a kernel."""

    def test_reports_nothing_when_the_lists_match(self) -> None:
        tree = self._declare(K_ALL, K_DEVICE)
        for rule in (rule_device_source_missing, rule_device_source_unneeded):
            self.assertEqual(self._messages(rule, tree), [])


class RuleDeviceSourceMissingTest(DeviceSourcesTest):
    """Covers the rule that every unit instantiating a kernel is listed."""

    def test_reports_a_unit_that_records_a_kernel_and_is_not_listed(self) -> None:
        messages = self._messages(rule_device_source_missing, self._declare(K_ALL, ["test_launched.cpp"]))
        self.assertEqual(len(messages), 1)
        self.assertIn("test_recorded.cpp instantiates a kernel recorded through the API", messages[0])

    def test_reports_a_unit_whose_quoted_header_launches_a_kernel(self) -> None:
        messages = self._messages(rule_device_source_missing, self._declare(K_ALL, ["test_recorded.cpp"]))
        self.assertEqual(len(messages), 1)
        self.assertIn("test_launched.cpp instantiates launch_helper.hpp -> a kernel launch", messages[0])

    def test_reads_no_recording_call_in_a_library_source(self) -> None:
        self.assertEqual(self._messages(rule_device_source_missing, self._declare(K_ALL, K_DEVICE)), [])


class RuleDeviceSourceUnneededTest(DeviceSourcesTest):
    """Covers the rule that a listed unit instantiates a kernel."""

    def test_reports_a_listed_unit_with_a_container_add_only(self) -> None:
        messages = self._messages(rule_device_source_unneeded, self._declare(K_ALL, [*K_DEVICE, "test_hooks.cpp"]))
        self.assertEqual(messages, ["test_hooks.cpp is in DEVICE_SOURCES and instantiates no kernel; "
                                    "list it in SOURCES only"])

    def test_reports_a_listed_unit_that_reaches_no_api(self) -> None:
        messages = self._messages(rule_device_source_unneeded, self._declare(K_ALL, [*K_DEVICE, "test_plain.cpp"]))
        self.assertEqual(len(messages), 1)
        self.assertIn("test_plain.cpp", messages[0])


if __name__ == "__main__":
    unittest.main()
