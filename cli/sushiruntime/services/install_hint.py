# install_hint.py
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026 Sushi Systems
# Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.
# Commercial use requires a licence from Sushi Systems.
"""The one command every message about a missing SYCL toolchain names."""

from __future__ import annotations

from ..config import PROFILE


def toolchain_install_command(name: str) -> str:
    """Returns the command that installs toolchain *name* into this checkout."""
    return f"{PROFILE.program} setup --toolchain {name}"
