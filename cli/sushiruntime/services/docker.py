# docker.py
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026-present Mustafa Garip & Sushi Systems
# Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.
# Commercial use requires a licence from Sushi Systems.
"""Docker environment management (replaces docker/build.* and docker/run.*)."""

from __future__ import annotations

from sushicore.proc import Runner

from .. import console
from ..config import PROFILE, find_project_root

IMAGE = "sushiruntime"
DOCKERFILE = ".config/docker/Dockerfile"

_RUNNER = Runner(console, PROFILE.program)


def build(no_cache: bool = False, oneapi: bool = False) -> int:
    console.header("Docker Build")
    root = find_project_root()
    cmd = ["docker", "build", "-f", DOCKERFILE, "-t", IMAGE]
    if no_cache:
        console.info("Building without the layer cache (--no-cache).")
        cmd.append("--no-cache")
    if oneapi:
        console.info("Including the Intel oneAPI DPC++ compiler (--build-arg WITH_ONEAPI=1).")
        cmd += ["--build-arg", "WITH_ONEAPI=1"]
    cmd.append(".")
    return _RUNNER.run(cmd, root)


def run(admin: bool = False, no_gpu: bool = False) -> int:
    console.header("Docker Run")
    root = find_project_root()
    cmd = ["docker", "run", "-it", "--rm"]
    if no_gpu:
        console.info("Running without GPU passthrough (--no-gpu).")
    else:
        cmd += ["--gpus", "all"]
    if admin:
        console.info("Running in ADMIN mode (privileged)")
        cmd += ["--privileged", "--cap-add=SYS_ADMIN"]
    cmd += ["-v", f"{root}:/workspace/sushiruntime", IMAGE, "/bin/bash"]
    return _RUNNER.run(cmd, root)
