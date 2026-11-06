# test_stack_compiler.py
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026-present Mustafa Garip & Sushi Systems
# Licensed under the Apache License 2.0. See LICENSE.
"""Tests that the compiler of a package build outranks the configured one, and that the
run-time folders of the packages lead the search path of the platform."""

from __future__ import annotations

import pytest

from sushiruntime.config import Config
from sushiruntime.services import library_packages

pytestmark = pytest.mark.small


def test_stack_cxx_outranks_the_configured_compiler():
    """The compiler SUSHISTACK_CXX names wins on both platforms' resolution."""
    cfg = Config(cxx="clang++", stack_cxx="/stack/c++", platform="windows")
    assert cfg.resolved_compilers()[1] == "/stack/c++"
    assert cfg.resolved_windows_compiler() == "/stack/c++"


def test_the_windows_run_dirs_lead_path():
    """On Windows the folders go on PATH."""
    cfg = Config(platform="windows")
    env = {"PATH": "C:/old"}
    library_packages.prepend_run_dirs(cfg, env, ["C:/pkg/bin"])
    assert env["PATH"].startswith("C:/pkg/bin")
