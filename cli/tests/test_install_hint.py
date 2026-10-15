# test_install_hint.py
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026-present Mustafa Garip & Sushi Systems
# Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.
# Commercial use requires a licence from Sushi Systems.
"""Tests that every message about a missing toolchain names the same install command."""

from __future__ import annotations

from pathlib import Path

import pytest

import sushiruntime
from sushiruntime.config import TOOLCHAINS, Config
from sushiruntime.services import project
from sushiruntime.services.install_hint import toolchain_install_command

pytestmark = pytest.mark.small


@pytest.mark.parametrize("name", TOOLCHAINS)
def test_the_install_command_is_sr_setup_for_the_toolchain(name):
    """The command that installs a toolchain is `sr setup --toolchain <name>`."""
    assert toolchain_install_command(name) == f"sr setup --toolchain {name}"


@pytest.mark.parametrize("name", TOOLCHAINS)
def test_a_missing_windows_compiler_names_sr_setup(name, monkeypatch):
    """The build's missing-compiler error names `sr setup`, as `sr toolchain` does."""
    printed: list[str] = []
    monkeypatch.setattr(project.console, "error", printed.append)
    monkeypatch.setattr(Config, "resolved_windows_compiler", lambda self: "no-such-compiler-xyz")
    assert project._check_windows_compiler(Config(platform="windows", toolchain=name), {}) == 1
    assert toolchain_install_command(name) in printed[0]
    assert "hub install" not in printed[0]


def test_no_service_message_sends_the_user_to_hub_install():
    """No string a service prints tells the user to run `hub install`."""
    services = Path(sushiruntime.__file__).parent / "services"
    found = [
        f"{path.name}:{number}"
        for path in sorted(services.glob("*.py"))
        for number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1)
        if "hub install" in line and not line.lstrip().startswith("#")
    ]
    assert found == []
