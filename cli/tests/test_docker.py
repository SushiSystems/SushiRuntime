# test_docker.py
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026-present Mustafa Garip & Sushi Systems
# Licensed under the Apache License 2.0. See LICENSE.
"""Tests the argv and stdio that `sr docker` hands to sushicore's Runner.

See cli/README.md.
"""

import pytest
import sushicore.proc as proc
from sushicore.console_memory import RecordingConsole

from sushiruntime.services import docker
from sushiruntime.services.surroundings import Surroundings

pytestmark = pytest.mark.medium


def test_build_with_no_flags(rig):
    built = rig()
    assert docker.build(surroundings=built.surroundings) == 0

    assert built.argv() == ["docker", "build", "-f", ".config/docker/Dockerfile",
                            "-t", "sushiruntime", "."]
    assert built.runner.commands[0].cwd == built.root


def test_build_with_no_cache_and_oneapi(rig):
    built = rig()
    assert docker.build(no_cache=True, oneapi=True, surroundings=built.surroundings) == 0

    assert built.argv() == ["docker", "build", "-f", ".config/docker/Dockerfile",
                            "-t", "sushiruntime", "--no-cache",
                            "--build-arg", "WITH_ONEAPI=1", "."]


def test_run_with_no_flags_passes_through_gpu(rig):
    built = rig()
    assert docker.run(surroundings=built.surroundings) == 0

    assert built.argv() == ["docker", "run", "-it", "--rm", "--gpus", "all",
                            "-v", f"{built.root}:/workspace/sushiruntime", "sushiruntime",
                            "/bin/bash"]
    assert built.runner.commands[0].cwd == built.root


def test_run_without_gpu_passthrough(rig):
    built = rig()
    assert docker.run(no_gpu=True, surroundings=built.surroundings) == 0

    assert built.argv() == ["docker", "run", "-it", "--rm",
                            "-v", f"{built.root}:/workspace/sushiruntime", "sushiruntime",
                            "/bin/bash"]


def test_run_admin_adds_privileged_flags(rig):
    built = rig()
    assert docker.run(admin=True, surroundings=built.surroundings) == 0

    assert "--privileged" in built.argv()
    assert "--cap-add=SYS_ADMIN" in built.argv()
    assert "--gpus" in built.argv(), "admin does not imply --no-gpu"


def test_run_is_still_interactive_through_runner(monkeypatch, tmp_path):
    """`docker run -it` needs inherited stdio. Runner.run passes the resolved command
    straight to subprocess.run with no stdout/stderr/stdin override, so the child
    inherits this process's terminal exactly as the old `subprocess.run(cmd,
    cwd=str(cwd))` did. Confirmed here against Runner.run's real implementation;
    only the standard library's subprocess.run is replaced."""

    class _Result:
        returncode = 0

    calls = []

    def fake_run(cmd, **kwargs):
        calls.append(kwargs)
        return _Result()

    monkeypatch.setattr(proc.subprocess, "run", fake_run)

    assert docker.run(surroundings=Surroundings(console=RecordingConsole(), root=tmp_path)) == 0

    assert len(calls) == 1
    assert set(calls[0]) <= {"cwd", "env"}, (
        "Runner.run must not redirect stdio, or `docker run -it` stops being "
        f"interactive: subprocess.run got kwargs {calls[0]}")
