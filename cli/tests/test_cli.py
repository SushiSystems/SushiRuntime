# test_cli.py
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026-present Mustafa Garip & Sushi Systems
# Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.
# Commercial use requires a licence from Sushi Systems.
"""Tests for sr's entry point, its root options and the commands it takes from sushicore."""

from __future__ import annotations

import json
import re
import subprocess
import sys
from pathlib import Path
from types import SimpleNamespace

import pytest
from sushicore.root_options import version_line
from typer.testing import CliRunner

from sushiruntime import cli, console
from sushiruntime.cli import app
from sushiruntime.services import diag, project
from sushiruntime.services.project import BuildType

pytestmark = pytest.mark.medium

K_VISIBLE = [
    "bench", "build", "clean", "config", "container build", "container run", "docs", "docs bundle",
    "doctor", "env", "link", "package", "run", "setup", "test", "toolchain", "unlink",
]
K_GIT = (
    "git", "-c", "user.name=tests", "-c", "user.email=tests@example.invalid",
    "-c", "commit.gpgsign=false",
)
K_PUBLISH_LIST = (
    'name = "sample"\n'
    'title = "Sample"\n'
    'summary = "A sample repository."\n'
    'sections = ["guides"]\n'
    'api = false\n'
)
K_ARCHIVE = "docs-bundle-1.2.3.tar.gz"
K_ANSI_ESCAPE = re.compile(r"\x1b\[[0-9;]*m")
K_DOXYFILE = Path(__file__).resolve().parents[2] / ".config" / "doxygen" / "Doxyfile"


def test_docs_runs_the_api_build_and_bundle_is_its_subcommand(monkeypatch, tmp_path):
    """`sr docs` still builds the reference, and `sr docs bundle` asks for a release."""
    monkeypatch.setattr(project, "docs", lambda: 6)
    monkeypatch.setattr(cli, "find_project_root", lambda: tmp_path)
    runner = CliRunner()
    assert runner.invoke(app, ["docs"]).exit_code == 6
    assert runner.invoke(app, ["doxygen"]).exit_code == 6
    missing = runner.invoke(app, ["docs", "bundle"])
    assert missing.exit_code == 2
    assert "--release" in K_ANSI_ESCAPE.sub("", missing.output)


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
    seen: list[tuple[bool, BuildType | None]] = []
    monkeypatch.setattr(
        diag, "env_dump",
        lambda show_all=False, build_type=None: seen.append((show_all, build_type)) or 0)
    assert CliRunner().invoke(app, ["env", flag, "--asan"]).exit_code == 0
    assert seen == [(True, BuildType.asan)]


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


def _published_checkout(root: Path) -> None:
    """Writes the CLI's root marker, a one-page docs/ tree and a git commit that holds them."""
    guides = root / "docs" / "guides"
    guides.mkdir(parents=True)
    (root / cli.PROFILE.root_marker).write_text("", encoding="utf-8")
    (root / "docs" / "publish.toml").write_text(K_PUBLISH_LIST, encoding="utf-8")
    (root / "docs" / "README.md").write_text(
        "# Sample\n\n- [Using](guides/USING.md)\n", encoding="utf-8")
    (guides / "USING.md").write_text("# Using\n\nRun it.\n", encoding="utf-8")
    for arguments in (("init", "--quiet"), ("add", "--all"),
                      ("commit", "--quiet", "--message", "docs")):
        subprocess.run([*K_GIT, *arguments], cwd=root, check=True, capture_output=True)


def _doxyfile_value(key: str, default: str) -> str:
    """Returns the value the repository's Doxyfile gives *key*, or *default* when it gives none."""
    for line in K_DOXYFILE.read_text(encoding="utf-8").splitlines():
        name, separator, value = line.partition("=")
        if separator and name.strip() == key:
            return value.strip().strip('"')
    return default


def test_docs_bundle_writes_the_archive_of_the_checkout_it_runs_in(tmp_path, monkeypatch):
    """Writes the archive and its digest file at the checkout's root and reports both lines."""
    _published_checkout(tmp_path)
    monkeypatch.chdir(tmp_path / "docs" / "guides")
    said: list[str] = []
    monkeypatch.setattr(cli, "console", SimpleNamespace(success=said.append))
    result = CliRunner().invoke(cli.app, ["docs", "bundle", "--release", "1.2.3"])
    assert result.exit_code == 0, result.output
    bundle = tmp_path / "build" / "docs" / "bundle"
    assert (bundle / K_ARCHIVE).is_file()
    assert (bundle / (K_ARCHIVE + ".sha256")).is_file()
    assert len(said) == 2, said
    assert said[0].endswith(f"{K_ARCHIVE} (1 pages)")
    assert said[1].startswith("sha256 ")


def test_the_api_source_reads_the_xml_doxygen_writes_under_the_site_folder(monkeypatch, tmp_path):
    """Names `<OUTPUT_DIRECTORY>/<XML_OUTPUT>` of the Doxyfile, below the repository root."""
    monkeypatch.setattr(cli, "find_project_root", lambda: tmp_path)
    written = Path(_doxyfile_value("OUTPUT_DIRECTORY", "")) / _doxyfile_value("XML_OUTPUT", "xml")
    assert cli._api_source().xml_dir == tmp_path / written
