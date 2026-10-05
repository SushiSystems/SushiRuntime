# console.py
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026 Sushi Systems
# Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.
# Commercial use requires a licence from Sushi Systems.
"""Exposes the sushicore console this CLI prints through, built on first use.

See cli/README.md.
"""

from __future__ import annotations

from sushicore.cli_console import LazyConsole

from .config import config_dir

_lazy = LazyConsole(config_dir)


def current():
    """Return the sushicore Console this run prints through, building it on first use."""
    return _lazy.get()


# PEP 562 resolves attributes on first access, so importing this module reads no files.
__getattr__ = _lazy.attribute
