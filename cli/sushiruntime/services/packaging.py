# packaging.py
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026 Sushi Systems
# Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.
# Commercial use requires a licence from Sushi Systems.
"""Turns a built tree into a redistributable archive.

See cli/README.md.
"""

from __future__ import annotations

from pathlib import Path

from .. import console
from ..config import Config, find_project_root, load_config
from ..env import load_build_env
from .project import _build_dir, _DRIVER, _RUNNER
from .stage_tree import StageRules, StageTree, StageTreeError


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


def package(prefix: str | None = None, archive: bool = True, force: bool = False) -> int:
    """Install the built tree to a prefix and archive it.

    :param prefix: Where to install. Defaults to build/dist under the project root.
    :param archive: Whether to run CPack over the configured generators.
    :param force: Whether a prefix holding files this command did not stage is cleared.
    :return: 0 when every requested step succeeded, non-zero on the first failure.
    """
    console.header("Project Package")
    root = find_project_root()
    cfg = load_config()
    build_dir = _build_dir(root, asan=False)

    if not (build_dir / "CMakeCache.txt").is_file():
        console.error(f"No configured build tree at {build_dir}. Run `sr build` first.")
        return 1

    default_stage = root / "build" / "dist"
    stage = Path(cfg.expand(prefix)).resolve() if prefix else default_stage
    tree = StageTree(stage)
    rules = StageRules(project_root=root, home=Path.home(), build_dir=build_dir,
                       default_stage=default_stage)
    try:
        tree.check(rules, force=force)
        env = load_build_env(cfg, build_dir)
        if stage.is_dir():
            console.info(f"Clearing previous stage tree at {stage}")
        tree.reset()
    except StageTreeError as error:
        console.error(str(error))
        return 1

    console.info(f"Installing to {stage}")
    rc = _RUNNER.run([_DRIVER.cmake(cfg), "--install", str(build_dir), "--prefix", str(stage)],
                     root, env)
    if rc != 0:
        console.error("cmake --install failed.")
        return rc

    if archive:
        cpack_config = build_dir / "CPackConfig.cmake"
        if cpack_config.is_file():
            console.info("Building the archive...")
            rc = _RUNNER.run([_cpack(cfg), "--config", str(cpack_config)], build_dir, env)
            if rc != 0:
                console.error("CPack failed.")
                return rc
        else:
            console.warn(f"No {cpack_config.name} in the build tree; skipping the archive.")

    console.success(f"Package ready at {stage}")
    console.info("Run `sr test --suite package` to check it actually works.")
    return 0
