"""Install / archive / verify the built tree as a consumable package.

`sr build` produces a build tree; this turns one into an install prefix, an
optional archive, and -- the part that carries the value -- a verdict on whether
that prefix actually works for someone outside the source tree.
"""

from __future__ import annotations

import shutil
import sys
from pathlib import Path

from .. import console
from ..config import Config, find_project_root, load_config
from ..env import load_build_env
from .project import _build_dir, _cmake, _run

ORACLE = Path("tools") / "packaging" / "verify_installed_package.py"


def _cpack(cfg: Config) -> str:
    """The cpack executable, resolved beside the configured cmake.

    CPack ships with CMake, so a project that pins cmake to a specific install
    and then takes cpack from PATH can end up running two different versions.

    :param cfg: Loaded CLI configuration.
    :return: A path to cpack, or the bare name when cmake is not pinned either.
    """
    if not cfg.cmake_exe:
        return "cpack"
    cmake_path = Path(cfg.expand(cfg.cmake_exe))
    candidate = cmake_path.with_name("cpack" + cmake_path.suffix)
    return str(candidate) if candidate.is_file() else "cpack"


def _cache_value(build_dir: Path, key: str) -> str | None:
    """Read a single CMake cache entry from a configured build tree.

    The consumer must compile with the same SYCL compiler the package was built
    with, since the runtime's kernels are header templates instantiated in the
    consumer's own translation units. Taking it from the cache rather than
    re-deriving it from config guarantees they cannot disagree.

    :param build_dir: A configured build tree.
    :param key: The cache variable name, without its type suffix.
    :return: The cached value, or None when the tree has no such entry.
    """
    cache = build_dir / "CMakeCache.txt"
    if not cache.is_file():
        return None
    prefix = key + ":"
    for line in cache.read_text(encoding="utf-8", errors="replace").splitlines():
        if line.startswith(prefix) and "=" in line:
            return line.split("=", 1)[1].strip()
    return None


def package(prefix: str | None = None, archive: bool = True,
            verify: bool = True) -> int:
    """Install the built tree to a prefix, archive it, and verify it is usable.

    :param prefix: Where to install. Defaults to build/stage under the project root.
    :param archive: Whether to run CPack over the configured generators.
    :param verify: Whether to run the packaging oracle against the install prefix.
    :return: 0 when every requested step succeeded, non-zero on the first failure.
    """
    console.header("Project Package")
    root = find_project_root()
    cfg = load_config()
    build_dir = _build_dir(root, asan=False)

    if not (build_dir / "CMakeCache.txt").is_file():
        console.error(f"No configured build tree at {build_dir}. Run `sr build` first.")
        return 1

    stage = Path(cfg.expand(prefix)).resolve() if prefix else root / "build" / "stage"
    env = load_build_env(cfg, build_dir)

    # A stale prefix hides a rule that stopped installing something: the file is
    # still there from last time and the oracle passes on a package the current
    # build would not produce.
    if stage.is_dir():
        console.info(f"Clearing previous stage tree at {stage}")
        shutil.rmtree(stage, ignore_errors=True)

    console.info(f"Installing to {stage}")
    rc = _run([_cmake(cfg), "--install", str(build_dir), "--prefix", str(stage)],
              env, cwd=root)
    if rc != 0:
        console.error("cmake --install failed.")
        return rc

    if archive:
        cpack_config = build_dir / "CPackConfig.cmake"
        if cpack_config.is_file():
            console.info("Building the archive...")
            rc = _run([_cpack(cfg), "--config", str(cpack_config)], env, cwd=build_dir)
            if rc != 0:
                console.error("CPack failed.")
                return rc
        else:
            console.warn(f"No {cpack_config.name} in the build tree; skipping the archive.")

    if verify:
        console.info("Verifying the installed package...")
        command = [sys.executable, str(root / ORACLE), str(stage)]

        compiler = _cache_value(build_dir, "CMAKE_CXX_COMPILER")
        if compiler:
            command += ["--compiler", compiler]
        command += ["--cmake", _cmake(cfg)]
        make_program = _cache_value(build_dir, "CMAKE_MAKE_PROGRAM")
        if make_program:
            command += ["--ninja", make_program]

        rc = _run(command, env, cwd=root)
        if rc != 0:
            console.error("The installed package did not verify.")
            return rc

    console.success(f"Package ready at {stage}")
    return 0
