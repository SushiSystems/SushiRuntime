# test_provision_commands.py
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026-present Mustafa Garip & Sushi Systems
# Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.
# Commercial use requires a licence from Sushi Systems.
"""Tests that sr carries the shared provision commands."""

from __future__ import annotations

import pytest

from typer.testing import CliRunner

from sushicore.provision.config import ProvisionSettings
from sushiruntime.cli import app
from sushiruntime.config import Config

pytestmark = pytest.mark.medium


def test_the_four_provision_commands_are_registered():
    """Assert setup, doctor, link and unlink appear in the help."""
    out = CliRunner().invoke(app, ["--help"]).output
    for name in ("setup", "doctor", "link", "unlink"):
        assert name in out


def test_setup_takes_the_shared_options():
    """Assert setup offers --dry-run, --toolchain and --no-gpu."""
    out = CliRunner().invoke(app, ["setup", "--help"]).output
    for option in ("--dry-run", "--toolchain", "--no-gpu"):
        assert option in out


def test_config_is_a_provision_config():
    """Assert the config carries every field provision reads."""
    assert issubclass(Config, ProvisionSettings)
