# test_check_layering.py
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026-present Mustafa Garip & Sushi Systems
# Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.
# Commercial use requires a licence from Sushi Systems.
"""Tests the public header rule of the layering checker on trees it reports and accepts."""

from __future__ import annotations

import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from layering.check_layering import collect, rule_public_headers  # noqa: E402

K_ENGINE_BUILD = "sushiruntime_add_module(\n    NAME engine TIER platform INTERNAL\n    HEADER_ONLY)\n"
K_SURFACE_BUILD = (
    "sushiruntime_add_module(\n    NAME surface TIER surface\n    SOURCES surface.cpp\n"
    "    PRIVATE_DEPENDS engine)\n"
)
K_ENGINE_INCLUDE = "#include <Project/engine/engine.hpp>\n"


def _reported(files: dict[str, str]) -> list[str]:
    """Returns `file:line` for each issue the rule reports on a tree built from files."""
    with tempfile.TemporaryDirectory() as root:
        for name, text in files.items():
            path = Path(root, "modules", name)
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(text, encoding="utf-8")
        return [
            f"{issue.path.name}:{issue.line}"
            for tree in collect([Path(root)]) for issue in rule_public_headers(tree)
        ]


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


if __name__ == "__main__":
    unittest.main()
