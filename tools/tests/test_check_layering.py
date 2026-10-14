# test_check_layering.py
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026-present Mustafa Garip & Sushi Systems
# Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.
# Commercial use requires a licence from Sushi Systems.
"""Tests the public header and unused dependency rules of the layering checker on small trees."""

from __future__ import annotations

import sys
import tempfile
import unittest
from collections.abc import Callable, Iterator
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from checking.checker import Issue  # noqa: E402
from layering.check_layering import (  # noqa: E402
    ModuleTree,
    collect,
    rule_public_headers,
    rule_unused_dependencies,
)

K_ENGINE_BUILD = "sushiruntime_add_module(\n    NAME engine TIER platform INTERNAL\n    HEADER_ONLY)\n"
K_SURFACE_BUILD = (
    "sushiruntime_add_module(\n    NAME surface TIER surface\n    SOURCES surface.cpp\n"
    "    PRIVATE_DEPENDS engine)\n"
)
K_ENGINE_INCLUDE = "#include <Project/engine/engine.hpp>\n"


def _issues(files: dict[str, str], rule: Callable[[ModuleTree], Iterator[Issue]]) -> list[Issue]:
    """Returns each issue a rule reports on a tree built from files."""
    with tempfile.TemporaryDirectory() as root:
        for name, text in files.items():
            path = Path(root, "modules", name)
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(text, encoding="utf-8")
        return [issue for tree in collect([Path(root)]) for issue in rule(tree)]


def _reported(files: dict[str, str]) -> list[str]:
    """Returns `file:line` for each issue the public header rule reports on a tree."""
    return [f"{issue.path.name}:{issue.line}" for issue in _issues(files, rule_public_headers)]


def _unused(files: dict[str, str]) -> list[str]:
    """Returns the message of each issue the unused dependency rule reports on a tree."""
    return [issue.message for issue in _issues(files, rule_unused_dependencies)]


class PublicHeadersTest(unittest.TestCase):
    """Checks which includes of an internal module the public header rule reports."""

    def test_reports_an_internal_include_in_an_installed_header(self) -> None:
        """Reports the include on its line when the including module is not INTERNAL."""
        reported = _reported({
            "platform/engine/CMakeLists.txt": K_ENGINE_BUILD,
            "platform/engine/include/Project/engine/engine.hpp": "#pragma once\n",
            "surface/surface/CMakeLists.txt": K_SURFACE_BUILD,
            "surface/surface/include/Project/surface/surface.hpp": "#pragma once\n" + K_ENGINE_INCLUDE,
        })
        self.assertEqual(reported, ["surface.hpp:2"])

    def test_accepts_an_internal_include_in_a_source_file(self) -> None:
        """Accepts the same include under source/, which is not installed."""
        reported = _reported({
            "platform/engine/CMakeLists.txt": K_ENGINE_BUILD,
            "platform/engine/include/Project/engine/engine.hpp": "#pragma once\n",
            "surface/surface/CMakeLists.txt": K_SURFACE_BUILD,
            "surface/surface/source/surface.cpp": K_ENGINE_INCLUDE,
        })
        self.assertEqual(reported, [])

    def test_accepts_an_internal_include_in_an_internal_header(self) -> None:
        """Accepts the include when the including module is INTERNAL itself."""
        reported = _reported({
            "platform/engine/CMakeLists.txt": K_ENGINE_BUILD,
            "platform/engine/include/Project/engine/engine.hpp": "#pragma once\n",
            "surface/surface/CMakeLists.txt": K_SURFACE_BUILD.replace("TIER surface", "TIER surface INTERNAL"),
            "surface/surface/include/Project/surface/surface.hpp": K_ENGINE_INCLUDE,
        })
        self.assertEqual(reported, [])

    def test_accepts_a_public_include_in_an_installed_header(self) -> None:
        """Accepts an include of a module that carries no INTERNAL mark."""
        reported = _reported({
            "platform/engine/CMakeLists.txt": K_ENGINE_BUILD.replace(" INTERNAL", ""),
            "platform/engine/include/Project/engine/engine.hpp": "#pragma once\n",
            "surface/surface/CMakeLists.txt": K_SURFACE_BUILD,
            "surface/surface/include/Project/surface/surface.hpp": K_ENGINE_INCLUDE,
        })
        self.assertEqual(reported, [])


class UnusedDependenciesTest(unittest.TestCase):
    """Checks which named dependencies the unused dependency rule reports."""

    def test_reports_a_dependency_no_file_includes(self) -> None:
        """Reports a module named under PRIVATE_DEPENDS that nothing includes."""
        unused = _unused({
            "platform/engine/CMakeLists.txt": K_ENGINE_BUILD,
            "platform/engine/include/Project/engine/engine.hpp": "#pragma once\n",
            "surface/surface/CMakeLists.txt": K_SURFACE_BUILD,
            "surface/surface/source/surface.cpp": "\n",
        })
        self.assertEqual(unused, ["surface names engine in PRIVATE_DEPENDS and no file includes it"])

    def test_reports_a_public_dependency_only_a_source_includes(self) -> None:
        """Reports a module named under DEPENDS that only source/ includes."""
        unused = _unused({
            "platform/engine/CMakeLists.txt": K_ENGINE_BUILD,
            "platform/engine/include/Project/engine/engine.hpp": "#pragma once\n",
            "surface/surface/CMakeLists.txt": K_SURFACE_BUILD.replace("PRIVATE_DEPENDS", "DEPENDS"),
            "surface/surface/source/surface.cpp": K_ENGINE_INCLUDE,
        })
        self.assertEqual(unused, [
            "surface names engine in DEPENDS and only source/ includes it; name it in PRIVATE_DEPENDS",
        ])

    def test_accepts_a_dependency_its_reach_includes(self) -> None:
        """Accepts a private dependency a source includes and a public one a header includes."""
        private = _unused({
            "platform/engine/CMakeLists.txt": K_ENGINE_BUILD,
            "platform/engine/include/Project/engine/engine.hpp": "#pragma once\n",
            "surface/surface/CMakeLists.txt": K_SURFACE_BUILD,
            "surface/surface/source/surface.cpp": K_ENGINE_INCLUDE,
        })
        public = _unused({
            "platform/engine/CMakeLists.txt": K_ENGINE_BUILD,
            "platform/engine/include/Project/engine/engine.hpp": "#pragma once\n",
            "surface/surface/CMakeLists.txt": K_SURFACE_BUILD.replace("PRIVATE_DEPENDS", "DEPENDS"),
            "surface/surface/include/Project/surface/surface.hpp": K_ENGINE_INCLUDE,
        })
        self.assertEqual(private + public, [])


if __name__ == "__main__":
    unittest.main()
