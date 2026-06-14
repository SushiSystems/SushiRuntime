"""Build-environment snapshotting.

A parent process cannot ``source setvars.sh`` / ``call vcvars64.bat`` and inherit
the result. Instead we run those scripts in a child shell, dump the resulting
environment, and reuse that dictionary for every cmake/ctest subprocess we spawn.

Sourcing oneAPI's setvars is slow, so the snapshot is cached on disk keyed by the
configuration that produced it; an unchanged config skips the shell entirely.
"""

from __future__ import annotations

import hashlib
import json
import os
import subprocess
from pathlib import Path

from . import console
from .config import Config

CACHE_NAME = ".sushi_env.json"


def _cache_key(cfg: Config) -> str:
    material = json.dumps(
        {
            "platform": cfg.platform,
            "vs_vcvars": cfg.vs_vcvars,
            "oneapi_root": cfg.oneapi_root,
        },
        sort_keys=True,
    )
    return hashlib.sha256(material.encode()).hexdigest()


def _parse_windows_set(output: str) -> dict[str, str]:
    env: dict[str, str] = {}
    for line in output.splitlines():
        if "=" in line:
            key, _, value = line.partition("=")
            if key:
                env[key] = value
    return env


def _snapshot_windows(cfg: Config) -> dict[str, str] | None:
    parts = []
    vcvars = cfg.expand(cfg.vs_vcvars)
    if vcvars and Path(vcvars).is_file():
        console.info("Loading Visual Studio environment (vcvars64)...")
        parts.append(f'call "{vcvars}"')
    oneapi = cfg.expand(cfg.oneapi_root)
    setvars = Path(oneapi) / "setvars.bat" if oneapi else None
    if setvars and setvars.is_file():
        console.info("Loading oneAPI environment (setvars)...")
        parts.append(f'call "{setvars}" intel64 >nul 2>&1')
    if not parts:
        return None
    parts.append("set")
    script = " && ".join(parts)
    result = subprocess.run(
        ["cmd", "/c", script], capture_output=True, text=True
    )
    if result.returncode != 0:
        console.warn("Environment script returned non-zero; using current env.")
        return None
    return _parse_windows_set(result.stdout)


def _snapshot_linux(cfg: Config) -> dict[str, str] | None:
    oneapi = cfg.expand(cfg.oneapi_root)
    setvars = Path(oneapi) / "setvars.sh" if oneapi else None
    if not (setvars and setvars.is_file()):
        return None
    console.info("Loading oneAPI environment (setvars)...")
    # env -0 emits NUL-separated KEY=VALUE pairs so multi-line values survive.
    script = f'source "{setvars}" --force >/dev/null 2>&1; env -0'
    result = subprocess.run(
        ["bash", "-c", script], capture_output=True, text=True
    )
    if result.returncode != 0:
        console.warn("setvars.sh returned non-zero; using current env.")
        return None
    env: dict[str, str] = {}
    for chunk in result.stdout.split("\0"):
        if "=" in chunk:
            key, _, value = chunk.partition("=")
            env[key] = value
    return env


def load_build_env(cfg: Config, build_dir: Path) -> dict[str, str]:
    """Return the environment for build/test/run subprocesses.

    Falls back to the current ``os.environ`` when no toolchain scripts are
    configured (e.g. inside the intel/llvm Docker image where the compiler is
    already on PATH).
    """
    build_dir.mkdir(parents=True, exist_ok=True)
    cache_file = build_dir / CACHE_NAME
    key = _cache_key(cfg)

    if cache_file.is_file():
        try:
            cached = json.loads(cache_file.read_text())
            if cached.get("key") == key:
                merged = dict(os.environ)
                merged.update(cached["env"])
                return merged
        except (json.JSONDecodeError, OSError, KeyError):
            pass  # fall through and rebuild

    snapshot = (
        _snapshot_windows(cfg) if cfg.is_windows else _snapshot_linux(cfg)
    )
    if not snapshot:
        return dict(os.environ)

    try:
        cache_file.write_text(json.dumps({"key": key, "env": snapshot}))
    except OSError:
        pass

    merged = dict(os.environ)
    merged.update(snapshot)
    return merged
