# test_gpu_backend_resolution.py
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026 Sushi Systems
# Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.
# Commercial use requires a licence from Sushi Systems.
"""GPU backend selection resolves a set of vendors, and drops the ones with no hardware.

`cmake/gpu/DetectGpu.cmake` used to elect one vendor: an if/elseif chain that looked for nvcc
and, having found it, never looked for ROCm. A machine holding both cards silently built for
one of them -- the exact guess `gpu/Cuda.cmake` refuses to make between two NVIDIA cards, made
one level up where nothing was watching. Selection is a set now, and these tests pin both
halves of that: every requested vendor is targeted, and a vendor whose hardware never answers
drops back out instead of poisoning the build with a triple it cannot fill.

The probes are steered rather than mocked, and the host is cut out of the answer entirely.
Every find_program in gpu/ is disarmed by turning off the system search paths, so a probe
succeeds only where a test preset its cache variable -- otherwise the machine running the
suite decides the result, and a developer with a CUDA toolkit installed sees different
selections than CI does. Presetting SR_NVCC / SR_HIPCC is what find_program already honours,
and presetting SR_CUDA_ARCH / SR_AMD_ARCH is the documented way to state an architecture
rather than probe for one, so a test can put a vendor in either state without owning a card.
"""

import shutil
import subprocess

from pathlib import Path

import pytest


GPU_DIR = Path(__file__).resolve().parents[2] / "cmake" / "gpu"


def _cmake() -> str:
    exe = shutil.which("cmake")
    if exe is None:
        pytest.skip("no cmake on PATH")
    return exe


def _configure(tmp_path: Path, **cache) -> subprocess.CompletedProcess:
    source = tmp_path / "project"
    source.mkdir()
    (source / "CMakeLists.txt").write_text(
        "cmake_minimum_required(VERSION 3.20)\n"
        "project(gpu NONE)\n"
        'set(SR_SYCL_TOOLCHAIN "intel-llvm")\n'
        # Hermetic probes: without these, find_program(nvcc) and find_program(nvidia-smi)
        # answer from whatever the host has installed, and the decline tests below would
        # pass or fail depending on whose machine ran them.
        "set(CMAKE_FIND_USE_SYSTEM_ENVIRONMENT_PATH FALSE)\n"
        "set(CMAKE_FIND_USE_CMAKE_SYSTEM_PATH FALSE)\n"
        f'include("{(GPU_DIR / "GpuBackend.cmake").as_posix()}")\n'
        f'include("{(GPU_DIR / "DetectGpu.cmake").as_posix()}")\n'
        'message(STATUS "RESOLVED=${SR_GPU_BACKEND_RESOLVED}")\n'
        'message(STATUS "TRIPLES=${SR_GPU_CLANG_TRIPLES}")\n',
        encoding="utf-8",
    )

    arguments = [_cmake(), "-S", str(source), "-B", str(tmp_path / "build")]
    arguments += [f"-D{name}={value}" for name, value in cache.items()]

    return subprocess.run(arguments, capture_output=True, text=True, check=False)


def _flat(text: str) -> str:
    """CMake reflows a diagnostic across lines, so a phrase is only contiguous without them."""
    return " ".join(text.split())


def _reported(result: subprocess.CompletedProcess, key: str) -> list[str]:
    for line in result.stdout.splitlines():
        marker = f"{key}="
        if marker in line:
            return [entry for entry in line.split(marker, 1)[1].strip().split(";") if entry]
    raise AssertionError(f"configure never reported {key}:\n{result.stdout}\n{result.stderr}")


def test_an_explicit_list_targets_every_vendor_in_it(tmp_path):
    """The request that used to be rejected outright is the one this change exists for."""
    result = _configure(
        tmp_path,
        SR_GPU_BACKEND="cuda;rocm",
        SR_CUDA_ARCH="89",
        SR_AMD_ARCH="gfx1030",
    )

    assert result.returncode == 0, result.stderr
    assert _reported(result, "RESOLVED") == ["cuda", "rocm"]
    assert _reported(result, "TRIPLES") == [
        "spir64",
        "nvidia_gpu_sm_89",
        "amd_gpu_gfx1030",
    ]


