"""Docker environment management (replaces docker/build.* and docker/run.*)."""

from __future__ import annotations

import subprocess

from .. import console
from ..config import find_project_root

IMAGE = "sushiruntime"


def _run(cmd: list[str], cwd) -> int:
    console.command(" ".join(cmd))
    return subprocess.run(cmd, cwd=str(cwd)).returncode


def build(no_cache: bool = False) -> int:
    console.header("Docker Build")
    root = find_project_root()
    cmd = ["docker", "build", "-t", IMAGE]
    if no_cache:
        console.info("Building without the layer cache (--no-cache).")
        cmd.append("--no-cache")
    cmd.append(".")
    return _run(cmd, cwd=root)


def run(admin: bool = False) -> int:
    console.header("Docker Run")
    root = find_project_root()
    cmd = ["docker", "run", "-it", "--rm", "--gpus", "all"]
    if admin:
        console.info("Running in ADMIN mode (privileged)")
        cmd += ["--privileged", "--cap-add=SYS_ADMIN"]
    cmd += ["-v", f"{root}:/workspace/sushiruntime", IMAGE, "/bin/bash"]
    return _run(cmd, cwd=root)
