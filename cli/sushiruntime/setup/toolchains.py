"""SYCL toolchain installers.

These mirror the Dockerfile's toolchain provisioning for a host install:

* ``install_intel_llvm`` downloads a pre-built intel/llvm nightly SYCL bundle
  (clang++ -fsycl) — the lean, vendor-neutral primary toolchain (Dockerfile
  lines 36-45).
* ``install_adaptivecpp`` builds AdaptiveCpp (acpp) from source, the secondary
  fully open-source toolchain (Dockerfile lines 52-76).

Intel oneAPI (icx/icpx) is handled separately in ``steps.py`` because it is
opt-in (``--oneapi``) and heavy.

Each installer returns the resolved path the CLI config needs (the bundle root
for intel-llvm, the ``acpp`` executable for adaptivecpp) or ``None`` on failure,
so the caller can record it in ``config.local.toml`` and degrade gracefully —
the intel-llvm path is enough to build, so an acpp build failure is non-fatal.
"""

from __future__ import annotations

import os
import shutil
import subprocess
import tarfile
import tempfile
from pathlib import Path

from .. import console
from ..config import Config, deps_dir
from .package_managers import (
    IPackageManager,
    _download,
    _gh_latest_asset,
    _run,
)

# AdaptiveCpp release pinned to match the Dockerfile (ADAPTIVECPP_VERSION).
ACPP_VERSION = "v24.10.0"
# clang/llvm major version used to build acpp (Dockerfile ACPP_LLVM).
ACPP_LLVM = "17"


def toolchains_dir() -> Path:
    """Base directory for SYCL toolchains installed by ``sr setup``."""
    return deps_dir() / "toolchains"


# --------------------------------------------------------------------------- #
# intel/llvm nightly bundle
# --------------------------------------------------------------------------- #

def install_intel_llvm(cfg: Config, dry_run: bool) -> str | None:
    """Download + extract the intel/llvm SYCL bundle. Return its root dir.

    The clang++ inside is the intel-llvm toolchain compiler. Already-present
    installs are reused (idempotent), so re-running setup is cheap.
    """
    root = toolchains_dir() / "llvm-sycl"
    clang = root / "bin" / ("clang++.exe" if cfg.is_windows else "clang++")
    if clang.is_file():
        console.info(f"intel/llvm bundle already present: {root}")
        return str(root)

    asset = "sycl_windows.tar.gz" if cfg.is_windows else "sycl_linux.tar.gz"
    if dry_run:
        console.info(f"(dry-run) would download intel/llvm '{asset}' to {root}")
        return str(root)

    try:
        url = _gh_latest_asset("intel/llvm", asset)
    except Exception as exc:
        console.error(f"Could not resolve intel/llvm release asset: {exc}")
        return None

    with tempfile.TemporaryDirectory() as tmp:
        archive = Path(tmp) / asset
        try:
            console.info("Downloading intel/llvm SYCL bundle (~300-400 MB) ...")
            _download(url, archive)
            _extract_tar_gz(archive, root)
        except Exception as exc:
            console.error(f"intel/llvm bundle install failed: {exc}")
            shutil.rmtree(root, ignore_errors=True)
            return None

    if clang.is_file():
        console.success(f"intel/llvm bundle installed: {root}")
        return str(root)
    console.error(f"intel/llvm bundle extracted but {clang.name} is missing.")
    return None


def _extract_tar_gz(archive: Path, dest: Path) -> None:
    """Extract *archive* into *dest*, collapsing a single top-level wrapper dir.

    The intel/llvm tarballs wrap everything in one directory (the Dockerfile
    strips it with ``--strip-components=1``); some assets do not. Extracting to a
    temp dir first lets us detect either layout and end up with bin/ and lib/ at
    the root of *dest*.
    """
    dest.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory() as staging:
        stage = Path(staging)
        with tarfile.open(archive, "r:gz") as tf:
            try:
                tf.extractall(stage, filter="data")  # py3.12+: path-traversal safe
            except TypeError:
                tf.extractall(stage)  # older Python: no filter kwarg
        entries = list(stage.iterdir())
        srcroot = entries[0] if len(entries) == 1 and entries[0].is_dir() else stage
        for item in srcroot.iterdir():
            target = dest / item.name
            if target.exists():
                shutil.rmtree(target) if target.is_dir() else target.unlink()
            shutil.move(str(item), str(target))


# --------------------------------------------------------------------------- #
# AdaptiveCpp (acpp) from source
# --------------------------------------------------------------------------- #

