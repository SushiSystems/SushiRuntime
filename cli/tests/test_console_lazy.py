# test_console_lazy.py
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026-present Mustafa Garip & Sushi Systems
# Licensed under the Apache License 2.0. See LICENSE.
"""Tests that the console of a CLI builds on first use and not at construction.

See cli/README.md.
"""

from __future__ import annotations

from pathlib import Path

import pytest
from sushicore.cli_console import LazyConsole

from sushiruntime import config


def _outside_a_checkout() -> Path:
    """Fails the way config_dir() fails when the working folder sits in no checkout."""
    raise SystemExit(1)


@pytest.mark.small
def test_constructing_the_console_resolves_nothing():
    """The config-dir resolver is not called, and no console is built, until first use."""
    calls: list[str] = []

    def resolver() -> Path:
        """Records the call and answers a folder."""
        calls.append("resolved")
        return Path("cli")

    lazy = LazyConsole(resolver)
    assert (lazy.built, calls) == (False, [])


@pytest.mark.small
def test_outside_a_checkout_no_config_file_is_layered():
    """A resolver that exits leaves the console with no sources, and raises nothing."""
    assert list(LazyConsole(_outside_a_checkout).sources()) == []


@pytest.mark.small
def test_the_console_builds_outside_a_checkout():
    """Getting the console outside a checkout builds a config-less console and no crash."""
    lazy = LazyConsole(_outside_a_checkout)
    assert lazy.get() is not None
    assert lazy.built is True


@pytest.mark.medium
def test_sources_inside_a_checkout_are_the_two_config_files_of_this_cli(tmp_path):
    """Inside a checkout the console layers config.toml and config.local.toml of cli/."""
    (tmp_path / "sushi-module.toml").write_text("[module]\n", encoding="utf-8")
    lazy = LazyConsole(lambda: config.config_dir(tmp_path))
    assert [path.name for path in lazy.sources()] == ["config.toml", "config.local.toml"]


@pytest.mark.small
def test_console_attribute_resolves():
    """The raw Rich `console` attribute resolves lazily too."""
    assert LazyConsole(_outside_a_checkout).attribute("console") is not None


@pytest.mark.small
def test_unknown_attribute_raises_attribute_error():
    """A name the console does not expose is an AttributeError, so hasattr() stays honest."""
    with pytest.raises(AttributeError):
        LazyConsole(_outside_a_checkout).attribute("does_not_exist")
