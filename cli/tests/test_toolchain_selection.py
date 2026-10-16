# test_toolchain_selection.py
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026-present Mustafa Garip & Sushi Systems
# Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.
# Commercial use requires a licence from Sushi Systems.
"""Tests that a stated toolchain survives in CMake and changes the configure stamp.

See cli/README.md.
"""

import re
import shutil
import subprocess

from pathlib import Path

import pytest

from sushiruntime.services.project import _configure_stamp

pytestmark = pytest.mark.medium


CMAKE_DIR = Path(__file__).resolve().parents[2] / "cmake"


def _cmake() -> str:
    exe = shutil.which("cmake")
    if exe is None:
        pytest.skip("no cmake on PATH")
    return exe


def _select(tmp_path: Path, compiler: str, stated: str | None = None) -> tuple[str, str]:
    """Run Sycl.cmake's phase-1 selection and report the toolchain and any warning.

    Only the selection block runs: the file's later phases look for a real compiler, and what
    is under test is which toolchain name comes out of a compiler *path*, which needs no
    compiler to exist.

    Each call gets its own tree. Sharing one would make the second configure a *re*configure,
    where the cache entry survives from the first and inference is correctly skipped -- so the
    calls would quietly measure something other than what they say.
    """
    tmp_path = tmp_path / re.sub(r"\W+", "_", f"{compiler}_{stated}")
    tmp_path.mkdir(parents=True, exist_ok=True)

    source = tmp_path / "project"
    source.mkdir(exist_ok=True)
    (source / "CMakeLists.txt").write_text(
        "cmake_minimum_required(VERSION 3.20)\n"
        "project(toolchain NONE)\n"
        f'set(CMAKE_CXX_COMPILER "{compiler}")\n'
        f'include("{(CMAKE_DIR / "Sycl.cmake").as_posix()}")\n'
        'message(STATUS "TOOLCHAIN=${SR_SYCL_TOOLCHAIN}")\n'
        "get_property(doc CACHE SR_SYCL_TOOLCHAIN PROPERTY HELPSTRING)\n"
        'message(STATUS "DOC=${doc}")\n',
        encoding="utf-8",
    )

    arguments = [_cmake(), "-S", str(source), "-B", str(tmp_path / "build")]
    if stated is not None:
        arguments.append(f"-DSR_SYCL_TOOLCHAIN={stated}")

    result = subprocess.run(arguments, capture_output=True, text=True, check=False)
    assert result.returncode == 0, result.stderr

    selected = ""
    documented = ""
    for line in result.stdout.splitlines():
        if "TOOLCHAIN=" in line:
            selected = line.split("TOOLCHAIN=", 1)[1].strip()
        elif "DOC=" in line:
            documented = line.split("DOC=", 1)[1].strip()
    return selected, documented + result.stderr


def test_a_stated_toolchain_survives_a_pinned_compiler(tmp_path):
    """The defect: this was silently rewritten to "oneapi" because icpx was pinned."""
    selected, _ = _select(tmp_path, "/opt/intel/bin/icpx", stated="adaptivecpp")

    assert selected == "adaptivecpp"


def test_a_mismatch_between_the_two_is_reported(tmp_path):
    """Obeying the request means it can now be wrong, and failing later names neither value."""
    _, output = _select(tmp_path, "/opt/intel/bin/icpx", stated="adaptivecpp")
    flattened = " ".join(output.split())

    assert "was requested" in flattened
    assert "oneapi" in flattened


def test_inference_still_runs_when_no_toolchain_was_stated(tmp_path):
    """The back-compat path the guard exists for: acpp and icpx invocations keep working."""
    assert _select(tmp_path, "/usr/bin/acpp")[0] == "adaptivecpp"
    assert _select(tmp_path, "/opt/intel/bin/icpx")[0] == "oneapi"
    assert _select(tmp_path, "/opt/llvm/bin/clang++")[0] == "intel-llvm"


def test_a_matching_pair_does_not_warn(tmp_path):
    """Stating what would have been inferred is agreement, not a mismatch."""
    _, output = _select(tmp_path, "/usr/bin/acpp", stated="adaptivecpp")

    assert "was requested" not in " ".join(output.split())


def test_the_cache_help_text_survives_inference(tmp_path):
    """Inference used to FORCE the entry with an empty docstring, wiping the help off it."""
    _, output = _select(tmp_path, "/usr/bin/acpp")

    assert "intel-llvm (primary)" in output


def test_the_stamp_changes_with_the_toolchain():
    """The other half: an unchanged stamp means configure is skipped and the tree reused."""
    before = _configure_stamp("OFF", "Release", "auto", True, "intel-llvm", "")
    after = _configure_stamp("OFF", "Release", "auto", True, "adaptivecpp", "")

    assert before != after


def test_the_stamp_changes_with_a_pinned_compiler():
    """Editing `cxx` in config.local.toml is the same class of change and was equally invisible."""
    before = _configure_stamp("OFF", "Release", "auto", True, "intel-llvm", "/a/clang++")
    after = _configure_stamp("OFF", "Release", "auto", True, "intel-llvm", "/b/clang++")

    assert before != after


def test_the_stamp_changes_with_a_pinned_library_checkout():
    """A SushiLog pin resolved to another checkout reaches CMake only through a reconfigure."""
    before = _configure_stamp("OFF", "Release", "auto", True, "intel-llvm", "",
                              ("-DSUSHILOG_DIR=/a/sushilog",))
    after = _configure_stamp("OFF", "Release", "auto", True, "intel-llvm", "",
                             ("-DSUSHILOG_DIR=/b/sushilog",))

    assert before != after


def test_the_stamp_is_stable_for_an_unchanged_configuration():
    """It has to keep skipping the configure it was invented to skip."""
    first = _configure_stamp("OFF", "Release", "auto", True, "intel-llvm", "/a/clang++")
    second = _configure_stamp("OFF", "Release", "auto", True, "intel-llvm", "/a/clang++")

    assert first == second
