# settings.py
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026-present Mustafa Garip & Sushi Systems
# Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.
# Commercial use requires a licence from Sushi Systems.
"""Holds the settings of one repository that the shared checkers read.

copy_shared_tools.py writes this file only when a repository has none, and never overwrites it.
"""

from __future__ import annotations

K_TIER_ORDER: tuple[str, ...] = ("foundation", "platform", "engine", "surface", "extension")
K_DOMAIN_TIERS: tuple[str, ...] = ()
K_SAME_TIER_EDGES: frozenset[tuple[str, str]] = frozenset()
K_MODULE_FUNCTION = "sushiruntime_add_module"
K_PUBLIC_DEPENDS = "DEPENDS"
K_PRIVATE_DEPENDS = "PRIVATE_DEPENDS"
K_PRIVATE_DEPENDS_ALSO: tuple[str, ...] = ()
K_INTERNAL_MARK = "INTERNAL"
K_CLOSED = False
K_MACRO_PREFIXES: tuple[str, ...] = ()
K_LOG_PREFIX = "SR"
K_CONSOLE_PATHS: tuple[str, ...] = (
    "cli",
    "modules/foundation/logging",
    "modules/foundation/core/source/assert.cpp",
    "modules/foundation/measurement/include/SushiRuntime/measurement/measurement.hpp",
    "modules/extension/distributed/source/worker_main.cpp",
)
