# test_aliases.py
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026 Sushi Systems
# Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.
# Commercial use requires a licence from Sushi Systems.
"""Tests for the old spellings sr still answers to."""

from __future__ import annotations

from datetime import date

import pytest
from typer.testing import CliRunner

from sushiruntime import aliases
from sushiruntime.cli import app
from sushiruntime.services import docker, project

K_OLD_TO_NEW = {
    "sr doxygen": "sr docs",
    "sr docker build": "sr container build",
    "sr docker run": "sr container run",
}


def test_the_table_holds_each_renamed_command():
    """Every renamed command has one row that names its replacement."""
    assert {row.old: row.new for row in aliases.K_ROWS} == K_OLD_TO_NEW
    for old, new in K_OLD_TO_NEW.items():
        assert aliases.ALIASES.alias_for(old).new == new


def test_every_row_is_removed_after_the_shared_date():
    """Each row carries 2027-03-25, the day the old spellings leave every Sushi CLI."""
    assert {row.removed_after for row in aliases.K_ROWS} == {date(2027, 3, 25)}


def test_no_row_has_expired_today():
    """No old spelling is still registered past its last day."""
    assert aliases.ALIASES.expired(date.today()) == []


def test_doxygen_still_runs_docs(monkeypatch):
    """`sr doxygen` runs the service behind `sr docs`."""
    monkeypatch.setattr(project, "docs", lambda: 5)
    runner = CliRunner()
    assert runner.invoke(app, ["docs"]).exit_code == 5
    assert runner.invoke(app, ["doxygen"]).exit_code == 5


@pytest.mark.parametrize(
    ("word", "arguments", "expected"),
    [
        ("build", ["--no-cache", "--oneapi"], {"no_cache": True, "oneapi": True}),
        ("run", ["--admin", "--no-gpu"], {"admin": True, "no_gpu": True}),
    ],
)
def test_docker_still_runs_container(word, arguments, expected, monkeypatch):
    """`sr docker <word>` runs the service behind `sr container <word>` with the same options."""
    seen: list[dict] = []
    monkeypatch.setattr(docker, word, lambda **options: seen.append(options) or 0)
    runner = CliRunner()
    assert runner.invoke(app, ["container", word, *arguments]).exit_code == 0
    assert runner.invoke(app, ["docker", word, *arguments]).exit_code == 0
    assert seen == [expected, expected]


def test_the_old_words_are_absent_from_the_help_screen():
    """`sr --help` lists neither `doxygen` nor `docker`."""
    words = CliRunner().invoke(app, ["--help"]).output.split()
    assert "doxygen" not in words
    assert "docker" not in words
