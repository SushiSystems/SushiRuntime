# test_packaging.py
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026-present Mustafa Garip & Sushi Systems
# Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.
# Commercial use requires a licence from Sushi Systems.
"""Tests that `sr package` clears only a directory it is allowed to and reports a refusal."""

from __future__ import annotations

import pytest
from sushicore.proc import Runner
from typer.testing import CliRunner

from sushiruntime.cli import app
from sushiruntime.config import Config
from sushiruntime.services import packaging
from sushiruntime.services.stage_tree import K_STAGE_MARKER


class _Console:
    """Records the error lines a service prints and ignores the rest."""

    def __init__(self) -> None:
        """Starts with no recorded line."""
        self.errors: list[str] = []

    def error(self, message: str) -> None:
        """Records one error line."""
        self.errors.append(message)

    def __getattr__(self, name: str):
        """Returns a printer that prints nothing, for every other console call."""
        return lambda *args, **kwargs: None


@pytest.fixture
def checkout(tmp_path, monkeypatch):
    """Returns a configured checkout beside its owner's home; commands are recorded, not run."""
    root = tmp_path / "work" / "repo"
    (root / "build").mkdir(parents=True)
    (tmp_path / "home").mkdir()
    (root / "build" / "CMakeCache.txt").write_text("", encoding="utf-8")
    commands: list[list[str]] = []
    recorder = _Console()
    monkeypatch.setattr(packaging, "find_project_root", lambda: root)
    monkeypatch.setattr(packaging, "load_config", lambda: Config(platform="linux"))
    monkeypatch.setattr(packaging, "load_build_env", lambda cfg, build_dir: {})
    monkeypatch.setattr(packaging, "console", recorder)
    monkeypatch.setenv("HOME", str(tmp_path / "home"))
    monkeypatch.setenv("USERPROFILE", str(tmp_path / "home"))
    monkeypatch.setattr(
        Runner, "run", lambda self, cmd, cwd, env=None: commands.append(list(cmd)) or 0)
    return root, commands, recorder


def test_package_refuses_a_foreign_directory_with_one_error_line(checkout, tmp_path):
    """A non-empty prefix the CLI did not stage is left untouched; one error line, exit 1."""
    root, commands, recorder = checkout
    foreign = tmp_path / "precious"
    foreign.mkdir()
    (foreign / "thesis.tex").write_text("years", encoding="utf-8")
    assert packaging.package(prefix=str(foreign), archive=False) == 1
    assert (foreign / "thesis.tex").read_text(encoding="utf-8") == "years"
    assert commands == []
    assert len(recorder.errors) == 1 and "\n" not in recorder.errors[0]


@pytest.mark.parametrize(
    ("prefix", "reason"),
    [
        (".", "the project root"),
        ("..", "an ancestor of the project root"),
        ("build", "the build tree"),
        ("~", "the home directory"),
    ],
)
def test_package_refuses_a_protected_prefix_even_with_force(checkout, monkeypatch, prefix, reason):
    """The checkout, its parent, its build tree and the home directory are never cleared."""
    root, commands, recorder = checkout
    monkeypatch.chdir(root)
    assert packaging.package(prefix=prefix, archive=False, force=True) == 1
    assert (root / "build" / "CMakeCache.txt").is_file()
    assert commands == []
    assert len(recorder.errors) == 1 and reason in recorder.errors[0]


def test_package_with_force_clears_a_foreign_directory_and_marks_it(checkout, tmp_path):
    """--force empties the prefix, writes the stage marker and installs into it."""
    root, commands, recorder = checkout
    foreign = tmp_path / "staging"
    foreign.mkdir()
    (foreign / "old.txt").write_text("stale", encoding="utf-8")
    assert packaging.package(prefix=str(foreign), archive=False, force=True) == 0
    assert [entry.name for entry in foreign.iterdir()] == [K_STAGE_MARKER]
    assert commands[0][1:] == ["--install", str(root / "build"), "--prefix", str(foreign)]


def test_package_clears_its_default_stage_without_force(checkout):
    """build/dist from an earlier run is cleared and marked with no flag."""
    root, commands, recorder = checkout
    stale = root / "build" / "dist" / "stale.txt"
    stale.parent.mkdir()
    stale.write_text("stale", encoding="utf-8")
    assert packaging.package(archive=False) == 0
    assert not stale.exists()
    assert (root / "build" / "dist" / K_STAGE_MARKER).is_file()
    assert recorder.errors == []


def test_the_command_passes_force_to_the_service(monkeypatch):
    """`sr package --force` reaches the service as force=True, and defaults to False."""
    seen: list[dict] = []
    monkeypatch.setattr(packaging, "package", lambda **options: seen.append(options) or 0)
    runner = CliRunner()
    assert runner.invoke(app, ["package", "--prefix", "out", "--force"]).exit_code == 0
    assert runner.invoke(app, ["package"]).exit_code == 0
    assert [options["force"] for options in seen] == [True, False]
