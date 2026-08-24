"""Two ways the GPU probes read the machine wrongly, both silent.

Neither of these announced itself. A bad regex accepted a malformed line and produced a
plausible-looking architecture; an unexpanded glob found no toolkit and reported the machine
as CPU-only. Both then flowed into a build that compiled and ran, which is why they needed a
test rather than a fix alone.

`sushiruntime_gpu_versioned_prefixes` is exercised directly against fabricated install trees.
The regex is exercised against the same construct `gpu/Cuda.cmake` uses, rather than by driving
nvidia-smi, because the inputs that matter are the ones no real driver on this machine emits.
"""

import re
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


def _script(tmp_path: Path, body: str) -> str:
    script = tmp_path / "probe.cmake"
    script.write_text(
        f'include("{(GPU_DIR / "GpuBackend.cmake").as_posix()}")\n{body}',
        encoding="utf-8",
    )
    result = subprocess.run(
        [_cmake(), "-P", str(script)], capture_output=True, text=True, check=False
    )
    assert result.returncode == 0, result.stderr
    return result.stdout


def _reported(output: str, key: str) -> list[str]:
    for line in output.splitlines():
        if f"{key}=" in line:
            listed = line.split(f"{key}=", 1)[1].strip()
            return [entry for entry in listed.split(";") if entry]
    raise AssertionError(f"script never reported {key}:\n{output}")


# --- The compute-capability regex ---------------------------------------------


def _capability_pattern() -> str:
    """The regex gpu/Cuda.cmake actually uses, lifted from the file.

    Restating the pattern here would make these cases pass against a copy while the real parse
    reverted underneath them -- a regression test that cannot catch its own regression. Lifting
    it means the file is what is under test.
    """
    source = (GPU_DIR / "Cuda.cmake").read_text(encoding="utf-8")
    match = re.search(r'if\(_sr_line MATCHES "([^"]+)"\)', source)
    assert match, "gpu/Cuda.cmake no longer parses nvidia-smi lines with a MATCHES pattern"
    return match.group(1)


def _matches(tmp_path: Path, line: str) -> str:
    """Run gpu/Cuda.cmake's parse over one nvidia-smi line; empty means rejected."""
    output = _script(
        tmp_path,
        f'set(line "{line}")\n'
        f'if(line MATCHES "{_capability_pattern()}")\n'
        '    message(STATUS "ARCH=${CMAKE_MATCH_1}${CMAKE_MATCH_2}")\n'
        "else()\n"
        '    message(STATUS "ARCH=")\n'
        "endif()\n",
    )
    listed = _reported(output, "ARCH")
    return listed[0] if listed else ""


def test_a_real_compute_capability_still_parses(tmp_path):
    assert _matches(tmp_path, "8.9") == "89"
    assert _matches(tmp_path, "12.0") == "120"


def test_a_separator_that_is_not_a_dot_is_rejected(tmp_path):
    """The defect: "\\." in a quoted argument escapes the character, so the regex got a bare dot.

    A bare dot matches anything, so "8x6" parsed as 86 and passed the ^[0-9]+$ validation
    downstream -- the separator had been dropped by then, leaving nothing to object to. Any
    malformed or unexpectedly formatted line of the shape <digits><anything><digits> would have
    been accepted as an architecture and compiled for.
    """
    assert _matches(tmp_path, "8x6") == ""
    assert _matches(tmp_path, "1a2") == ""
    assert _matches(tmp_path, "8,9") == ""


# --- Versioned toolkit prefixes -----------------------------------------------


def _prefixes(tmp_path: Path, pattern: str) -> list[str]:
    output = _script(
        tmp_path,
        f'sushiruntime_gpu_versioned_prefixes(found "{pattern}")\n'
        'message(STATUS "FOUND=${found}")\n',
    )
    return [Path(entry).parent.name for entry in _reported(output, "FOUND")]


def test_a_versioned_prefix_is_expanded(tmp_path):
    """find_program's PATHS does not glob, so the pattern has to be expanded before it.

    This is the case that made a machine with a real CUDA install report "no accelerator
    toolkit found" and build CPU-only -- the exact outcome the versioned PATHS entries existed
    to prevent.
    """
    for version in ("12.6", "11.8"):
        (tmp_path / "usr" / f"cuda-{version}" / "bin").mkdir(parents=True)

    found = _prefixes(tmp_path, f"{(tmp_path / 'usr').as_posix()}/cuda-*/bin")

    assert set(found) == {"cuda-12.6", "cuda-11.8"}


def test_the_newest_toolkit_comes_first(tmp_path):
    """find_program takes the first match, and several installed toolkits means the newest.

    Ordering has to be NATURAL: compared as text, cuda-9.0 sorts above cuda-12.6, so a plain
    sort would hand the oldest toolkit to a machine that has both.
    """
    for version in ("9.0", "11.8", "12.6"):
        (tmp_path / "usr" / f"cuda-{version}" / "bin").mkdir(parents=True)

    found = _prefixes(tmp_path, f"{(tmp_path / 'usr').as_posix()}/cuda-*/bin")

    assert found == ["cuda-12.6", "cuda-11.8", "cuda-9.0"]


def test_no_match_yields_an_empty_list(tmp_path):
    """The common case on any machine without the toolkit; it must not leave the pattern behind.

    Passing an unexpanded pattern through to find_program is what the old code did, and it
    searched for a directory named with a literal asterisk.
    """
    found = _prefixes(tmp_path, f"{(tmp_path / 'nothing').as_posix()}/cuda-*/bin")

    assert found == []
