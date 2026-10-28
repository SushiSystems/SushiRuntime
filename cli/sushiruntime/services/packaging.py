# packaging.py
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026-present Mustafa Garip & Sushi Systems
# Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.
# Commercial use requires a licence from Sushi Systems.
"""Turns a built tree into a redistributable archive.

See cli/README.md.
"""

from __future__ import annotations

from pathlib import Path

from sushicore.provision.package_install import capture_command, install_summary

from .. import console
from ..config import Config, find_project_root, load_config
from ..env import load_build_env
from .project import _DRIVER, _RUNNER, K_DEFAULT_TYPE, BuildType, build_tree
from .stage_tree import StageRules, StageTree, StageTreeError

#: Runs the install with its output held back; replaced in tests.
_capture = capture_command


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


def package(prefix: str | None = None, archive: bool = True, force: bool = False,
            build_dir: str | None = None, build_type: BuildType = K_DEFAULT_TYPE) -> int:
    """Install the built tree to a prefix and archive it.

    :param prefix: Where to install. Defaults to build/dist under the project root.
    :param archive: Whether to run CPack over the configured generators.
    :param force: Whether a prefix holding files this command did not stage is cleared.
    :param build_dir: The built tree to install from, below the project root. Defaults to the
        tree of *build_type*.
    :param build_type: The build type whose tree is installed when no *build_dir* is given.
    :return: 0 when every requested step succeeded, non-zero on the first failure.
    """
    console.header("Project Package")
    root = find_project_root()
    cfg = load_config()
    built = build_tree(root, build_type, build_dir)

    if not (built / "CMakeCache.txt").is_file():
        console.error(f"No configured build tree at {built}. Run `sr build` first.")
        return 1

    default_stage = root / "build" / "dist"
    stage = Path(cfg.expand(prefix)).resolve() if prefix else default_stage
    tree = StageTree(stage)
    rules = StageRules(project_root=root, home=Path.home(), build_dir=built,
                       default_stage=default_stage)
    try:
        tree.check(rules, force=force)
        env = load_build_env(cfg, built)
        if stage.is_dir():
            console.info(f"Clearing previous stage tree at {stage}")
        tree.reset()
    except StageTreeError as error:
        console.error(str(error))
        return 1

    install = [_DRIVER.cmake(cfg), "--install", str(built), "--prefix", str(stage)]
    console.command(" ".join(install))
    result = _capture(install, root, env)
    if result.code != 0:
        console.error(f"cmake --install exited {result.code}:\n{result.output.rstrip()}")
        return result.code
    console.info(install_summary(result.output, stage))

    if archive:
        cpack_config = built / "CPackConfig.cmake"
        if cpack_config.is_file():
            console.info("Building the archive...")
            rc = _RUNNER.run([_cpack(cfg), "--config", str(cpack_config)], built, env)
            if rc != 0:
                console.error("CPack failed.")
                return rc
        else:
            console.warn(f"No {cpack_config.name} in the build tree; skipping the archive.")

    console.success(f"Package ready at {stage}")
    console.info("Run `sr test --label package` to check it actually works.")
    return 0
