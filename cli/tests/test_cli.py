# test_cli.py
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026-present Mustafa Garip & Sushi Systems
# Licensed under the Apache License 2.0. See LICENSE.
"""Tests for sr's entry point, its root options and the commands it takes from sushicore."""

from __future__ import annotations

import json
import os
import re
import subprocess
import tomllib
from pathlib import Path

import pytest
from sushicore.cli_console import LazyConsole
from typer.testing import CliRunner

from sushiruntime import cli, config
from sushiruntime.cli import app

pytestmark = pytest.mark.medium

K_VISIBLE = [
    "bench", "build", "clean", "config", "container build", "container run", "docs", "docs bundle",
    "doctor", "env", "link", "package", "run", "setup", "test", "toolchain", "unlink",
]
#: The groups sushicore adds whole; their commands are sushicore's to list, so only the group is checked.
K_SUSHICORE_GROUPS = ("backlog", "design")
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
K_PYPROJECT = Path(__file__).resolve().parents[1] / "pyproject.toml"
K_DOXYFILE = Path(__file__).resolve().parents[2] / ".config" / "doxygen" / "Doxyfile"


def test_docs_builds_the_api_reference(doxygen_exits_with, monkeypatch, tmp_path):
    """`sr docs` ends with the exit code of the doxygen it runs."""
    doxygen_exits_with(6, tmp_path)
    monkeypatch.chdir(tmp_path)
    assert CliRunner().invoke(app, ["docs"]).exit_code == 6


def test_docs_bundle_asks_for_a_release():
    """`sr docs bundle` without --release is a usage error that names the option."""
    missing = CliRunner().invoke(app, ["docs", "bundle"])
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
    unbuilt = LazyConsole(config.config_dir)
    with pytest.raises(SystemExit) as caught:
        cli.main(["config"], report=lambda message: unbuilt.get().error(message))
    captured = capsys.readouterr()
    assert caught.value.code == 1
    assert len(captured.err.splitlines()) == 1
    assert "config.toml" in captured.err
    assert "Traceback" not in captured.err + captured.out


def test_main_exits_zero_after_a_command_that_succeeds():
    """A command that returns 0 ends the process with exit code 0."""
    with pytest.raises(SystemExit) as caught:
        cli.main(["--version"])
    assert caught.value.code == 0


def test_version_names_the_distribution():
    """--version prints the distribution name of cli/pyproject.toml and a release version on one line."""
    declared = tomllib.loads(K_PYPROJECT.read_text(encoding="utf-8"))["project"]["name"]
    result = CliRunner().invoke(app, ["--version"])
    assert result.exit_code == 0
    assert re.fullmatch(rf"{re.escape(declared)} \d+(\.\d+)+\S*", result.output.strip())


def test_describe_lists_every_visible_command():
    """--describe names the program and each command a user can run."""
    document = _described()
    assert document["program"] == "sr"
    names = [command["name"] for command in document["commands"]]
    assert [name for name in names if name.split()[0] not in K_SUSHICORE_GROUPS] == K_VISIBLE
    assert {name.split()[0] for name in names} >= set(K_SUSHICORE_GROUPS)


def _checkout(root: Path) -> None:
    """Writes the root marker of a checkout with no configuration of its own."""
    (root / cli.PROFILE.root_marker).write_text("", encoding="utf-8")


def test_config_reports_the_resolved_configuration(tmp_path, monkeypatch):
    """`sr config` reports the configuration of a checkout and exits 0."""
    _checkout(tmp_path)
    monkeypatch.chdir(tmp_path)
    result = CliRunner().invoke(app, ["config"])
    assert result.exit_code == 0, result.output


@pytest.mark.parametrize("flag", ["--all", "-a"])
def test_env_keeps_its_own_asan_flag(flag, tmp_path, monkeypatch):
    """`sr env --asan` reads the environment of the sanitizer tree, with --all or without."""
    _checkout(tmp_path)
    monkeypatch.chdir(tmp_path)
    assert CliRunner().invoke(app, ["env", flag, "--asan"]).exit_code == 0
    assert (tmp_path / "build" / "asan-release").is_dir()


def _demo_tree(built) -> None:
    """Makes the Debug tree of the rig checkout and an executable called demo in it."""
    tree = built.root / "build" / "default-debug"
    tree.mkdir(parents=True)
    (tree / ("demo.exe" if os.name == "nt" else "demo")).write_text("", encoding="utf-8")


def test_run_forwards_a_known_role(rig):
    """`sr run --distributed worker` hands the role to the program it runs."""
    built = rig()
    _demo_tree(built)
    result = CliRunner().invoke(
        app, ["run", "demo", "--distributed", "WORKER"], obj=built.surroundings)
    assert result.exit_code == 0, result.output
    assert built.argv()[-1] == "--distributed=worker"


def test_run_rejects_an_unknown_role(rig):
    """`sr run --distributed <typo>` exits 2 and starts nothing."""
    built = rig()
    _demo_tree(built)
    result = CliRunner().invoke(
        app, ["run", "demo", "--distributed", "wroker"], obj=built.surroundings)
    assert result.exit_code == 2
    assert built.runner.commands == []


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
    result = CliRunner().invoke(cli.app, ["docs", "bundle", "--release", "1.2.3"])
    assert result.exit_code == 0, result.output
    bundle = tmp_path / "build" / "docs" / "bundle"
    assert (bundle / K_ARCHIVE).is_file()
    assert (bundle / (K_ARCHIVE + ".sha256")).is_file()
    said = "".join(K_ANSI_ESCAPE.sub("", result.output).split())
    assert said.count("[SUCCESS]") == 2, said
    assert f"{K_ARCHIVE}(1pages)" in said
    assert "[SUCCESS]sha256" in said


def test_the_api_source_reads_the_xml_doxygen_writes_under_the_site_folder(tmp_path):
    """Names `<OUTPUT_DIRECTORY>/<XML_OUTPUT>` of the Doxyfile, below the repository root."""
    written = Path(_doxyfile_value("OUTPUT_DIRECTORY", "")) / _doxyfile_value("XML_OUTPUT", "xml")
    assert cli._api_source(tmp_path).xml_dir == tmp_path / written
