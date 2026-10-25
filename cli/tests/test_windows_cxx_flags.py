# test_windows_cxx_flags.py
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026-present Mustafa Garip & Sushi Systems
# Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.
# Commercial use requires a licence from Sushi Systems.
"""Tests that the Windows configure leaves -fsycl out of CMAKE_CXX_FLAGS and always states the entry.

See cli/README.md.
"""

from pathlib import Path

import pytest

from sushiruntime.services import project

pytestmark = pytest.mark.small


class _StubConfig:
    """Enough of Config for _configure_args_windows; every tool is a bare name."""

    is_windows = True
    cmake_exe = ""
    vcpkg_root = "C:/vcpkg"
    vcpkg_triplet = "x64-windows"
    ninja_exe = "ninja"
    pkgconf_exe = "pkgconf"
    rc_exe = ""
    cxx = ""

    def __init__(self, toolchain: str) -> None:
        self.toolchain = toolchain

    def expand(self, value):
        return value

    def resolved_windows_compiler(self):
        return "clang++"


def _cxx_flags(toolchain: str, asan: bool) -> str:
    """Returns the CMAKE_CXX_FLAGS the Windows configure states, failing when it states none."""
    arguments = project._configure_args_windows(
        _StubConfig(toolchain), Path("build"), "Release", "auto", "OFF", asan=asan, tests=True)
    stated = [argument for argument in arguments if argument.startswith("-DCMAKE_CXX_FLAGS=")]
    assert len(stated) == 1, f"CMAKE_CXX_FLAGS stated {len(stated)} times:\n{arguments}"
    return stated[0].split("=", 1)[1]


@pytest.mark.parametrize("toolchain", ["intel-llvm", "oneapi", "adaptivecpp"])
def test_a_release_configure_states_empty_cxx_flags(toolchain):
    """An empty entry rewrites the -fsycl an older configure left in the cache."""
    assert _cxx_flags(toolchain, asan=False) == ""


def test_an_asan_configure_states_the_asan_flags_only():
    """The ASan flags reach the compiler; -fsycl does not."""
    assert _cxx_flags("intel-llvm", asan=True) == project.ASAN_FLAGS_WINDOWS
    assert _cxx_flags("adaptivecpp", asan=True) == project.ASAN_FLAGS


def test_the_base_flags_are_part_of_the_configure_stamp(monkeypatch):
    """A change to BASE_CXX_FLAGS configures an existing tree again."""
    before = project._configure_stamp("OFF", "Release", "auto", True, "intel-llvm", "")
    monkeypatch.setattr(project, "BASE_CXX_FLAGS", "-fsycl")
    after = project._configure_stamp("OFF", "Release", "auto", True, "intel-llvm", "")
    assert before != after
