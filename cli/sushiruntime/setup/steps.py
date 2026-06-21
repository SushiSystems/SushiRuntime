"""Concrete pipeline steps.

Each step has a single responsibility and depends only on abstractions:
``DetectStep`` inventories the machine, ``InstallDepsStep`` installs missing
manifest packages through injected package managers, ``ConfigureStep`` writes
``config.local.toml`` from probed tool paths, ``VerifyStep`` builds and
smoke-tests through the project service, and ``UninstallStep`` tears down
everything the installer placed on the system.
"""

from __future__ import annotations

import shutil
import subprocess
from pathlib import Path

from rich.table import Table

from .. import console
from ..config import config_dir
from . import probe
from .probe import binary_works
from .dependency_source import Dependency, IDependencySource
from .package_managers import (
    IPackageManager,
    WINGET_ID_TO_CMD,
    _tools_dir,
)
from .pipeline import InstallContext, Step, StepResult
from . import toolchains

# System toolchain installed through the toolchain manager (not via the manifest
# since these tools must exist before vcpkg/pip can run).
_LINUX_TOOLCHAIN_APT = ["build-essential", "cmake", "ninja-build", "git"]
_WINDOWS_TOOLCHAIN_PKGS = [
    "Kitware.CMake", "Ninja-build.Ninja", "Git.Git",
]


def _dep_installed(dep: Dependency, mgr: IPackageManager, platform: str) -> bool:
    if dep.check_cmd:
        try:
            if subprocess.run(dep.check_cmd, capture_output=True).returncode == 0:
                return True
        except (OSError, FileNotFoundError):
            pass
    pkgs = dep.packages_for(platform)
    if not pkgs:
        return False
    return all(mgr.is_installed(p) for p in pkgs)


def _first_available(managers: list[IPackageManager]) -> IPackageManager | None:
    for mgr in managers:
        if mgr.available():
            return mgr
    return None


class DetectStep(Step):
    """Inventory tools and dependencies; fill ``ctx.detected``."""

    name = "detect"

    def __init__(self, source: IDependencySource) -> None:
        self._source = source

    def run(self, ctx: InstallContext) -> StepResult:
        plat = ctx.cfg.platform
        table = Table(show_header=True, header_style="bold magenta",
                      title="Environment inventory")
        table.add_column("Component")
        table.add_column("Status")
        table.add_column("Detail", style="dim")

        for tool in ("python3" if plat != "windows" else "python",
                     "git", "cmake", "ninja", "pkg-config"):
            found = shutil.which(tool) is not None
            ctx.detected[tool] = found
            table.add_row(tool, _mark(found), shutil.which(tool) or "")

        compiler, where = probe.find_sycl_compiler(ctx.cfg)
        ctx.detected["sycl_compiler"] = compiler is not None
        table.add_row("SYCL compiler", _mark(compiler is not None),
                      f"{compiler or '-'} {where or ''}".strip())

        for dep in self._source.selected(plat, ctx.gpu):
            present = False
            if dep.check_cmd:
                try:
                    present = subprocess.run(
                        dep.check_cmd, capture_output=True).returncode == 0
                except (OSError, FileNotFoundError):
                    present = False
            ctx.detected[dep.name] = present
            pkgs = ", ".join(dep.packages_for(plat))
            table.add_row(dep.name, _mark(present) if dep.check_cmd else "[dim]?[/dim]",
                          f"{dep.description} ({pkgs})")

        gpu_present = shutil.which("nvidia-smi") is not None
        ctx.detected["nvidia_gpu"] = gpu_present
        table.add_row("NVIDIA GPU", _mark(gpu_present),
                      "use --gpu to install CUDA" if gpu_present else "")

        console.console.print(table)
        return StepResult.OK


