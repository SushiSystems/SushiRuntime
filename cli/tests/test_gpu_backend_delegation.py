# test_gpu_backend_delegation.py
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026 Sushi Systems
# Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.
# Commercial use requires a licence from Sushi Systems.
"""Detection is one question, asked in one place, on every platform.

Windows was frozen to `-DSR_GPU_BACKEND=cpu` here, on the grounds that the host has no nvptx
toolchain. That stopped being true -- this workspace's intel/llvm bundle compiles
`-fsycl-targets=spir64,nvidia_gpu_sm_89` against the installed CUDA toolkit, and `hub install`
offers that toolkit as a component -- so the freeze meant provisioning a toolkit the build could
never use, with no flag to override it.

Worse, it made the two CLIs disagree about the same machine: SushiEngine passed no backend at
all and so got cmake/gpu/DetectGpu's "auto", while SushiRuntime forced "cpu". One checkout, one
host, two answers about which hardware to target.

Both CLIs delegate to DetectGpu now. These tests pin the delegation rather than the answer,
because the answer is the part that is supposed to depend on the machine.
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
