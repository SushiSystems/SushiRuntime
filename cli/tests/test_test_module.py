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
from sushiruntime.services.project import Label

pytestmark = pytest.mark.medium


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
    (tmp_path / "build" / "default-debug").mkdir(parents=True)
    quiet = SimpleNamespace(**{
        name: lambda *arguments, **options: None
        for name in ("header", "info", "success", "error")
    })
    monkeypatch.setattr(project, "_DRIVER", fake)
    monkeypatch.setattr(project, "console", quiet)
    monkeypatch.setattr(project, "find_project_root", lambda: tmp_path)
    monkeypatch.setattr(project, "load_config", lambda: SimpleNamespace(is_windows=False))
    monkeypatch.setattr(project, "load_build_env", lambda cfg, build_dir: {})
    monkeypatch.setattr(project, "_neutralize_runtime_ld", lambda cfg, env: None)
    monkeypatch.setattr(project.cmake_cache, "is_stale", lambda build_dir, root: False)
    return fake


def test_module_selects_its_label_exactly(driver):
    """A module name becomes an anchored label regex, so `graph` does not match another label."""
    assert project.test(None, module="graph") == 0
    assert [run["label_regex"] for run in driver.runs] == ["^graph$"]


def test_size_label_applies_without_a_module(driver):
    """Without a module the size's own label is passed."""
    assert project.test(Label.small) == 0
    assert [run["label_regex"] for run in driver.runs] == ["^small$"]


def test_no_label_runs_every_size_but_the_quarantined(driver):
    """Without a label or a module every size runs and the quarantined tests do not."""
    assert project.test(None) == 0
    assert [(run["label_regex"], run["exclude_label_regex"]) for run in driver.runs] == [
        ("^(small|medium|large)$", "^quarantined$")]


def test_a_size_run_leaves_the_quarantined_tests_out(driver):
    """A quarantined test keeps its size label, so a size run excludes the quarantined label."""
    assert project.test(Label.medium) == 0
    assert [run["exclude_label_regex"] for run in driver.runs] == ["^quarantined$"]


def test_the_quarantined_run_excludes_nothing(driver):
    """The quarantined run selects that label and leaves nothing out."""
    assert project.test(Label.quarantined) == 0
    assert [(run["label_regex"], run["exclude_label_regex"]) for run in driver.runs] == [
        ("^quarantined$", None)]


def test_jobs_reach_ctest(driver):
    """The number of tests run at once is handed to ctest as given."""
    assert project.test(None, jobs=3) == 0
    assert [run["jobs"] for run in driver.runs] == [3]


def test_no_jobs_leaves_the_choice_to_ctest(driver):
    """Without --jobs no count is handed to ctest."""
    assert project.test(None) == 0
    assert [run["jobs"] for run in driver.runs] == [None]