def install_adaptivecpp(cfg: Config, mgr: IPackageManager | None,
                        vcpkg: IPackageManager | None, dry_run: bool) -> str | None:
    """Build AdaptiveCpp from source. Return the ``acpp`` executable path.

    Best-effort: a failure is reported but left non-fatal by the caller, since
    the intel-llvm toolchain already provides a working SYCL compiler.
    """
    prefix = toolchains_dir() / "adaptivecpp"
    acpp = prefix / "bin" / ("acpp.bat" if cfg.is_windows else "acpp")
    acpp_exe = prefix / "bin" / "acpp"
    for cand in (acpp, acpp_exe):
        if cand.is_file():
            console.info(f"AdaptiveCpp already present: {prefix}")
            return str(cand)

    # cmake/git may be off PATH (e.g. scoop), so honour the configured paths.
    cmake = cfg.expand(cfg.cmake_exe) if cfg.cmake_exe else (shutil.which("cmake") or "")
    git = shutil.which("git") or ""
    if not git or not cmake:
        console.warn("AdaptiveCpp build needs git and cmake; skipping.")
        return None

    if dry_run:
        console.info(f"(dry-run) would build AdaptiveCpp {ACPP_VERSION} -> {prefix}")
        return str(acpp_exe)

    llvm_dir, clang_prefix = (
        _acpp_deps_windows(vcpkg) if cfg.is_windows else _acpp_deps_linux(mgr)
    )
    if llvm_dir is None:
        console.warn("AdaptiveCpp LLVM dependency unavailable; skipping acpp build.")
        return None

    with tempfile.TemporaryDirectory() as tmp:
        src = Path(tmp) / "acpp"
        build = src / "build"
        clone = _run(
            [git, "clone", "--depth", "1", "--branch", ACPP_VERSION,
             "https://github.com/AdaptiveCpp/AdaptiveCpp.git", str(src)],
            dry_run, check=True,
        )
        if clone != 0:
            console.warn("AdaptiveCpp clone failed; skipping.")
            return None

        cfg_cmd = [
            cmake, "-S", str(src), "-B", str(build), "-G", "Ninja",
            "-DCMAKE_BUILD_TYPE=Release",
            f"-DCMAKE_INSTALL_PREFIX={prefix}",
            f"-DLLVM_DIR={llvm_dir}",
            "-DCMAKE_POLICY_VERSION_MINIMUM=3.5",
        ]
        if clang_prefix:
            cfg_cmd.append(f"-DCLANG_INSTALL_PREFIX={clang_prefix}")
        if cfg.ninja_exe:
            cfg_cmd.append(f"-DCMAKE_MAKE_PROGRAM={cfg.expand(cfg.ninja_exe)}")

        if _run(cfg_cmd, dry_run, check=True) != 0:
            console.warn("AdaptiveCpp configure failed; skipping.")
            return None
        if _run([cmake, "--build", str(build), "--target", "install"],
                dry_run, check=True) != 0:
            console.warn("AdaptiveCpp build failed; skipping.")
            return None

    result = next((c for c in (acpp, acpp_exe) if c.is_file()), None)
    if result:
        console.success(f"AdaptiveCpp installed: {prefix}")
        return str(result)
    console.warn("AdaptiveCpp build completed but acpp binary not found.")
    return None


def _acpp_deps_linux(mgr: IPackageManager | None) -> tuple[str | None, str | None]:
    """Install acpp build deps via the distro manager. Return (LLVM_DIR, clang_prefix).

    Mirrors the Dockerfile: clang-17/llvm-17-dev + boost-context/fiber. Only the
    apt path matches the exact package names; other distros use generic clang/llvm
    + boost so we let cmake's find_package locate LLVM (LLVM_DIR left to default).
    """
    if mgr is None:
        return (None, None)
    if mgr.name == "apt":
        pkgs = [
            f"clang-{ACPP_LLVM}", f"llvm-{ACPP_LLVM}-dev",
            f"libclang-{ACPP_LLVM}-dev", f"lld-{ACPP_LLVM}",
            "libboost-context-dev", "libboost-fiber-dev",
        ]
        mgr.install(pkgs, dry_run=False)
        llvm_dir = f"/usr/lib/llvm-{ACPP_LLVM}/lib/cmake/llvm"
        clang_prefix = f"/usr/lib/llvm-{ACPP_LLVM}"
        return (llvm_dir if Path(llvm_dir).is_dir() else None, clang_prefix)
    # Non-apt: install generic clang/llvm-dev/boost and let find_package resolve.
    mgr.install(mgr.translate_apt(
        ["clang", "llvm", "libboost-context-dev", "libboost-fiber-dev"]),
        dry_run=False)
    return ("", None)  # empty LLVM_DIR => cmake searches default locations


def _acpp_deps_windows(vcpkg: IPackageManager | None) -> tuple[str | None, str | None]:
    """Install acpp build deps on Windows. Return (LLVM_DIR, clang_prefix).

    LLVM comes from winget (LLVM.LLVM); boost-context/fiber from vcpkg. This is
    the least-tested path — acpp's Windows support is young — so it is wrapped in
    best-effort error handling by the caller.
    """
    if vcpkg is not None:
        vcpkg.install(["boost-context", "boost-fiber"], dry_run=False)
    if not shutil.which("clang"):
        if shutil.which("winget"):
            subprocess.run(
                ["winget", "install", "--id", "LLVM.LLVM", "-e",
                 "--accept-package-agreements", "--accept-source-agreements",
                 "--silent"],
                check=False,
            )
    for base in (r"C:/Program Files/LLVM", r"C:/Program Files (x86)/LLVM"):
        cm = Path(base) / "lib" / "cmake" / "llvm"
        if cm.is_dir():
            return (str(cm), base)
    return (None, None)
