# aliases.py
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026-present Mustafa Garip & Sushi Systems
# Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.
# Commercial use requires a licence from Sushi Systems.
"""The old spellings `sr` still answers to, each dated, in one alias table."""

from __future__ import annotations

from datetime import date

from sushicore.aliases import Alias, AliasTable

K_REMOVED_AFTER = date(2027, 3, 25)

K_ROWS: tuple[Alias, ...] = (
    Alias("sr doxygen", "sr docs", K_REMOVED_AFTER),
    Alias("sr docker build", "sr container build", K_REMOVED_AFTER),
    Alias("sr docker run", "sr container run", K_REMOVED_AFTER),
)

ALIASES = AliasTable("sr", K_ROWS)
