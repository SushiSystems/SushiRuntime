# diag.py
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026-present Mustafa Garip & Sushi Systems
# Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.
# Commercial use requires a licence from Sushi Systems.
"""Declares the diagnostics behind `sr config` and `sr env`.

See cli/README.md.
"""

from __future__ import annotations

from pathlib import Path

from sushicore.diag import Diagnostics

from .. import config as config_module
from .. import console
from ..config import PROFILE
from .project import K_DEFAULT_TYPE, BuildType, build_tree


def _build_env(cfg, build_dir: Path):
    """The environment a build runs under.

    Imported here rather than at module scope: ``env`` imports ``config``, which
    would close a cycle at module load.
    """
    from ..env import load_build_env

    return load_build_env(cfg, build_dir)


DIAGNOSTICS = Diagnostics(
    profile=PROFILE,
    console=console,
    config=config_module,
    build_env=_build_env,
)


def env_dump(show_all: bool = False, build_type: BuildType = K_DEFAULT_TYPE) -> int:
    """Print the build environment of the tree of *build_type*.

    Each build type has its own tree, and ASan its own ``asan-release`` one, so a
    sanitized build never shares object files with the ordinary one; asking
    which environment a build ran under therefore has to say *which* build.
    """
    root = config_module.find_project_root()
    build_dir = build_tree(root, build_type)
    return DIAGNOSTICS.env_dump(show_all=show_all, build_dir=build_dir)
