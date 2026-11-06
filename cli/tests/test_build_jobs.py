# test_build_jobs.py
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026-present Mustafa Garip & Sushi Systems
# Licensed under the Apache License 2.0. See LICENSE.
"""Tests that `sr build --jobs` reaches the compile command."""

from __future__ import annotations

import pytest
from sushicore import build_admission
from typer.testing import CliRunner

from sushiruntime.cli import app
from sushiruntime.services import project
from sushiruntime.services.project import BuildType

pytestmark = pytest.mark.medium


def _parallel(argv: list[str]) -> str | None:
    """Returns the job count of a `cmake --build` command, or None when it names none."""
    return argv[argv.index("--parallel") + 1] if "--parallel" in argv else None


def _compile_argv(rig) -> list[str]:
    """Returns the `cmake --build` command the rig recorded."""
    return next(command.argv for command in rig.runner.commands if "--build" in command.argv)


def test_build_passes_jobs_to_the_compile_call(rig):
    """A job count given to build() is the `--parallel` count of the compile."""
    surroundings = rig()
    assert project.build(BuildType.release, jobs=6, surroundings=surroundings.surroundings) == 0
    assert _parallel(_compile_argv(surroundings)) == "6"


def test_build_uses_the_hardware_threads_when_no_jobs_are_given(rig):
    """Without a job count the compile gets the job count the memory gate is sized for."""
    surroundings = rig()
    assert project.build(BuildType.release, surroundings=surroundings.surroundings) == 0
    assert _parallel(_compile_argv(surroundings)) == str(build_admission.hardware_threads())


@pytest.mark.parametrize("arguments, expected", [
    (["build", "--jobs", "4"], "4"),
    (["build", "-j", "12"], "12"),
])
def test_the_build_command_forwards_the_flag(arguments, expected, rig):
    """`sr build --jobs N` and `-j N` reach the compile as its `--parallel` count."""
    surroundings = rig()
    result = CliRunner().invoke(app, arguments, obj=surroundings.surroundings)
    assert result.exit_code == 0, result.output
    assert _parallel(_compile_argv(surroundings)) == expected


def test_the_build_command_rejects_zero_jobs(rig):
    """`sr build --jobs 0` exits 2 and runs no command."""
    surroundings = rig()
    result = CliRunner().invoke(app, ["build", "--jobs", "0"], obj=surroundings.surroundings)
    assert result.exit_code == 2
    assert surroundings.runner.commands == []
