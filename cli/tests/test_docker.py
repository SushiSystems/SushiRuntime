# test_docker.py
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026-present Mustafa Garip & Sushi Systems
# Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.
# Commercial use requires a licence from Sushi Systems.
"""Tests the argv and stdio that `sr docker` hands to sushicore's Runner.

See cli/README.md.
"""

from pathlib import Path

import pytest
from sushicore.proc import Runner

from sushiruntime.services import docker


@pytest.fixture
def calls(tmp_path, monkeypatch):
    """Every command Runner.run would have executed, recorded instead of run."""
    recorded: list[tuple[list[str], Path]] = []
    monkeypatch.setattr(docker, "find_project_root", lambda: tmp_path)
    monkeypatch.setattr(Runner, "run",
                        lambda self, cmd, cwd, env=None: recorded.append(
                            (list(cmd), cwd)) or 0)
    return recorded


def test_build_with_no_flags(calls, tmp_path):
    assert docker.build() == 0

    cmd, cwd = calls[0]
    assert cmd == ["docker", "build", "-t", "sushiruntime", "."]
    assert cwd == tmp_path


def test_build_with_no_cache_and_oneapi(calls):
    assert docker.build(no_cache=True, oneapi=True) == 0

    cmd, _ = calls[0]
    assert cmd == ["docker", "build", "-t", "sushiruntime", "--no-cache",
                    "--build-arg", "WITH_ONEAPI=1", "."]


def test_run_with_no_flags_passes_through_gpu(calls, tmp_path):
    assert docker.run() == 0

    cmd, cwd = calls[0]
    assert cmd == ["docker", "run", "-it", "--rm", "--gpus", "all",
                    "-v", f"{tmp_path}:/workspace/sushiruntime", "sushiruntime", "/bin/bash"]
    assert cwd == tmp_path


def test_run_without_gpu_passthrough(calls, tmp_path):
    assert docker.run(no_gpu=True) == 0

    cmd, _ = calls[0]
    assert "--gpus" not in cmd
    assert cmd == ["docker", "run", "-it", "--rm",
                    "-v", f"{tmp_path}:/workspace/sushiruntime", "sushiruntime", "/bin/bash"]


def test_run_admin_adds_privileged_flags(calls):
    assert docker.run(admin=True) == 0

    cmd, _ = calls[0]
    assert "--privileged" in cmd
    assert "--cap-add=SYS_ADMIN" in cmd
    assert "--gpus" in cmd, "admin does not imply --no-gpu"


def test_run_is_still_interactive_through_runner(monkeypatch, tmp_path):
    """`docker run -it` needs inherited stdio. Runner.run passes the resolved command
    straight to subprocess.run with no stdout/stderr/stdin override, so the child
    inherits this process's terminal exactly as the old `subprocess.run(cmd,
    cwd=str(cwd))` did. Confirmed here against Runner.run's real implementation
    rather than the stand-in the other tests in this file use."""
    monkeypatch.setattr(docker, "find_project_root", lambda: tmp_path)

    class _Result:
        returncode = 0

    calls = []

    def fake_run(cmd, **kwargs):
        calls.append(kwargs)
        return _Result()

    import sushicore.proc as proc
    monkeypatch.setattr(proc.subprocess, "run", fake_run)

    assert docker.run() == 0

    assert len(calls) == 1
    assert set(calls[0]) <= {"cwd", "env"}, (
        "Runner.run must not redirect stdio, or `docker run -it` stops being "
        f"interactive: subprocess.run got kwargs {calls[0]}")
