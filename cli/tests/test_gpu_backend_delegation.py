# test_gpu_backend_delegation.py
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026-present Mustafa Garip & Sushi Systems
# Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.
# Commercial use requires a licence from Sushi Systems.
"""Tests that the Windows configure arguments carry the resolved GPU backend.

See cli/README.md.
"""

from pathlib import Path

from sushiruntime.services import project


class _StubConfig:
    """Enough of Config for _configure_args_windows; every tool is a bare name."""

    is_windows = True
    cmake_exe = ""
    toolchain = "intel-llvm"
    vcpkg_root = "C:/vcpkg"
    vcpkg_triplet = "x64-windows"
    ninja_exe = "ninja"
    pkgconf_exe = "pkgconf"
    rc_exe = ""
    cxx = ""

    def expand(self, value):
        return value

    def resolved_windows_compiler(self):
        return "clang++"


def _backend(arguments: list[str]) -> str:
    for argument in arguments:
        if argument.startswith("-DSR_GPU_BACKEND="):
            return argument.split("=", 1)[1]
    raise AssertionError(f"no GPU backend stated:\n{arguments}")


def _windows_args(gpu_backend: str) -> list[str]:
    return project._configure_args_windows(
        _StubConfig(), Path("build"), "Release", gpu_backend, "OFF", asan=False, tests=True)


def test_the_windows_configure_carries_the_resolved_backend():
    """The defect: this argument was the literal string "cpu", whatever had been resolved."""
    assert _backend(_windows_args("auto")) == "auto"


def test_the_windows_configure_can_still_be_told_cpu():
    """--no-cuda and ASan resolve to "cpu", and that has to reach CMake unchanged."""
    assert _backend(_windows_args("cpu")) == "cpu"
