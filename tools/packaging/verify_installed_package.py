# verify_installed_package.py
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026-present Mustafa Garip & Sushi Systems
# Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.
# Commercial use requires a licence from Sushi Systems.
"""Verifies that an installed SushiRuntime is usable from outside its tree.

Usage:
    python tools/packaging/verify_installed_package.py <install-prefix> \
        [--compiler <path>] [--cmake <path>] [--ninja <path>] [--work <dir>]

See tools/packaging/README.md.
"""

from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path

CONSUMER_SOURCE_DIR = Path(__file__).resolve().parents[2] / "tests" / "package"
CONSUMER_TARGET = "sushiruntime_package_consumer"
SUCCESS_MARKER = "OK: SushiRuntime"


def minimal_environment(prefix: Path) -> dict[str, str]:
    """Build the environment the installed package must be sufficient for.

    Everything the developer machine happens to provide is dropped; what remains
    is the OS and the prefix's own binary directory, which is exactly what a
    consumer on a clean machine would have after unpacking the archive.

    :param prefix: The install prefix whose bin/ directory the run may use.
    :return: An environment mapping suitable for subprocess.run.
    """
    if os.name == "nt":
        system_root = os.environ.get("SystemRoot", r"C:\Windows")
        path = os.pathsep.join([
            system_root,
            str(Path(system_root) / "system32"),
            str(prefix / "bin"),
        ])
        environment = {
            "SystemRoot": system_root,
            "windir": system_root,
            "PATH": path,
        }
        for optional in ("TEMP", "TMP"):
            if optional in os.environ:
                environment[optional] = os.environ[optional]
        return environment

    return {
        "PATH": os.pathsep.join(["/usr/bin", "/bin", str(prefix / "bin")]),
        "LD_LIBRARY_PATH": str(prefix / "lib"),
        "HOME": os.environ.get("HOME", "/tmp"),
    }


def run(command: list[str], **kwargs) -> int:
    """Run a command, echoing it first so a failure is reproducible by hand.

    :param command: Argument vector to execute.
    :param kwargs: Passed through to subprocess.run.
    :return: The process exit code.
    """
    print("+ " + " ".join(str(part) for part in command), flush=True)
    return subprocess.run(command, **kwargs).returncode


def main() -> int:
    """Configure, build and run the out-of-tree consumer against an install prefix.

    :return: 0 when the consumer builds and then runs in a scrubbed environment,
             1 on any failure, with the reason printed.
    """
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("prefix", type=Path, help="The install prefix to verify.")
    parser.add_argument("--compiler", default=os.environ.get("CXX", "clang++"),
                        help="SYCL compiler the consumer builds with.")
    parser.add_argument("--cmake", default="cmake", help="cmake executable.")
    parser.add_argument("--ninja", default=None, help="ninja executable.")
    parser.add_argument("--work", type=Path, default=None,
                        help="Scratch build directory for the consumer.")
    arguments = parser.parse_args()

    prefix = arguments.prefix.resolve()
    if not prefix.is_dir():
        print(f"[FAIL] no such install prefix: {prefix}")
        return 1

    # A build tree is not an install tree; see tools/packaging/README.md.
    if (prefix / "CMakeCache.txt").is_file():
        print(f"[FAIL] {prefix} is a build tree, not an install prefix.")
        return 1

    work = (arguments.work or prefix.parent / "pkg-consumer").resolve()
    if work.exists():
        shutil.rmtree(work)

    configure = [
        arguments.cmake,
        "-S", str(CONSUMER_SOURCE_DIR),
        "-B", str(work),
        "-G", "Ninja",
        "-DCMAKE_BUILD_TYPE=Release",
        f"-DCMAKE_PREFIX_PATH={prefix.as_posix()}",
        f"-DCMAKE_CXX_COMPILER={arguments.compiler}",
        "-DCMAKE_CXX_FLAGS=-fsycl",
    ]
    if arguments.ninja:
        configure.append(f"-DCMAKE_MAKE_PROGRAM={arguments.ninja}")

    if run(configure) != 0:
        print("[FAIL] the consumer could not configure against the package.")
        return 1

    if run([arguments.cmake, "--build", str(work)]) != 0:
        print("[FAIL] the consumer could not build against the package.")
        return 1

    executable = work / CONSUMER_TARGET
    if os.name == "nt":
        executable = executable.with_suffix(".exe")
    if not executable.is_file():
        print(f"[FAIL] the consumer built but produced no {executable.name}.")
        return 1

    print(f"+ running {executable.name} with a scrubbed environment", flush=True)
    completed = subprocess.run([str(executable)],
                               env=minimal_environment(prefix),
                               capture_output=True, text=True)
    output = completed.stdout + completed.stderr
    print(output, end="" if output.endswith("\n") else "\n", flush=True)

    if completed.returncode != 0:
        print(f"[FAIL] the consumer exited {completed.returncode} with nothing on "
              f"PATH but the install prefix. The package is missing a library it "
              f"needs at run time; this machine's own PATH was hiding that.")
        return 1

    if SUCCESS_MARKER not in output:
        print(f"[FAIL] the consumer ran but never printed '{SUCCESS_MARKER}'.")
        return 1

    print(f"[OK] {prefix} builds and runs an out-of-tree consumer unaided.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
