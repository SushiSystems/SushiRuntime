# test_cli.py
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026 Sushi Systems
# Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.
# Commercial use requires a licence from Sushi Systems.
"""Tests for sr's entry point, its root options and the commands it takes from sushicore."""

from __future__ import annotations

import json
import sys

import pytest
from sushicore.root_options import version_line
from typer.testing import CliRunner

from sushiruntime import cli, console
from sushiruntime.cli import app
from sushiruntime.services import diag, project

K_VISIBLE = [
    "build", "clean", "config", "container build", "container run", "docs", "doctor",
    "env", "link", "package", "run", "setup", "test", "toolchain", "unlink",
]


def _described() -> dict:
    """Returns the catalogue `sr --describe` prints."""
    result = CliRunner().invoke(app, ["--describe"])
    assert result.exit_code == 0, result.output
    return json.loads(result.output)


def test_main_reports_a_malformed_config_as_one_line_and_exit_one(tmp_path, monkeypatch, capsys):
    """A config.toml that is not TOML leaves one line on stderr and exit code 1."""
    (tmp_path / "sushi-module.toml").write_text("[module]\n", encoding="utf-8")
    (tmp_path / "cli").mkdir()
    (tmp_path / "cli" / "config.toml").write_text("[tool\ncmake_exe = ", encoding="utf-8")
    monkeypatch.chdir(tmp_path)
    monkeypatch.setattr(sys, "argv", ["sr", "config"])
    monkeypatch.setattr(console._lazy, "_console", None)
    with pytest.raises(SystemExit) as caught:
        cli.main()
    captured = capsys.readouterr()
    assert caught.value.code == 1
    assert len(captured.err.splitlines()) == 1
    assert "config.toml" in captured.err
    assert "Traceback" not in captured.err + captured.out


def test_main_exits_zero_after_a_command_that_succeeds(monkeypatch):
    """A command that returns 0 ends the process with exit code 0."""
    monkeypatch.setattr(sys, "argv", ["sr", "--version"])
    with pytest.raises(SystemExit) as caught:
        cli.main()
    assert caught.value.code == 0


def test_version_names_the_distribution():
    """--version prints the distribution name and its version on one line."""
    result = CliRunner().invoke(app, ["--version"])
    assert result.exit_code == 0
    assert result.output.strip() == version_line("sushiruntime-cli")


def test_describe_lists_every_visible_command():
    """--describe names the program and each command a user can run."""
    document = _described()
    assert document["program"] == "sr"
    assert [command["name"] for command in document["commands"]] == K_VISIBLE


def test_describe_leaves_the_hidden_spellings_out():
    """--describe carries neither `doxygen` nor anything under `docker`."""
    names = [command["name"] for command in _described()["commands"]]
    assert not [name for name in names if name == "doxygen" or name.startswith("docker")]


def test_config_runs_the_diagnostics_object(monkeypatch):
    """`sr config` exits with what the diagnostics object's report returns."""
    monkeypatch.setattr(type(diag.DIAGNOSTICS), "config_show", lambda self: 7)
    assert CliRunner().invoke(app, ["config"]).exit_code == 7


@pytest.mark.parametrize("flag", ["--all", "-a"])
def test_env_keeps_its_own_asan_flag(flag, monkeypatch):
    """`sr env` passes --all and --asan to the runtime's own environment report."""
    seen: list[tuple[bool, bool]] = []
    monkeypatch.setattr(
        diag, "env_dump", lambda show_all=False, asan=False: seen.append((show_all, asan)) or 0)
    assert CliRunner().invoke(app, ["env", flag, "--asan"]).exit_code == 0
    assert seen == [(True, True)]


def test_run_forwards_a_known_role(monkeypatch):
    """`sr run --distributed worker` hands the role to the run service."""
    seen: list[str | None] = []
    monkeypatch.setattr(
        project, "run", lambda **options: seen.append(options["distributed_role"]) or 0)
    assert CliRunner().invoke(app, ["run", "demo", "--distributed", "WORKER"]).exit_code == 0
    assert seen == ["worker"]


def test_run_rejects_an_unknown_role(monkeypatch):
    """`sr run --distributed <typo>` exits 2 and starts nothing."""
    started: list[dict] = []
    monkeypatch.setattr(project, "run", lambda **options: started.append(options) or 0)
    assert CliRunner().invoke(app, ["run", "demo", "--distributed", "wroker"]).exit_code == 2
    assert started == []
