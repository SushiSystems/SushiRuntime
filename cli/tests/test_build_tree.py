# test_build_tree.py
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026-present Mustafa Garip & Sushi Systems
# Licensed under the Apache License 2.0. See LICENSE.
"""Tests that each build type has its own tree below the checkout."""

from __future__ import annotations

from pathlib import Path

import pytest

from sushiruntime.services import project
from sushiruntime.services.project import BuildType

pytestmark = pytest.mark.small

K_ROOT = Path("checkout")


@pytest.mark.parametrize("build_type, folder", [
    (BuildType.debug, "default-debug"),
    (BuildType.release, "default-release"),
    (BuildType.relwithdebinfo, "default-relwithdebinfo"),
    (BuildType.asan, "asan-release"),
    (BuildType.tsan, "tsan-relwithdebinfo"),
])
def test_each_build_type_has_its_own_tree(build_type, folder):
    """The tree of a build type is build/<name>-<type>, and ASan keeps its own name."""
    assert project.build_tree(K_ROOT, build_type) == K_ROOT / "build" / folder


def test_an_explicit_build_dir_overrides_the_tree_of_the_type():
    """--build-dir names the tree whatever the build type."""
    assert project.build_tree(K_ROOT, BuildType.debug, "build/pkg-x") == K_ROOT / "build" / "pkg-x"