class InstallDepsStep(Step):
    """Install missing manifest dependencies and system toolchain."""

    name = "install-deps"

    def __init__(self, source: IDependencySource,
                 managers: list[IPackageManager]) -> None:
        self._source = source
        self._managers = managers

    def _manager(self, name: str) -> IPackageManager | None:
        for m in self._managers:
            if m.name == name:
                return m
        return None

    def run(self, ctx: InstallContext) -> StepResult:
        if ctx.cfg.platform == "windows":
            return self._run_windows(ctx)
        return self._run_linux(ctx)

    # -- SYCL toolchains (shared) --------------------------------------------- #

    def _install_toolchains(self, ctx: InstallContext,
                            mgr: IPackageManager | None,
                            vcpkg: IPackageManager | None) -> None:
        """Install the default SYCL toolchains (intel-llvm + AdaptiveCpp).

        Both are recorded on ``ctx.resolved_paths`` so ConfigureStep can write
        their paths. Failures are non-fatal: the intel-llvm bundle alone is enough
        to build, and AdaptiveCpp's from-source build is best-effort.
        """
        llvm = toolchains.install_intel_llvm(ctx.cfg, ctx.dry_run)
        if llvm:
            ctx.resolved_paths["llvm_root"] = llvm
        else:
            console.warn("intel/llvm bundle not installed; the default "
                         "intel-llvm toolchain will be unavailable.")
        acpp = toolchains.install_adaptivecpp(ctx.cfg, mgr, vcpkg, ctx.dry_run)
        if acpp:
            ctx.resolved_paths["acpp_exe"] = acpp

    # -- Linux ---------------------------------------------------------------- #

    def _run_linux(self, ctx: InstallContext) -> StepResult:
        linux_managers = ["apt", "dnf", "yum", "pacman", "zypper"]
        mgr = next(
            (self._manager(n) for n in linux_managers
             if self._manager(n) and self._manager(n).available()),  # type: ignore[union-attr]
            None,
        )
        if mgr is None:
            msg = ("No supported package manager found (apt, dnf, yum, pacman, zypper). "
                   "Install python3, pip, git, cmake, and ninja manually, then re-run.")
            if ctx.dry_run:
                console.warn(f"(dry-run) {msg}")
                return StepResult.SKIPPED
            console.error(msg)
            return StepResult.FAILED

        console.info(f"Using package manager: {mgr.name}")

        # Translate the generic apt toolchain list to native package names.
        pkgs: list[str] = list(mgr.translate_apt(_LINUX_TOOLCHAIN_APT))
        for dep in self._source.selected("linux", ctx.gpu):
            if _dep_installed(dep, mgr, "linux"):
                console.info(f"{dep.name}: already installed, skipping.")
                continue
            pkgs.extend(mgr.translate_apt(dep.linux_apt))

        pkgs = _dedup(pkgs)
        if not pkgs:
            console.info("All packages already present.")
            return StepResult.SKIPPED

        console.info(f"Installing via {mgr.name}: {', '.join(pkgs)}")
        ok = mgr.install(pkgs, ctx.dry_run)
        ctx.installed.extend(pkgs)

        self._install_toolchains(ctx, mgr=mgr, vcpkg=None)

        # Intel oneAPI DPC++ — opt-in (--oneapi). Mirrors the Dockerfile's
        # WITH_ONEAPI apt route; needs the Intel oneAPI apt repo configured.
        if ctx.oneapi and mgr.name == "apt":
            console.info("oneAPI: installing intel-oneapi-compiler-dpcpp-cpp "
                         "(requires the Intel oneAPI apt repository).")
            mgr.install(["intel-oneapi-compiler-dpcpp-cpp"], ctx.dry_run)
        elif ctx.oneapi:
            console.warn(f"--oneapi on {mgr.name} is not automated; install the "
                         "Intel oneAPI DPC++ compiler manually.")
        return StepResult.OK if ok else StepResult.FAILED

    # -- Windows -------------------------------------------------------------- #

    def _run_windows(self, ctx: InstallContext) -> StepResult:
        winget = self._manager("winget")
        direct = self._manager("direct-download")
        vcpkg  = self._manager("vcpkg")

        # Only install toolchain items that are not already on PATH.
        missing = [
            pkg for pkg in _WINDOWS_TOOLCHAIN_PKGS
            if not shutil.which(WINGET_ID_TO_CMD.get(pkg, ""))
        ]

        tool_ok = True
        if missing:
            if winget and winget.available():
                console.info("Installing missing toolchain via winget ...")
                tool_ok = winget.install(missing, ctx.dry_run)
            elif direct:
                console.info("winget unavailable; installing toolchain via direct download ...")
                tool_ok = direct.install(missing, ctx.dry_run)
            else:
                console.error("No install method available for toolchain packages.")
                return StepResult.FAILED
        else:
            console.info("System toolchain already present, skipping.")

        # C++ library ports via vcpkg.
        ports: list[str] = []
        for dep in self._source.selected("windows", ctx.gpu):
            if vcpkg and _dep_installed(dep, vcpkg, "windows"):
                console.info(f"{dep.name}: already installed, skipping.")
                continue
            ports.extend(dep.windows_vcpkg)
        ports = _dedup(ports)

        lib_ok = True
        if ports:
            if vcpkg is None:
                console.error("vcpkg manager missing; cannot install C++ libs.")
                return StepResult.FAILED
            console.info(f"Installing via vcpkg: {', '.join(ports)}")
            lib_ok = vcpkg.install(ports, ctx.dry_run)
            ctx.installed.extend(ports)

        # VS Build Tools — only attempt if winget is present.
        if winget and winget.available():
            if not winget.is_installed("Microsoft.VisualStudio.2022.BuildTools"):
                if ctx.dry_run:
                    console.info("(dry-run) skipping VS Build Tools install.")
                else:
                    vs_cmd = [
                        "winget", "install",
                        "--id", "Microsoft.VisualStudio.2022.BuildTools", "-e",
                        "--accept-package-agreements", "--accept-source-agreements",
                        "--override",
                        "--add Microsoft.VisualStudio.Workload.VCTools "
                        "--includeRecommended --quiet --wait --norestart",
                    ]
                    with console.console.status(
                        "[bold cyan]Installing Visual Studio Build Tools "
                        "(C++ workloads) — this may take 10–20 minutes.",
                        spinner="bouncingBar",
                    ):
                        rc = subprocess.run(vs_cmd).returncode
                    if rc != 0:
                        console.warn("Visual Studio Build Tools install failed or was cancelled.")
                        tool_ok = False
                    else:
                        console.success("Visual Studio Build Tools installed.")

        # Lean SYCL toolchains (intel-llvm bundle + AdaptiveCpp), like the
        # Dockerfile's default. oneAPI is installed below only with --oneapi.
        self._install_toolchains(ctx, mgr=None, vcpkg=vcpkg)

        # Intel oneAPI — opt-in (--oneapi), and only when icx-cl is not present.
        if ctx.oneapi and not ctx.detected.get("sycl_compiler", False):
            if ctx.dry_run:
                console.info("(dry-run) skipping Intel oneAPI install.")
            else:
                oneapi_url = (
                    "https://registrationcenter-download.intel.com/akdlm/IRC_NAS/"
                    "bae85ab1-cfcd-4251-8d42-a0c27949ea33/"
                    "intel-oneapi-toolkit-2026.0.0.193_offline.exe"
                )
                installer = Path.home() / "intel-oneapi-toolkit-offline.exe"
                if not installer.is_file():
                    console.info("Downloading Intel oneAPI Installer (~4 GB) from Intel servers ...")
                    dl_rc = subprocess.run(
                        ["curl", "-L", "-o", str(installer), oneapi_url]
                    ).returncode
                    if dl_rc != 0:
                        console.error("Failed to download Intel oneAPI installer.")
                        tool_ok = False
                if installer.is_file() and tool_ok:
                    oneapi_cmd = [
                        str(installer), "-s", "-a", "--silent", "--eula", "accept",
                        "-p=NEED_VS2022_INTEGRATION=1",
                    ]
                    with console.console.status(
                        "[bold cyan]Installing Intel oneAPI Toolkit silently "
                        "— this may take 10–20 minutes.",
                        spinner="bouncingBar",
                    ):
                        try:
                            rc = subprocess.run(oneapi_cmd).returncode
                        except OSError as exc:
                            if getattr(exc, "winerror", None) == 740:
                                console.info(
                                    "Intel oneAPI requires administrator privileges. "
                                    "A UAC prompt will appear — approve it to continue."
                                )
                                try:
                                    ps_cmd = (
                                        f"$p = Start-Process -FilePath '{str(installer)}'"
                                        f" -ArgumentList '-s','-a','--silent','--eula','accept'"
                                        f",'-p=NEED_VS2022_INTEGRATION=1'"
                                        f" -Verb RunAs -Wait -PassThru; exit $p.ExitCode"
                                    )
                                    rc = subprocess.run(
                                        ["powershell", "-Command", ps_cmd],
                                        timeout=1200,
                                    ).returncode
                                except Exception as exc2:
                                    console.error(f"Elevated oneAPI launch failed: {exc2}")
                                    tool_ok = False
                                    rc = 1
                            else:
                                console.warn(f"Intel oneAPI installer failed to launch: {exc}")
                                tool_ok = False
                                rc = 1
                    if rc != 0 and tool_ok:
                        console.warn("Intel oneAPI Toolkit installation failed.")
                        tool_ok = False
                    elif rc == 0:
                        console.success("Intel oneAPI Toolkit installed.")

        return StepResult.OK if (tool_ok and lib_ok) else StepResult.FAILED


