# test_rocm_route.py
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026-present Mustafa Garip & Sushi Systems
# Licensed under the Apache License 2.0. See LICENSE.
"""Tests that the ROCm route is found whole, stated to CMake and to a run, and absent when partial.

See cli/README.md.
"""

from pathlib import Path

import pytest

from sushiruntime.rocm_route import RocmRoute, tree_offloads_to_rocm

pytestmark = pytest.mark.small


def _touch(path: Path) -> Path:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(b"")
    return path


def _install(base: Path, toolkit=True, bitcode=True, adapter=True) -> None:
    if toolkit:
        _touch(base / "rocm" / "10.1.0" / "bin" / "hipcc.exe")
    if bitcode:
        _touch(base / "rocm" / "devlibs-7.0" / "opt" / "rocm-7.0.0" / "lib" / "llvm" / "lib"
               / "clang" / "20" / "lib" / "amdgcn" / "bitcode" / "hip.bc")
    if adapter:
        _touch(base / "ur" / "hip-abc" / "bin" / "ur_adapter_hip.dll")


def test_a_complete_install_is_found(tmp_path):
    _install(tmp_path)

    route = RocmRoute.discover([tmp_path])

    assert route is not None
    assert route.root == tmp_path / "rocm" / "10.1.0"
    assert route.device_libraries.name == "bitcode"
    assert route.adapter.name == "ur_adapter_hip.dll"


@pytest.mark.parametrize("missing", ["toolkit", "bitcode", "adapter"])
def test_a_missing_piece_leaves_the_build_unchanged(tmp_path, missing):
    _install(tmp_path, **{missing: False})

    assert RocmRoute.discover([tmp_path]) is None


def test_the_second_root_is_read_when_the_first_has_nothing(tmp_path):
    (tmp_path / "first").mkdir()
    _install(tmp_path / "second")

    assert RocmRoute.discover([tmp_path / "first", tmp_path / "second"]) is not None


def test_the_route_reaches_cmake_with_the_stated_isa(tmp_path):
    _install(tmp_path)
    route = RocmRoute.discover([tmp_path])

    assert route.configure_arguments({}) == [
        f"-DSR_ROCM_PATH={(tmp_path / 'rocm' / '10.1.0').as_posix()}",
        f"-DSR_ROCM_DEVICE_LIBS={route.device_libraries.as_posix()}"]
    assert "-DSR_AMD_ARCH=gfx1036" in route.configure_arguments({"SR_AMD_ARCH": "gfx1036"})


def test_the_force_load_list_is_quoted_and_leaves_out_an_absent_adapter(tmp_path):
    _install(tmp_path)
    bin_directory = tmp_path / "toolchain" / "bin"
    _touch(bin_directory / "ur_adapter_cuda.dll")
    route = RocmRoute.discover([tmp_path])

    assert route.force_load_value(bin_directory) == (
        f'"{route.adapter}","{bin_directory / "ur_adapter_cuda.dll"}"')


def test_only_a_tree_that_resolved_rocm_offloads_to_it(tmp_path):
    cache = tmp_path / "CMakeCache.txt"
    assert not tree_offloads_to_rocm(tmp_path)

    cache.write_text("SR_GPU_BACKEND_RESOLVED:INTERNAL=cuda;rocm\n", encoding="utf-8")
    assert tree_offloads_to_rocm(tmp_path)

    cache.write_text("SR_GPU_BACKEND_RESOLVED:INTERNAL=cpu\n", encoding="utf-8")
    assert not tree_offloads_to_rocm(tmp_path)
