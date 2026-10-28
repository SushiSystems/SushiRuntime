# test_build_jobs.py
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026-present Mustafa Garip & Sushi Systems
# Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.
# Commercial use requires a licence from Sushi Systems.
"""Tests that `sr build --jobs` reaches the driver's compile call."""

from __future__ import annotations

from types import SimpleNamespace

import pytest
from typer.testing import CliRunner

from sushiruntime.cli import app
from sushiruntime.services import project
from sushiruntime.services.project import BuildType

pytestmark = pytest.mark.medium


class FakeDriver:
    """Stands in for the CMake driver over a tree that is already configured."""

    def __init__(self) -> None:
        """Starts with no compile call recorded."""
        self.compiles: list[dict] = []

    def needs_configure(self, build_dir, generator, root=None) -> bool:
        """Reports the tree as configured, so build() goes straight to compile."""
        return False

    def compile(self, cfg, build_dir, **options) -> int:
        """Records the keyword arguments of one compile call."""
        self.compiles.append(options)
        return 0


@pytest.fixture
def driver(monkeypatch, tmp_path) -> FakeDriver:
    """Returns a fake driver installed in the build service, with its surroundings stubbed."""
    fake = FakeDriver()
    packages = SimpleNamespace(definitions=lambda: [], run_dirs=lambda platform: [])
    config = SimpleNamespace(is_windows=False, platform="linux", toolchain="intel-llvm",
                             cxx="clang++", generator="Ninja", ccache_exe="",
                             sushi_definitions=lambda root, packaged: [],
                             sushi_packages=lambda *arguments, **options: packages)
    quiet = SimpleNamespace(**{
        name: lambda *arguments, **options: None
        for name in ("header", "info", "success", "error", "warn")
    })
    monkeypatch.setattr(project, "_DRIVER", fake)
    monkeypatch.setattr(project, "console", quiet)
    monkeypatch.setattr(project, "find_project_root", lambda: tmp_path)
    monkeypatch.setattr(project, "load_config", lambda: config)
    monkeypatch.setattr(project, "load_build_env", lambda cfg, build_dir: {})
    monkeypatch.setattr(project, "_stamp_differs", lambda build_dir, key: False)
    monkeypatch.setattr(project, "_resolve_compiler",
                        lambda *arguments, **options: ("clang", "clang++"))
    return fake


def test_build_passes_jobs_to_the_compile_call(driver):
    """A job count given to build() is the driver's `jobs` argument."""
    assert project.build(BuildType.release, jobs=6) == 0
    assert [call["jobs"] for call in driver.compiles] == [6]


def test_build_uses_the_hardware_threads_when_no_jobs_are_given(driver, monkeypatch):
    """Without a job count the driver gets the job count the memory gate is sized for."""
    monkeypatch.setattr(project.build_admission, "hardware_threads", lambda: 11)
    assert project.build(BuildType.release) == 0
    assert [call["jobs"] for call in driver.compiles] == [11]


@pytest.mark.parametrize("arguments, expected", [
    (["build", "--jobs", "4"], 4),
    (["build", "-j", "12"], 12),
    (["build"], None),
])
def test_the_build_command_forwards_the_flag(arguments, expected, monkeypatch):
    """`sr build --jobs N` and `-j N` hand N to the build service."""
    seen: list[int | None] = []
    monkeypatch.setattr(
        project, "build", lambda *positional, **options: seen.append(options["jobs"]) or 0)
    assert CliRunner().invoke(app, arguments).exit_code == 0
    assert seen == [expected]


def test_the_build_command_rejects_zero_jobs(monkeypatch):
    """`sr build --jobs 0` exits 2 and starts no build."""
    started: list[dict] = []
    monkeypatch.setattr(project, "build", lambda *positional, **options: started.append(options) or 0)
    assert CliRunner().invoke(app, ["build", "--jobs", "0"]).exit_code == 2
    assert started == []
