"""Package-manager abstractions.

Each concrete manager wraps one underlying tool (apt, winget, vcpkg). Steps
depend only on :class:`IPackageManager`, so adding a platform/tool means adding a
class here — no step changes (Open-Closed + Dependency Inversion).

A manager advertises which *kind* of dependency it handles via
:attr:`handles`, matched against the dependency manager-hint so the install step
can route ``windows_vcpkg`` ports to vcpkg and toolchain packages to winget.
"""

from __future__ import annotations

import shutil
import subprocess
from abc import ABC, abstractmethod
from pathlib import Path

from .. import console
from ..config import Config


def _run(cmd: list[str], dry_run: bool, *, check: bool = False) -> int:
    console.command(subprocess.list2cmdline(cmd))
    if dry_run:
        console.info("(dry-run) not executed")
        return 0
    result = subprocess.run(cmd)
    if check and result.returncode != 0:
        console.error(f"Command failed ({result.returncode}).")
    return result.returncode


class IPackageManager(ABC):
    """Installs packages of one kind and reports availability."""

    #: Stable identifier ('apt' | 'winget' | 'vcpkg'); also the manifest channel.
    name: str = "pkg"

    @abstractmethod
    def available(self) -> bool:
        """Is the underlying tool present on this machine?"""
        raise NotImplementedError

    @abstractmethod
    def is_installed(self, pkg: str) -> bool:
        """Best-effort check whether ``pkg`` is already installed."""
        raise NotImplementedError

    @abstractmethod
    def install(self, pkgs: list[str], dry_run: bool) -> bool:
        """Install the given packages. Return True on success."""
        raise NotImplementedError


class AptManager(IPackageManager):
    """Debian/Ubuntu apt. Handles all Linux dependency packages."""

    name = "apt"

    def available(self) -> bool:
        return shutil.which("apt-get") is not None

    def is_installed(self, pkg: str) -> bool:
        # dpkg-query exits 0 and prints "install ok installed" when present.
        try:
            result = subprocess.run(
                ["dpkg-query", "-W", "-f=${Status}", pkg],
                capture_output=True, text=True,
            )
        except OSError:
            return False  # dpkg-query absent => treat as not installed.
        return result.returncode == 0 and "install ok installed" in result.stdout

    def install(self, pkgs: list[str], dry_run: bool) -> bool:
        if not pkgs:
            return True
        # update once, then a single install for all packages.
        sudo = [] if _is_root() else ["sudo"]
        if _run([*sudo, "apt-get", "update"], dry_run) != 0 and not dry_run:
            console.warn("apt-get update failed; continuing to install anyway.")
        rc = _run([*sudo, "apt-get", "install", "-y", *pkgs], dry_run, check=True)
        return rc == 0


class WingetManager(IPackageManager):
    """Windows winget. Used for system toolchain (cmake, ninja, git, ...)."""

    name = "winget"

    def available(self) -> bool:
        return shutil.which("winget") is not None

    def is_installed(self, pkg: str) -> bool:
        try:
            result = subprocess.run(
                ["winget", "list", "--id", pkg, "-e"],
                capture_output=True, text=True,
            )
        except OSError:
            return False  # winget absent => treat as not installed.
        return result.returncode == 0 and pkg.lower() in result.stdout.lower()

    def install(self, pkgs: list[str], dry_run: bool) -> bool:
        ok = True
        for pkg in pkgs:
            rc = _run(
                ["winget", "install", "--id", pkg, "-e",
                 "--accept-package-agreements", "--accept-source-agreements"],
                dry_run, check=True,
            )
            ok = ok and rc == 0
        return ok


class VcpkgManager(IPackageManager):
    """Windows vcpkg. Used for C++ library ports (hwloc, gtest, pkgconf)."""

    name = "vcpkg"

    def __init__(self, cfg: Config) -> None:
        self._cfg = cfg
        self._triplet = cfg.vcpkg_triplet or "x64-windows"

    def _root(self) -> Path:
        root = self._cfg.expand(self._cfg.vcpkg_root) if self._cfg.vcpkg_root else ""
        return Path(root) if root else Path.home() / "vcpkg"

    def _exe(self) -> Path:
        return self._root() / "vcpkg.exe"

    def available(self) -> bool:
        return self._exe().is_file() or shutil.which("vcpkg") is not None

    def ensure_bootstrapped(self, dry_run: bool) -> bool:
        """Clone + bootstrap vcpkg into the configured root if absent."""
        root = self._root()
        if self._exe().is_file():
            return True
        console.info(f"vcpkg not found; bootstrapping into {root} ...")
        if not (root / ".git").is_dir():
            rc = _run(["git", "clone", "https://github.com/microsoft/vcpkg.git",
                       str(root)], dry_run, check=True)
            if rc != 0 and not dry_run:
                return False
        bootstrap = root / "bootstrap-vcpkg.bat"
        rc = _run(["cmd", "/c", str(bootstrap), "-disableMetrics"], dry_run, check=True)
        return rc == 0 or dry_run

    def is_installed(self, pkg: str) -> bool:
        # On a bare machine vcpkg isn't bootstrapped yet; don't shell out to a
        # non-existent exe (FileNotFoundError) — report "not installed" so the
        # install step collects the port and bootstraps before installing.
        if not self.available():
            return False
        exe = self._exe() if self._exe().is_file() else Path("vcpkg")
        try:
            result = subprocess.run(
                [str(exe), "list", "--triplet", self._triplet],
                capture_output=True, text=True,
            )
        except OSError:
            return False
        return result.returncode == 0 and pkg.lower() in result.stdout.lower()

    def install(self, pkgs: list[str], dry_run: bool) -> bool:
        if not pkgs:
            return True
        if not self.ensure_bootstrapped(dry_run):
            console.error("vcpkg bootstrap failed.")
            return False
        exe = self._exe()
        ports = [f"{p}:{self._triplet}" for p in pkgs]
        rc = _run([str(exe), "install", *ports], dry_run, check=True)
        return rc == 0


def _is_root() -> bool:
    try:
        import os
        return os.geteuid() == 0  # type: ignore[attr-defined]
    except AttributeError:
        return False
