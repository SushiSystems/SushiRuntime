# docker.py
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026-present Mustafa Garip & Sushi Systems
# Licensed under the Apache License 2.0. See LICENSE.
"""Docker environment management (replaces docker/build.* and docker/run.*)."""

from __future__ import annotations

from .surroundings import Surroundings

IMAGE = "sushiruntime"
DOCKERFILE = ".config/docker/Dockerfile"


def build(no_cache: bool = False, oneapi: bool = False,
          surroundings: Surroundings | None = None) -> int:
    ws = surroundings or Surroundings()
    console = ws.console
    console.header("Docker Build")
    root = ws.root
    cmd = ["docker", "build", "-f", DOCKERFILE, "-t", IMAGE]
    if no_cache:
        console.info("Building without the layer cache (--no-cache).")
        cmd.append("--no-cache")
    if oneapi:
        console.info("Including the Intel oneAPI DPC++ compiler (--build-arg WITH_ONEAPI=1).")
        cmd += ["--build-arg", "WITH_ONEAPI=1"]
    cmd.append(".")
    return ws.runner.run(cmd, root)


def run(admin: bool = False, no_gpu: bool = False,
        surroundings: Surroundings | None = None) -> int:
    ws = surroundings or Surroundings()
    console = ws.console
    console.header("Docker Run")
    root = ws.root
    cmd = ["docker", "run", "-it", "--rm"]
    if no_gpu:
        console.info("Running without GPU passthrough (--no-gpu).")
    else:
        cmd += ["--gpus", "all"]
    if admin:
        console.info("Running in ADMIN mode (privileged)")
        cmd += ["--privileged", "--cap-add=SYS_ADMIN"]
    cmd += ["-v", f"{root}:/workspace/sushiruntime", IMAGE, "/bin/bash"]
    return ws.runner.run(cmd, root)
