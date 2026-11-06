# test_gpu_backend_delegation.py
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026-present Mustafa Garip & Sushi Systems
# Licensed under the Apache License 2.0. See LICENSE.
"""Tests that the Windows configure arguments carry the resolved GPU backend.

See cli/README.md.
"""

from pathlib import Path

import pytest

from sushiruntime.config import Config
from sushiruntime.services import project

pytestmark = pytest.mark.small


def _backend(arguments: list[str]) -> str:
    for argument in arguments:
        if argument.startswith("-DSR_GPU_BACKEND="):
            return argument.split("=", 1)[1]
    raise AssertionError(f"no GPU backend stated:\n{arguments}")


def _windows_args(gpu_backend: str) -> list[str]:
    return project._configure_args_windows(
        "cmake", Config(platform="windows", vcpkg_root="C:/vcpkg"), Path("build"), "Release",
        gpu_backend, "OFF", asan=False, tests=True)


@pytest.mark.parametrize("resolved", ["auto", "cpu"])
def test_the_windows_configure_carries_the_resolved_backend(resolved):
    """The configure states the backend it was given: "auto", or "cpu" from --no-cuda and ASan.

    The defect: this argument was the literal string "cpu", whatever had been resolved.
    """
    assert _backend(_windows_args(resolved)) == resolved
