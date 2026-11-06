# test_packaging.py
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026-present Mustafa Garip & Sushi Systems
# Licensed under the Apache License 2.0. See LICENSE.
"""Tests that `sr package` clears only a directory it is allowed to and reports a refusal."""

from __future__ import annotations

import pytest
from typer.testing import CliRunner

from sushiruntime.cli import app
from sushiruntime.services import packaging
from sushiruntime.services.stage_tree import K_STAGE_MARKER

pytestmark = pytest.mark.medium


@pytest.fixture
def make(rig, tmp_path, monkeypatch):
    """Returns a builder of a rig over a configured checkout beside its owner's home."""
    (tmp_path / "home").mkdir()
    monkeypatch.setenv("HOME", str(tmp_path / "home"))
    monkeypatch.setenv("USERPROFILE", str(tmp_path / "home"))

    def build(**options):
        """Builds the rig and the configured Debug tree below its checkout."""
        built = rig(**options)
        (built.root / "build" / "default-debug").mkdir(parents=True)
        cache = built.root / "build" / "default-debug" / "CMakeCache.txt"
        cache.write_text("", encoding="utf-8")
        return built

    return build


def _package(built, **options) -> int:
    """Runs the package service over the rig and returns its exit code."""
    return packaging.package(surroundings=built.surroundings, **options)


def test_package_prints_one_line_for_the_install(make):
    """cmake per-file lines are held back and one summary names the stage."""
    built = make(capture_output="-- Installing: nowhere\n")
    assert _package(built, archive=False) == 0
    assert built.console.texts("info")[-2:] == [
        f"Installed 0 files, 0 up to date, into {built.root / 'build' / 'dist'}",
        "Run `sr test --label package` to check it actually works."]


def test_package_shows_cmakes_output_when_the_install_fails(make):
    """A failed install exits with the code of cmake and its output in one error."""
    built = make(capture_code=2, capture_output="CMake Error: no install rules")
    assert _package(built, archive=False) == 2
    assert built.console.texts("error") == ["cmake --install exited 2:\nCMake Error: no install rules"]


def test_package_refuses_a_foreign_directory_with_one_error_line(make, tmp_path):
    """A non-empty prefix the CLI did not stage is left untouched; one error line, exit 1."""
    checkout = make()
    foreign = tmp_path / "precious"
    foreign.mkdir()
    (foreign / "thesis.tex").write_text("years", encoding="utf-8")
    assert _package(checkout, prefix=str(foreign), archive=False) == 1
    assert (foreign / "thesis.tex").read_text(encoding="utf-8") == "years"
    assert checkout.captured == []
    errors = checkout.console.texts("error")
    assert len(errors) == 1 and "\n" not in errors[0]


@pytest.mark.parametrize(
    ("prefix", "reason"),
    [
        (".", "the project root"),
        ("..", "an ancestor of the project root"),
        ("build", "the build tree"),
        ("~", "the home directory"),
    ],
)
def test_package_refuses_a_protected_prefix_even_with_force(make, monkeypatch, prefix, reason):
    """The checkout, its parent, its build tree and the home directory are never cleared."""
    checkout = make()
    monkeypatch.chdir(checkout.root)
    assert _package(checkout, prefix=prefix, archive=False, force=True) == 1
    assert (checkout.root / "build" / "default-debug" / "CMakeCache.txt").is_file()
    assert checkout.captured == []
    errors = checkout.console.texts("error")
    assert len(errors) == 1 and reason in errors[0]


def test_package_with_force_clears_a_foreign_directory_and_marks_it(make, tmp_path):
    """--force empties the prefix, writes the stage marker and installs into it."""
    checkout = make()
    foreign = tmp_path / "staging"
    foreign.mkdir()
    (foreign / "old.txt").write_text("stale", encoding="utf-8")
    assert _package(checkout, prefix=str(foreign), archive=False, force=True) == 0
    assert [entry.name for entry in foreign.iterdir()] == [K_STAGE_MARKER]
    assert checkout.captured[0][1:] == ["--install", str(checkout.root / "build" / "default-debug"),
                                        "--prefix", str(foreign)]


def test_package_clears_its_default_stage_without_force(make):
    """build/dist from an earlier run is cleared and marked with no flag."""
    checkout = make()
    stale = checkout.root / "build" / "dist" / "stale.txt"
    stale.parent.mkdir()
    stale.write_text("stale", encoding="utf-8")
    assert _package(checkout, archive=False) == 0
    assert not stale.exists()
    assert (checkout.root / "build" / "dist" / K_STAGE_MARKER).is_file()
    assert checkout.console.texts("error") == []


@pytest.mark.parametrize("arguments, forced", [(["--force"], True), ([], False)])
def test_the_command_passes_force_to_the_service(make, arguments, forced):
    """`sr package --force` clears a prefix it did not stage, and without it refuses one."""
    checkout = make()
    prefix = checkout.root / "out"
    prefix.mkdir()
    (prefix / "foreign.txt").write_text("data", encoding="utf-8")
    result = CliRunner().invoke(
        app, ["package", "--prefix", str(prefix), "--no-archive", *arguments],
        obj=checkout.surroundings)
    assert (result.exit_code == 0) is forced
    assert (not (prefix / "foreign.txt").exists()) is forced
