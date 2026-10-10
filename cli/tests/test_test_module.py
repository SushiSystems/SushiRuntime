# test_test_module.py
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026-present Mustafa Garip & Sushi Systems
# Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.
# Commercial use requires a licence from Sushi Systems.
"""Tests that `sr test --module` selects the tests labelled with the module's name."""

from __future__ import annotations

from types import SimpleNamespace

import pytest

from sushiruntime.services import project
from sushiruntime.services.project import Suite


class FakeDriver:
    """Stands in for the CMake driver and records each ctest call."""

    def __init__(self) -> None:
        """Starts with no ctest call recorded."""
        self.runs: list[dict] = []

    def ctest_run(self, cfg, build_dir, env, **options) -> int:
        """Records the keyword arguments of one ctest call."""
        self.runs.append(options)
        return 0


@pytest.fixture
def driver(monkeypatch, tmp_path) -> FakeDriver:
    """Returns a fake driver installed in the test service over an existing build folder."""
    fake = FakeDriver()
    (tmp_path / "build").mkdir()
    quiet = SimpleNamespace(**{
        name: lambda *arguments, **options: None
        for name in ("header", "info", "success", "error")
    })
    monkeypatch.setattr(project, "_DRIVER", fake)
    monkeypatch.setattr(project, "console", quiet)
    monkeypatch.setattr(project, "find_project_root", lambda: tmp_path)
    monkeypatch.setattr(project, "load_config", lambda: SimpleNamespace())
    monkeypatch.setattr(project, "load_build_env", lambda cfg, build_dir: {})
    monkeypatch.setattr(project, "_neutralize_runtime_ld", lambda cfg, env: None)
    monkeypatch.setattr(project.cmake_cache, "is_stale", lambda build_dir, root: False)
    return fake


def test_module_selects_its_label_exactly(driver):
    """A module name becomes an anchored label regex, so `graph` does not match another label."""
    assert project.test(Suite.functional, module="graph") == 0
    assert [run["label_regex"] for run in driver.runs] == ["^graph$"]


def test_suite_label_applies_without_a_module(driver):
    """Without a module the suite's own label regex is passed, as before."""
    assert project.test(Suite.unit) == 0
    assert [run["label_regex"] for run in driver.runs] == [project._SUITE_LABEL_REGEX[Suite.unit]]