class ConfigureStep(Step):
    """Probe installed tools and write ``config.local.toml``."""

    name = "configure"

    def run(self, ctx: InstallContext) -> StepResult:
        values = probe.resolve_local_config(ctx.cfg, gpu=ctx.gpu)
        ctx.resolved_paths = values
        if not values:
            console.info("No machine-specific paths to write; defaults suffice.")
            return StepResult.SKIPPED

        target = config_dir() / "config.local.toml"
        content = probe.render_local_config(ctx.cfg.platform, values)

        if ctx.dry_run:
            console.info(f"(dry-run) would write {target}:")
            console.console.print(content, markup=False)
            return StepResult.OK

        if target.is_file():
            backup = target.with_suffix(".toml.bak")
            shutil.copyfile(target, backup)
            console.info(f"Backed up existing config to {backup.name}")

        target.write_text(content, encoding="utf-8")
        console.success(f"Wrote {target}")
        return StepResult.OK


class VerifyStep(Step):
    """Build and smoke-test through the existing project service."""

    name = "verify"

    def run(self, ctx: InstallContext) -> StepResult:
        if ctx.dry_run:
            console.info("(dry-run) skipping build/verify.")
            return StepResult.SKIPPED

        from ..services import project as project_svc
        from ..services.project import BuildType, Suite

        no_cuda = not ctx.gpu and ctx.cfg.platform != "windows"
        rc = project_svc.build(BuildType.release, distributed=False,
                               no_cuda=no_cuda, clean=False)
        if rc != 0:
            console.error("Build failed during verification.")
            return StepResult.FAILED

        rc = project_svc.test(Suite.functional, distributed=False,
                              filter=None, asan=False, repeat=0)
        if rc != 0:
            console.warn("Functional smoke test reported failures.")
            return StepResult.FAILED

        console.success("Build + smoke test passed. Project is ready.")
        return StepResult.OK