def test_auto_requests_every_toolkit_it_finds(tmp_path):
    """Auto-detect stops at the first toolkit no longer -- it collects them.

    Both arches are stated so the vendor files skip their hardware probes; what is under test
    here is the request auto builds, not what the cards report.
    """
    result = _configure(
        tmp_path,
        SR_NVCC="/fake/nvcc",
        SR_HIPCC="/fake/hipcc",
        SR_CUDA_ARCH="89",
        SR_AMD_ARCH="gfx1030",
    )

    assert result.returncode == 0, result.stderr
    assert _reported(result, "RESOLVED") == ["cuda", "rocm"]


def test_a_vendor_with_no_hardware_drops_out_and_the_others_survive(tmp_path):
    """Finding a toolkit proves a compiler, not a card, and the two must not share a fate.

    This is the case that makes auto safe to widen: a machine with both toolkits installed and
    one card present builds for the card it has, rather than for a GPU that is not there.
    """
    result = _configure(
        tmp_path,
        SR_NVCC="/fake/nvcc",
        SR_HIPCC="/fake/hipcc",
        SR_CUDA_ARCH="89",
    )

    assert result.returncode == 0, result.stderr
    assert _reported(result, "RESOLVED") == ["cuda"]
    assert _reported(result, "TRIPLES") == ["spir64", "nvidia_gpu_sm_89"]
    assert "amdgcn" not in result.stdout


def test_every_vendor_declining_falls_back_to_cpu(tmp_path):
    """A guessed set that empties out is a CPU build, and must be reported as one.

    Reporting it as 'rocm' would tell the reader this build carries an amdgcn triple it does
    not have.
    """
    result = _configure(tmp_path, SR_HIPCC="/fake/hipcc")

    assert result.returncode == 0, result.stderr
    assert _reported(result, "RESOLVED") == ["cpu"]
    assert _reported(result, "TRIPLES") == ["spir64"]


def test_a_named_vendor_with_no_hardware_fails_instead_of_declining(tmp_path):
    """Declining is for a guess. Naming a vendor and not its architecture is the omission."""
    result = _configure(tmp_path, SR_GPU_BACKEND="rocm")

    assert result.returncode != 0
    assert "SR_AMD_ARCH could not be resolved" in _flat(result.stderr)


def test_cpu_cannot_be_combined_with_a_vendor(tmp_path):
    """'cpu' is not a member of the set; it is the statement that the set is empty."""
    result = _configure(tmp_path, SR_GPU_BACKEND="cuda;cpu", SR_CUDA_ARCH="89")

    assert result.returncode != 0
    assert "is not one backend among others" in _flat(result.stderr)


def test_an_unknown_vendor_is_rejected(tmp_path):
    """A typo must not be read as a vendor that silently contributes nothing."""
    result = _configure(tmp_path, SR_GPU_BACKEND="cuda;nvidia", SR_CUDA_ARCH="89")

    assert result.returncode != 0
    assert "which is not a GPU backend" in _flat(result.stderr)


def test_cpu_alone_still_resolves_to_cpu(tmp_path):
    """The terminal value keeps working, and keeps producing no offload triple."""
    result = _configure(tmp_path, SR_GPU_BACKEND="cpu")

    assert result.returncode == 0, result.stderr
    assert _reported(result, "RESOLVED") == ["cpu"]
    assert _reported(result, "TRIPLES") == ["spir64"]


def test_the_legacy_switch_still_names_cuda(tmp_path):
    """SR_ENABLE_CUDA=ON is the old spelling of an explicit request and stays one.

    It has to survive the rewrite as *explicit*, or a machine using it would start declining
    where it used to fail, which is the quiet downgrade the flag's own comment warns about.
    """
    result = _configure(tmp_path, SR_ENABLE_CUDA="ON", SR_CUDA_ARCH="89")

    assert result.returncode == 0, result.stderr
    assert _reported(result, "RESOLVED") == ["cuda"]
