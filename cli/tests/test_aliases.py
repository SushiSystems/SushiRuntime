# test_aliases.py
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026-present Mustafa Garip & Sushi Systems
# Licensed under the Apache License 2.0. See LICENSE.
"""Tests for the old spellings sr still answers to."""

from __future__ import annotations

from datetime import date

import pytest
from typer.testing import CliRunner

from sushiruntime import aliases
from sushiruntime.cli import app

K_OLD_TO_NEW = {
    "sr doxygen": "sr docs",
    "sr docker build": "sr container build",
    "sr docker run": "sr container run",
}


@pytest.mark.small
def test_the_table_holds_each_renamed_command():
    """Every renamed command has one row that names its replacement."""
    assert {row.old: row.new for row in aliases.K_ROWS} == K_OLD_TO_NEW
    for old, new in K_OLD_TO_NEW.items():
        assert aliases.ALIASES.alias_for(old).new == new


@pytest.mark.small
def test_no_row_has_expired_on_a_fixed_day():
    """No old spelling is still registered past 2027-03-25, the day the old spellings leave."""
    assert aliases.ALIASES.expired(date(2027, 3, 25)) == []


@pytest.mark.medium
def test_doxygen_still_runs_docs(doxygen_exits_with, monkeypatch, tmp_path):
    """`sr doxygen` runs the service behind `sr docs`: both end with the exit code of doxygen."""
    doxygen_exits_with(5, tmp_path)
    monkeypatch.chdir(tmp_path)
    runner = CliRunner()
    assert runner.invoke(app, ["docs"]).exit_code == 5
    assert runner.invoke(app, ["doxygen"]).exit_code == 5


@pytest.mark.medium
@pytest.mark.parametrize(
    ("word", "arguments", "expected"),
    [
        ("build", ["--no-cache", "--oneapi"], ["--no-cache", "WITH_ONEAPI=1"]),
        ("run", ["--admin", "--no-gpu"], ["--privileged", "--cap-add=SYS_ADMIN"]),
    ],
)
def test_docker_still_runs_container(word, arguments, expected, rig):
    """`sr docker <word>` runs the command behind `sr container <word>` with the same options."""
    built = rig()
    runner = CliRunner()
    assert runner.invoke(app, ["container", word, *arguments], obj=built.surroundings).exit_code == 0
    assert runner.invoke(app, ["docker", word, *arguments], obj=built.surroundings).exit_code == 0
    assert built.argv(0) == built.argv(1)
    assert set(expected) <= set(built.argv(0))


@pytest.mark.medium
def test_the_old_words_are_absent_from_the_help_screen():
    """`sr --help` lists neither `doxygen` nor `docker`."""
    words = CliRunner().invoke(app, ["--help"]).output.split()
    assert "doxygen" not in words
    assert "docker" not in words