class UninstallStep(Step):
    """Remove packages and files that the installer placed on this system.

    With ``ctx.everything`` set, also removes toolchain binaries (cmake, git,
    ninja) that were downloaded by the direct-download manager. This is a
    destructive operation and cannot be undone automatically.
    """

    name = "uninstall"

    def __init__(self, source: IDependencySource,
                 managers: list[IPackageManager]) -> None:
        self._source = source
        self._managers = managers

    def _manager(self, name: str) -> IPackageManager | None:
        for m in self._managers:
            if m.name == name:
                return m
        return None

    def run(self, ctx: InstallContext) -> StepResult:
        if ctx.cfg.platform == "windows":
            return self._run_windows(ctx)
        return self._run_linux(ctx)

    def _run_linux(self, ctx: InstallContext) -> StepResult:
        linux_names = ["apt", "dnf", "yum", "pacman", "zypper"]
        mgr = next(
            (self._manager(n) for n in linux_names
             if self._manager(n) and self._manager(n).available()),  # type: ignore[union-attr]
            None,
        )
        pkgs: list[str] = []
        for dep in self._source.selected("linux", ctx.gpu):
            pkgs.extend(mgr.translate_apt(dep.linux_apt) if mgr else dep.linux_apt)
        pkgs = _dedup(pkgs)

        if mgr and pkgs:
            console.info(f"Removing via {mgr.name}: {', '.join(pkgs)}")
            mgr.remove(pkgs, ctx.dry_run)

        self._remove_config(ctx)
        return StepResult.OK

    def _run_windows(self, ctx: InstallContext) -> StepResult:
        vcpkg = self._manager("vcpkg")

        ports: list[str] = []
        for dep in self._source.selected("windows", ctx.gpu):
            ports.extend(dep.windows_vcpkg)
        ports = _dedup(ports)

        if vcpkg and ports:
            console.info(f"Removing vcpkg ports: {', '.join(ports)}")
            vcpkg.remove(ports, ctx.dry_run)

        # Remove the ninja extracted to the tools dir.
        tools = _tools_dir()
        ninja_exe = tools / "ninja.exe"
        if ninja_exe.is_file():
            if ctx.dry_run:
                console.info(f"(dry-run) would remove {ninja_exe}")
            else:
                ninja_exe.unlink()
                console.info(f"Removed {ninja_exe}")
            if tools.is_dir() and not any(tools.iterdir()):
                if not ctx.dry_run:
                    tools.rmdir()

        if ctx.everything:
            self._remove_toolchain_windows(ctx)

        self._remove_config(ctx)
        return StepResult.OK

    def _remove_toolchain_windows(self, ctx: InstallContext) -> None:
        """Remove cmake and git installed by the direct-download manager.

        This uses msiexec /x for CMake and the Git uninstaller found in the
        Program Files tree. Winget uninstall is tried first when available.
        """
        winget = self._manager("winget")
        if winget and winget.available():
            console.info("Removing toolchain via winget ...")
            winget.remove(["Kitware.CMake", "Git.Git"], ctx.dry_run)
            return

        # CMake MSI uninstall via product code discovery.
        if not ctx.dry_run:
            import subprocess as sp
            try:
                result = sp.run(
                    ["wmic", "product", "where", "name like 'CMake%'",
                     "get", "IdentifyingNumber", "/value"],
                    capture_output=True, text=True,
                )
                for line in result.stdout.splitlines():
                    if line.startswith("IdentifyingNumber="):
                        guid = line.split("=", 1)[1].strip()
                        sp.run(["msiexec", "/x", guid, "/quiet", "/norestart"],
                               timeout=120)
                        console.info("CMake uninstalled.")
            except Exception as exc:
                console.warn(f"CMake uninstall skipped: {exc}")

            # Git uninstaller lives alongside the binary.
            git_path = shutil.which("git")
            if git_path:
                uninstaller = Path(git_path).parent.parent / "unins000.exe"
                if uninstaller.is_file():
                    sp.run([str(uninstaller), "/VERYSILENT", "/NORESTART"],
                           timeout=120)
                    console.info("Git uninstalled.")
        else:
            console.info("(dry-run) would remove CMake and Git.")

    def _remove_config(self, ctx: InstallContext) -> None:
        target = config_dir() / "config.local.toml"
        if target.is_file():
            if ctx.dry_run:
                console.info(f"(dry-run) would remove {target}")
            else:
                target.unlink()
                console.success(f"Removed {target}")


# --------------------------------------------------------------------------- #
# Helpers
# --------------------------------------------------------------------------- #

def _mark(ok: bool) -> str:
    return "[green]OK[/green]" if ok else "[red]MISSING[/red]"


def _dedup(items: list[str]) -> list[str]:
    seen: set[str] = set()
    out: list[str] = []
    for item in items:
        if item not in seen:
            seen.add(item)
            out.append(item)
    return out
