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
from .config import Config, find_project_root

CACHE_NAME = ".sushiruntime_env.json"


def _cache_key(cfg: Config) -> str:
    material = json.dumps(
        {
            "platform": cfg.platform,
            "vs_vcvars": cfg.vs_vcvars,
            "oneapi_root": cfg.oneapi_root,
            # The toolchain gates whether oneAPI setvars is sourced, so it must
            # key the cache: switching intel-llvm<->oneapi has to rebuild the snapshot.
            "toolchain": cfg.toolchain,
            # The intel-llvm/adaptivecpp toolchains inject their own bin dirs into
            # the snapshot PATH, so a change to either invalidates the cache. The
            # resolved root, not the raw setting: when the setting is empty the
            # bundle is probed for, and swapping which one is present has to
            # invalidate the snapshot just as re-pinning it would.
            "llvm_root": cfg.resolved_llvm_root(),
            "acpp_exe": cfg.acpp_exe,
        },
        sort_keys=True,
    )
    return hashlib.sha256(material.encode()).hexdigest()


def _merge_env(base: dict[str, str], overlay: dict[str, str]) -> dict[str, str]:
    """Merge overlay into base. PATH is prepended rather than replaced so that
    toolchain directories (VS, oneAPI) take priority while scoop/user tools added
    by the shell profile remain reachable.

    PATH is collapsed into the *single* case variant the base already uses (e.g.
    Windows' "Path"). Writing the overlay's "PATH" as a new key while base keeps
    "Path" leaves two conflicting entries; Windows then honours only one for child
    processes, so toolchain dirs added to the other silently vanish from DLL/exe
    resolution.
    """
    merged = dict(base)
    path_key = next((k for k in merged if k.upper() == "PATH"), None)
    for k, v in overlay.items():
        if k.upper() == "PATH":
            target = path_key or k
            existing = merged.get(target, "")
            merged[target] = v + os.pathsep + existing if existing else v
            path_key = target
        else:
            merged[k] = v
    return merged


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
    # oneAPI setvars only for the oneapi toolchain (see _snapshot_linux): mixing
    # it into an llvm/adaptivecpp build pollutes the loader search path. vcvars
    # above stays unconditional — MSVC's env is needed for every Windows build.
    if cfg.toolchain == "oneapi":
        oneapi = cfg.expand(cfg.oneapi_root)
        setvars = Path(oneapi) / "setvars.bat" if oneapi else None
        if setvars and setvars.is_file():
            console.info("Loading oneAPI environment (setvars)...")
            parts.append(f'call "{setvars}" intel64 >nul 2>&1')
    if not parts:
        return None
    parts.append("set")
    script = " && ".join(parts)
    # Pass the whole command as a single string, NOT as ["cmd", "/c", script].
    # With a list, subprocess re-quotes each element and mangles the inner
    # double-quotes around the (space-containing) vcvars path, so cmd.exe sees
    # the quoted path as one unknown token ("...vcvars64.bat" is not recognized)
    # and returns non-zero — silently dropping the entire VS environment.
    result = subprocess.run(
        "cmd /c " + script, capture_output=True, text=True
    )
    if result.returncode != 0:
        console.warn("Environment script returned non-zero; using current env.")
        return None
    return _parse_windows_set(result.stdout)


def _prepend_path(env: dict[str, str], var: str, dirs: list[str]) -> None:
    """Prepend *dirs* to the (case-insensitive) *var* path entry of *env*."""
    if not dirs:
        return
    key = next((k for k in env if k.upper() == var.upper()), var)
    existing = env.get(key, "")
    env[key] = os.pathsep.join(dirs + ([existing] if existing else []))


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
                # compiler-rt ships AddressSanitizer's runtime as a DLL beside its
                # import library, so an instrumented binary cannot even start
                # unless that directory is on PATH. Added for every intel-llvm
                # environment: the directory holds nothing but compiler-rt, so it
                # is inert for a non-sanitized build, and the alternative — adding
                # it only for `--asan` — would leave `sr run` on an ASan binary
                # failing with a bare "DLL not found".
                bins.extend(str(p) for p in
                            sorted((Path(root) / "lib" / "clang").glob("*/lib/windows")))
    elif cfg.toolchain == "adaptivecpp":
        acpp = cfg.expand(cfg.acpp_exe)
        if acpp and Path(acpp).is_file():
            acpp_root = Path(acpp).parent.parent
            bins.append(str(Path(acpp).parent))
            libs.append(str(acpp_root / "lib"))
    # vcpkg's applocal deployment copies a target's DIRECT dependents next to it,
    # so a library pulled in transitively (hwloc, reached through sushiruntime.dll
    # rather than by the test executable) can be missing from a fresh build tree
    # and the binary then fails to start with a bare "DLL not found". Putting the
    # port bin dir on PATH makes any freshly built binary runnable regardless of
    # what got copied where; it does not replace deployment for a shipped build.
    if cfg.is_windows and cfg.use_vcpkg and cfg.vcpkg_root:
        vcpkg_bin = (Path(cfg.expand(cfg.vcpkg_root)) / "installed"
                     / cfg.vcpkg_triplet / "bin")
        if vcpkg_bin.is_dir():
            bins.append(str(vcpkg_bin))

    bins = [d for d in bins if Path(d).is_dir()]
    _prepend_path(env, "PATH", bins)
    if not cfg.is_windows:
        libs = [d for d in libs if Path(d).is_dir()]
        _prepend_path(env, "LD_LIBRARY_PATH", libs)


def _snapshot_linux(cfg: Config) -> dict[str, str] | None:
    # Source oneAPI's setvars ONLY for the oneapi toolchain. Sourcing it for the
    # intel-llvm/adaptivecpp toolchains prepends oneAPI's lib dir to LD_LIBRARY_PATH,
    # so at runtime the binary loads oneAPI's libsycl/libur_loader while the CUDA
    # backend still resolves the llvm-sycl libur_adapter_cuda — a Unified Runtime
    # version mismatch that segfaults during SYCL device discovery
    # (urVirtualMemMap). The intel-llvm/adaptivecpp toolchains ship on PATH and
    # need no setvars at all, so returning None keeps their environment clean.
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

    if cache_file.is_file():
        try:
            cached = json.loads(cache_file.read_text())
            if cached.get("key") == key:
                env = _merge_env(dict(os.environ), cached["env"])
                _inject_toolchain_paths(cfg, env)
                _inject_sanitizer_options(env)
                return env
        except (json.JSONDecodeError, OSError, KeyError):
            pass  # fall through and rebuild

    snapshot = (
        _snapshot_windows(cfg) if cfg.is_windows else _snapshot_linux(cfg)
    )
    if not snapshot:
        env = dict(os.environ)
        _inject_toolchain_paths(cfg, env)
        _inject_sanitizer_options(env)
        return env

    try:
        cache_file.write_text(json.dumps({"key": key, "env": snapshot}))
    except OSError:
        pass

    env = _merge_env(dict(os.environ), snapshot)
    _inject_toolchain_paths(cfg, env)
    _inject_sanitizer_options(env)
    return env
