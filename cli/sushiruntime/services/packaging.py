"""Turn a built tree into a redistributable archive.

This is the release action: install to a prefix, then archive that prefix. It
does not check whether the result works -- that question has a pass/fail answer
and so belongs with the tests, as `sr test --suite package`, which installs to
the build tree's own stage directory and then builds and runs a consumer against
it with nothing else on PATH.
"""

from __future__ import annotations

import shutil
from pathlib import Path

from .. import console
from ..config import Config, find_project_root, load_config
from ..env import load_build_env
from .project import _build_dir, _cmake, _run


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



def package(prefix: str | None = None, archive: bool = True) -> int:
    """Install the built tree to a prefix and archive it.

    :param prefix: Where to install. Defaults to build/dist under the project root.
    :param archive: Whether to run CPack over the configured generators.
    :return: 0 when every requested step succeeded, non-zero on the first failure.
    """
    console.header("Project Package")
    root = find_project_root()
    cfg = load_config()
    build_dir = _build_dir(root, asan=False)

    if not (build_dir / "CMakeCache.txt").is_file():
        console.error(f"No configured build tree at {build_dir}. Run `sr build` first.")
        return 1

    stage = Path(cfg.expand(prefix)).resolve() if prefix else root / "build" / "dist"
    env = load_build_env(cfg, build_dir)

    # A stale prefix hides a rule that stopped installing something: the file is
    # still there from last time, so the archive ships a file the current build
    # would no longer produce.
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

    console.success(f"Package ready at {stage}")
    console.info("Run `sr test --suite package` to check it actually works.")
    return 0
