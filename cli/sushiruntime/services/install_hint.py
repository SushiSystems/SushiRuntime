# install_hint.py
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026-present Mustafa Garip & Sushi Systems
# Licensed under the Apache License 2.0. See LICENSE.
"""The one command every message about a missing SYCL toolchain names."""

from __future__ import annotations

from ..config import PROFILE


def toolchain_install_command(name: str) -> str:
    """Returns the command that installs toolchain *name* into this checkout."""
    return f"{PROFILE.program} setup --toolchain {name}"
