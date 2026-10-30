# library_packages.py
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026-present Mustafa Garip & Sushi Systems
# Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.
# Commercial use requires a licence from Sushi Systems.
"""Takes the Sushi libraries `sr` links as built packages and puts their run-time folders on the path.

See cli/README.md.
"""

from __future__ import annotations

from collections.abc import Callable, Mapping, Sequence
from pathlib import Path

from sushicore.build_tree import package_build_type
from sushicore.provision.sushi_packages import PackageSet, RunDirs

from ..config import Config

#: The file of a build tree that names the run-time folders of the packages it linked.
K_RUN_DIRS_FILE = ".sushiruntime_package_dirs.json"

#: The build type whose tree is ThreadSanitizer-instrumented.
K_THREAD_TYPE = "tsan"

#: The package build type of a ThreadSanitizer tree; TSan needs the symbols to name a race.
K_THREAD_PACKAGE_TYPE = "relwithdebinfo"

#: The libraries a ThreadSanitizer tree links, each built with ``--sanitize thread``.
K_THREAD_LIBRARIES = ("sushilog", "sushiprofiler")


def resolve(cfg: Config, root: Path, build_type: str, compiler: str,
            report: Callable[[str], None], warn: Callable[[str], None]) -> PackageSet:
    """Return a built package of every library the runtime links, building a missing one.

    The runtime passes no option to a library, so a library builds as its CLI defaults, except
    for a ``tsan`` tree: TSan cannot see a synchronisation edge that runs inside an
    uninstrumented library, so SushiLog and SushiProfiler are built with ``--sanitize thread``
    as RelWithDebInfo packages.

    Args:
        cfg: The resolved configuration.
        root: The project root.
        build_type: The build type of this configure, such as ``release`` or ``asan``.
        compiler: The compiler of this build; the library packages are built with it.
        report: Receives one ``reused`` or ``built`` line per library.
        warn: Receives the warnings of the resolver.

    Raises:
        sushicore.provision.sushi_packages.PackageError: A package cannot be chosen.
    """
    options: Mapping[str, tuple[str, ...]] = {}
    package_type = package_build_type(build_type)
    if build_type == K_THREAD_TYPE:
        package_type = K_THREAD_PACKAGE_TYPE
        options = {name: ("--sanitize", "thread") for name in K_THREAD_LIBRARIES}
    return cfg.sushi_packages(root, package_type, options,
                              report=report, warn=warn, compiler=compiler)


#: The run-time folders of the packages a tree linked, kept in the tree for the commands that run it.
_RUN_DIRS = RunDirs(K_RUN_DIRS_FILE)

def record_run_dirs(tree: Path, dirs: Sequence[str]) -> None:
    """Write the run-time folders of the packages *tree* linked, for the commands that run it."""
    _RUN_DIRS.save(tree, dirs)


def read_run_dirs(tree: Path) -> list[str]:
    """Return the folders :func:`record_run_dirs` wrote for *tree*, none when it wrote none."""
    return _RUN_DIRS.read(tree)


def prepend_run_dirs(cfg: Config, env: dict[str, str], dirs: Sequence[str]) -> None:
    """Put *dirs* first on the search path of *env*, where a binary finds the package libraries."""
    _RUN_DIRS.prepend(env, dirs, cfg.is_windows)
