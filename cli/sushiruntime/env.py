# env.py
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026 Sushi Systems
# Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.
# Commercial use requires a licence from Sushi Systems.
"""Snapshots the build environment that every cmake and ctest subprocess runs under.

See cli/README.md.
"""

from __future__ import annotations

import hashlib
import json
import os
import subprocess
from pathlib import Path

from sushicore.build_env import (
    merge_env,
    prepend_path,
    read_cache,
    snapshot_windows,
    write_cache,
)

from . import console
from .config import Config, find_project_root

CACHE_NAME = ".sushiruntime_env.json"


def _cache_key(cfg: Config) -> str:
    material = json.dumps(
        {
            "platform": cfg.platform,
            "vs_vcvars": cfg.vs_vcvars,
            "oneapi_root": cfg.oneapi_root,
            # The toolchain decides whether setvars is sourced, so it keys the cache.
            "toolchain": cfg.toolchain,
            # The resolved root keys the cache, not the raw setting; see cli/README.md.
            "llvm_root": cfg.resolved_llvm_root(),
            "acpp_exe": cfg.acpp_exe,
        },
        sort_keys=True,
    )
    return hashlib.sha256(material.encode()).hexdigest()


def _inject_toolchain_paths(cfg: Config, env: dict[str, str]) -> None:
    """Add the non-oneAPI toolchain bin/lib dirs to a build/run environment.

    Neither platform ships a system SYCL compiler for the intel-llvm bundle
    (clang++) or AdaptiveCpp (acpp): they live in their own install trees, so
    their bin must be on PATH (and, on Linux, their lib on LD_LIBRARY_PATH) for
    the compiler and its SYCL runtime libraries to resolve at build and run time.
    oneAPI needs no help here — its setvars wires this up already.
    """
    if cfg.toolchain == "oneapi":
        return
    bins: list[str] = []
    libs: list[str] = []
    if cfg.toolchain == "intel-llvm":
        root = cfg.resolved_llvm_root()
        if root and Path(root).is_dir():
            bins.append(str(Path(root) / "bin"))
            libs.append(str(Path(root) / "lib"))
            if cfg.is_windows:
                # compiler-rt's ASan runtime directory goes on PATH; see cli/README.md.
                bins.extend(str(p) for p in
                            sorted((Path(root) / "lib" / "clang").glob("*/lib/windows")))
    elif cfg.toolchain == "adaptivecpp":
        acpp = cfg.expand(cfg.acpp_exe)
        if acpp and Path(acpp).is_file():
            acpp_root = Path(acpp).parent.parent
            bins.append(str(Path(acpp).parent))
            libs.append(str(acpp_root / "lib"))
    # vcpkg's port bin directory goes on PATH for transitive DLLs; see cli/README.md.
    if cfg.is_windows and cfg.use_vcpkg and cfg.vcpkg_root:
        vcpkg_bin = (Path(cfg.expand(cfg.vcpkg_root)) / "installed"
                     / cfg.vcpkg_triplet / "bin")
        if vcpkg_bin.is_dir():
            bins.append(str(vcpkg_bin))

    bins = [d for d in bins if Path(d).is_dir()]
    prepend_path(env, "PATH", bins)
    if not cfg.is_windows:
        libs = [d for d in libs if Path(d).is_dir()]
        prepend_path(env, "LD_LIBRARY_PATH", libs)


def _snapshot_linux(cfg: Config) -> dict[str, str] | None:
    # setvars is sourced only for the oneapi toolchain; see cli/README.md.
    if cfg.toolchain != "oneapi":
        return None
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


def _inject_sanitizer_options(env: dict[str, str]) -> None:
    """Point LeakSanitizer at the repo's suppression file, if it has one.

    LeakSanitizer runs as part of AddressSanitizer on Linux and reports at process
    exit, so a third-party library that leaks its one-time initialisation makes
    every ASan test process exit non-zero — the tests themselves pass and then the
    binary fails, which reads as a broken suite rather than a known upstream leak.
    Pointing LSan at the checked-in suppression list keeps real leaks failing while
    the known ones stay quiet. Set unconditionally: a non-ASan build never reads
    the variable, and LeakSanitizer does not exist on Windows at all.
    """
    if "LSAN_OPTIONS" in env:
        return  # a caller who set it explicitly outranks us
    try:
        suppressions = find_project_root() / "tests" / "lsan_suppressions.txt"
    except SystemExit:
        return  # not inside a checkout; nothing to point at
    if suppressions.is_file():
        env["LSAN_OPTIONS"] = f"suppressions={suppressions.as_posix()}"


def load_build_env(cfg: Config, build_dir: Path) -> dict[str, str]:
    """Return the environment for build/test/run subprocesses.

    Falls back to the current ``os.environ`` when no toolchain scripts are
    configured (e.g. inside the intel/llvm Docker image where the compiler is
    already on PATH).
    """
    build_dir.mkdir(parents=True, exist_ok=True)
    cache_file = build_dir / CACHE_NAME
    key = _cache_key(cfg)

    def finish(env: dict[str, str]) -> dict[str, str]:
        """The two steps that are this module's own, applied to every path out."""
        _inject_toolchain_paths(cfg, env)
        _inject_sanitizer_options(env)
        return env

    cached = read_cache(cache_file, key)
    if cached is not None:
        return finish(merge_env(os.environ, cached))

    snapshot = (
        snapshot_windows(cfg, console) if cfg.is_windows else _snapshot_linux(cfg)
    )
    if not snapshot:
        return finish(dict(os.environ))

    write_cache(cache_file, key, snapshot)
    return finish(merge_env(os.environ, snapshot))
