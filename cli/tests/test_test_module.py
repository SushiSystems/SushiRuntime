# test_test_module.py
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026-present Mustafa Garip & Sushi Systems
# Licensed under the Apache License 2.0. See LICENSE.
"""Tests that `sr test` selects the tests by label through the shared test command and hands the job count to ctest."""

from __future__ import annotations

import pytest

from sushiruntime.services import project
from sushiruntime.services.project import Label

pytestmark = pytest.mark.medium


def _option(argv: list[str], flag: str) -> str | None:
    """Returns the word after *flag* in a ctest command, or None when it has none."""
    return argv[argv.index(flag) + 1] if flag in argv else None


def _ctest(rig, **options) -> list[str]:
    """Runs project.test over a built Debug tree and returns the one ctest command."""
    built = rig()
    (built.root / "build" / "default-debug").mkdir(parents=True)
    assert project.test(surroundings=built.surroundings, **options) == 0
    ctest = [index for index, command in enumerate(built.runner.commands) if built.argv(index)[0] == "ctest"]
    assert len(ctest) == 1
    return built.argv(ctest[0])


@pytest.mark.parametrize("options, label, excluded", [
    ({"label": None, "module": "graph"}, "^graph$", "^quarantined$"),
    ({"label": Label.small}, "^(small)$", "^quarantined$"),
    ({"label": Label.medium}, "^(medium)$", "^quarantined$"),
    ({"label": None}, "^(small|medium|large)$", "^quarantined$"),
    ({"label": Label.quarantined}, "^quarantined$", None),
])
def test_the_label_regex_and_the_exclusion_follow_the_choice(rig, options, label, excluded):
    """A module is an anchored label, a size is its own label and the quarantined run excludes none."""
    argv = _ctest(rig, **options)
    assert (_option(argv, "-L"), _option(argv, "-LE")) == (label, excluded)


def test_jobs_reach_ctest_as_given(rig):
    """The number of tests run at once is handed to ctest as given."""
    assert _option(_ctest(rig, label=None, jobs=3), "--parallel") == "3"


def test_no_jobs_lets_the_shared_command_choose_a_count(rig):
    """Without --jobs the shared command picks a count from the cores and the free memory."""
    assert _option(_ctest(rig, label=None), "--parallel").isdigit()
