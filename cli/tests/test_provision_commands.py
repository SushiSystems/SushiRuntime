# test_provision_commands.py
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026-present Mustafa Garip & Sushi Systems
# Licensed under the Apache License 2.0. See LICENSE.
"""Tests that sr carries the shared provision commands."""

from __future__ import annotations

import dataclasses

import pytest
from typer.testing import CliRunner

from sushicore.provision.config import ProvisionSettings
from sushiruntime.cli import app
from sushiruntime.config import Config

pytestmark = pytest.mark.medium


def test_setup_takes_the_shared_options():
    """Assert setup offers --dry-run, --toolchain and --no-gpu."""
    out = CliRunner().invoke(app, ["setup", "--help"]).output
    for option in ("--dry-run", "--toolchain", "--no-gpu"):
        assert option in out


@pytest.mark.parametrize("field", [field.name for field in dataclasses.fields(ProvisionSettings)])
def test_config_carries_every_field_provision_reads(field):
    """The configuration has each field the shared provision commands read."""
    assert hasattr(Config(), field)
