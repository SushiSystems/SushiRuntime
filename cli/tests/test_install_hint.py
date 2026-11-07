# test_install_hint.py
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026-present Mustafa Garip & Sushi Systems
# Licensed under the Apache License 2.0. See LICENSE.
"""Tests that every message about a missing toolchain names the same install command."""

from __future__ import annotations

import pytest
from sushicore.console_memory import RecordingConsole
from sushicore.proc_memory import RecordingRunner

from sushiruntime.config import TOOLCHAINS, Config
from sushiruntime.services import project
from sushiruntime.services.install_hint import toolchain_install_command

pytestmark = pytest.mark.small


@pytest.mark.parametrize("name", TOOLCHAINS)
def test_the_install_command_is_sr_setup_for_the_toolchain(name):
    """The command that installs a toolchain is `sr setup --toolchain <name>`."""
    assert toolchain_install_command(name) == f"sr setup --toolchain {name}"


@pytest.mark.parametrize("name", TOOLCHAINS)
def test_a_missing_windows_compiler_names_sr_setup(name):
    """The build's missing-compiler error names `sr setup`, as `sr toolchain` does."""
    console = RecordingConsole()
    cfg = Config(platform="windows", toolchain=name, stack_cxx="no-such-compiler-xyz")
    assert project._check_windows_compiler(console, RecordingRunner(console), cfg, {}) == 1
    printed = console.texts("error")
    assert toolchain_install_command(name) in printed[0]
    assert "hub install" not in printed[0]

