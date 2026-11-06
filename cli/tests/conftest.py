# conftest.py
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026-present Mustafa Garip & Sushi Systems
# Licensed under the Apache License 2.0. See LICENSE.
"""Makes the real sushicore importable for the SushiRuntime CLI test suite.

See cli/README.md.
"""

from __future__ import annotations

import importlib
import importlib.util
import os
import sys
from pathlib import Path

import pytest


def _ensure_sushicore() -> None:
    """Make :mod:`sushicore` importable, or fail with a message that says how.

    Already installed (CI does `pip install -e <stack>/sushicore`) wins. Failing
    that, it is found on disk: a workspace puts it at ``<workspace>/sushicore``,
    a developer's flat checkout layout at ``<siblings>/sushistack/sushicore``.
    """
    if importlib.util.find_spec("sushicore") is not None:
        return

    repo = Path(__file__).resolve().parents[2]
    for candidate in (repo.parent / "sushicore", repo.parent / "sushistack" / "sushicore"):
        if (candidate / "sushicore" / "__init__.py").is_file():
            sys.path.insert(0, str(candidate))
            importlib.invalidate_caches()
            if importlib.util.find_spec("sushicore") is not None:
                return

    raise RuntimeError(
        "sushicore is not importable, so the sushiruntime CLI cannot be imported. "
        "It ships in the SushiStack repository: install it with "
        "`pip install -e <sushistack>/sushicore`, or place a SushiStack "
        "checkout next to this one."
    )


_ensure_sushicore()


K_SIZE_TIMEOUTS = {"small": 60, "medium": 300, "large": 900}


def _sizes_of(item: pytest.Item) -> list[str]:
    """Returns the size labels *item* carries, one entry per label."""
    return [mark.name for mark in item.iter_markers() if mark.name in K_SIZE_TIMEOUTS]


def pytest_collection_modifyitems(items: list[pytest.Item]) -> None:
    """Fails collection when a test carries no size or more than one, and gives each its timeout."""
    wrong = [f"{item.nodeid}: {_sizes_of(item) or 'no size'}"
             for item in items if len(_sizes_of(item)) != 1]
    if wrong:
        raise pytest.UsageError(f"every test carries exactly one size: {', '.join(wrong)}")
    for item in items:
        item.add_marker(pytest.mark.timeout(K_SIZE_TIMEOUTS[_sizes_of(item)[0]]))


from dataclasses import dataclass, field  # noqa: E402
from typing import Callable  # noqa: E402

from sushicore.console_memory import RecordingConsole  # noqa: E402
from sushicore.proc_memory import RecordingRunner  # noqa: E402
from sushicore.provision.package_install import CapturedRun  # noqa: E402

from sushiruntime.config import Config  # noqa: E402
from sushiruntime.services.surroundings import Surroundings  # noqa: E402

K_LOG_DIR = "/store/sushilog/abc/prefix/lib/cmake/SushiLog"
K_LOG_LIB = "/store/sushilog/abc/prefix/lib"


class OnePackage:
    """The package set of one library, SushiLog, as sushicore's resolver would answer it."""

    #: The closure libraries a filter skipped; this set skips none.
    left_out: tuple[str, ...] = ()

    def names(self) -> list[str]:
        """Returns the library names."""
        return ["sushilog"]

    def definitions(self) -> list[str]:
        """Returns the one package definition."""
        return [f"-DSushiLog_DIR={K_LOG_DIR}"]

    def run_dirs(self, platform: str) -> list[str]:
        """Returns the one run-time folder."""
        return [K_LOG_LIB]


class PackageDesk:
    """Answers each package request with the one package and keeps the requests."""

    def __init__(self) -> None:
        """Starts with no request."""
        self.asked: list[dict] = []

    def __call__(self, root, build_type, options=None, report=print, warn=print,
                 compiler="") -> OnePackage:
        """Keeps one request and answers it."""
        self.asked.append({"root": root, "build_type": build_type,
                           "options": dict(options or {}), "compiler": compiler})
        return OnePackage()


@dataclass
class Rig:
    """A command's surroundings over a temporary checkout, and what they recorded."""

    root: Path
    console: RecordingConsole
    runner: RecordingRunner
    surroundings: Surroundings
    packages: PackageDesk
    captured: list[list[str]] = field(default_factory=list)

    def argv(self, index: int = 0) -> list[str]:
        """Returns the words of the command run at *index*."""
        return list(self.runner.commands[index].argv)


@pytest.fixture
def rig(tmp_path, monkeypatch) -> Callable[..., Rig]:
    """Returns a builder of a Rig; no command is spawned and a compiler sits on the PATH."""
    compilers = tmp_path / "compilers"
    compilers.mkdir()
    for name in ("clang++", "clang"):
        compiler = compilers / (name + (".exe" if os.name == "nt" else ""))
        compiler.write_text("", encoding="utf-8")
        compiler.chmod(0o755)
    monkeypatch.setenv("PATH", os.pathsep.join([str(compilers), os.environ.get("PATH", "")]))

    def build(*, cfg: Config | None = None, capture_code: int = 0,
              capture_output: str = "") -> Rig:
        console = RecordingConsole()
        runner = RecordingRunner(console)
        root = tmp_path / "checkout"
        root.mkdir(exist_ok=True)
        captured: list[list[str]] = []
        packages = PackageDesk()

        def capture(command, cwd, env=None) -> CapturedRun:
            """Keeps the command and answers with the scripted code and output."""
            captured.append(list(command))
            return CapturedRun(capture_code, capture_output)

        surroundings = Surroundings(
            console=console, runner=runner, root=root,
            cfg=cfg or Config(platform="linux", generator="Ninja", cxx="clang++"),
            provision_packages=packages, capture=capture)
        return Rig(root, console, runner, surroundings, packages, captured)

    return build


@pytest.fixture
def doxygen_exits_with(monkeypatch) -> Callable[[int, Path], None]:
    """Returns a function that makes *tmp* a checkout whose configured doxygen exits with a code.

    The checkout holds the root marker and a Doxyfile; the program is a script that ignores its
    arguments, so the exit code of `sr docs` is the code given.
    """

    def prepare(code: int, tmp: Path) -> None:
        """Writes the checkout, the doxygen script and names the script as the doxygen of the configuration."""
        (tmp / "sushi-module.toml").write_text("", encoding="utf-8")
        (tmp / ".config" / "doxygen").mkdir(parents=True)
        (tmp / ".config" / "doxygen" / "Doxyfile").write_text("", encoding="utf-8")
        bin_dir = tmp / "doxygen-bin"
        bin_dir.mkdir()
        if os.name == "nt":
            script = bin_dir / "doxygen.bat"
            script.write_text(f"@exit /b {code}\r\n", encoding="utf-8")
        else:
            script = bin_dir / "doxygen"
            script.write_text(f"#!/bin/sh\nexit {code}\n", encoding="utf-8")
            script.chmod(0o755)
        monkeypatch.setenv("SR_DOXYGEN", str(script))

    return prepare
