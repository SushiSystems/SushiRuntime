"""Concrete pipeline steps.

Each step has a single responsibility and depends only on abstractions:
``DetectStep`` inventories the machine, ``InstallDepsStep`` installs missing
manifest packages through injected package managers, ``ConfigureStep`` writes
``config.local.toml`` from probed tool paths, and ``VerifyStep`` builds and
smoke-tests through the existing project service.
"""

from __future__ import annotations

import shutil
import subprocess
from pathlib import Path

from rich.table import Table

from .. import console
from ..config import config_dir
from . import probe
from .dependency_source import Dependency, IDependencySource
from .package_managers import IPackageManager
from .pipeline import InstallContext, Step, StepResult

# System toolchain that is not a C++ library dependency (so it lives here, not in
# the manifest): the build tools every platform needs on PATH.
_LINUX_TOOLCHAIN_APT = ["build-essential", "cmake", "ninja-build", "git"]
_WINDOWS_TOOLCHAIN_WINGET = [
    "Kitware.CMake", "Ninja-build.Ninja", "Git.Git",
]


def _dep_installed(dep: Dependency, mgr: IPackageManager, platform: str) -> bool:
    """True if a dependency is already satisfied (falls back to package manager if check_cmd fails)."""
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

        # Generic tools (presence on PATH).
        for tool in ("python3" if plat != "windows" else "python",
                     "git", "cmake", "ninja", "pkg-config"):
            found = shutil.which(tool) is not None
            ctx.detected[tool] = found
            table.add_row(tool, _mark(found), shutil.which(tool) or "")

        # Toolchain compiler.
        compiler, where = probe.find_sycl_compiler(ctx.cfg)
        ctx.detected["sycl_compiler"] = compiler is not None
        table.add_row("SYCL compiler", _mark(compiler is not None),
                      f"{compiler or '-'} {where or ''}".strip())

        # Manifest dependencies.
        for dep in self._source.selected(plat, ctx.gpu):
            # Detection here is informational; can't always check without the
            # package manager, so just record declared presence via check_cmd.
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

        # GPU.
        gpu_present = shutil.which("nvidia-smi") is not None
        ctx.detected["nvidia_gpu"] = gpu_present
        table.add_row("NVIDIA GPU", _mark(gpu_present),
                      "use --gpu to install CUDA" if gpu_present else "")

        console.console.print(table)
        return StepResult.OK


class InstallDepsStep(Step):
    """Install missing manifest dependencies + system toolchain."""

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
        plat = ctx.cfg.platform
        if plat == "windows":
            return self._run_windows(ctx)
        return self._run_linux(ctx)

    # -- Linux: everything through apt -------------------------------------- #
    def _run_linux(self, ctx: InstallContext) -> StepResult:
        apt = self._manager("apt")
        if apt is None or not apt.available():
            msg = "apt-get not available. Only Debian/Ubuntu is supported."
            if ctx.dry_run:
                console.warn(f"(dry-run) {msg} Would stop here on a real run.")
                return StepResult.SKIPPED
            console.error(msg)
            return StepResult.FAILED

        pkgs: list[str] = list(_LINUX_TOOLCHAIN_APT)
        for dep in self._source.selected("linux", ctx.gpu):
            if _dep_installed(dep, apt, "linux"):
                console.info(f"{dep.name}: already installed, skipping.")
                continue
            pkgs.extend(dep.linux_apt)

        pkgs = _dedup(pkgs)
        if not pkgs:
            return StepResult.SKIPPED
        console.info(f"Installing via apt: {', '.join(pkgs)}")
        ok = apt.install(pkgs, ctx.dry_run)
        ctx.installed.extend(pkgs)
        # The SYCL compiler (intel/llvm or oneAPI) is a toolchain bootstrap, not
        # an apt library; surface guidance rather than failing the pipeline.
        if not ctx.detected.get("sycl_compiler", False):
            console.warn(
                "No SYCL compiler detected. Install the intel/llvm nightly "
                "toolchain or Intel oneAPI DPC++ (see README / Dockerfile)."
            )
        return StepResult.OK if ok else StepResult.FAILED

    # -- Windows: toolchain via winget, libs via vcpkg ---------------------- #
    def _run_windows(self, ctx: InstallContext) -> StepResult:
        winget = self._manager("winget")
        vcpkg = self._manager("vcpkg")
        if winget is None or not winget.available():
            msg = ("winget not available. Install 'App Installer' from the "
                   "Microsoft Store, then re-run.")
            if ctx.dry_run:
                console.warn(f"(dry-run) {msg} Would stop here on a real run.")
                return StepResult.SKIPPED
            console.error(msg)
            return StepResult.FAILED

        console.info("Installing system toolchain via winget...")
        tool_ok = winget.install(_WINDOWS_TOOLCHAIN_WINGET, ctx.dry_run)

        # C++ library ports through vcpkg.
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

        if winget and winget.available() and not winget.is_installed("Microsoft.VisualStudio.2022.BuildTools"):
            if ctx.dry_run:
                console.info("(dry-run) skipping VS Build Tools install.")
            else:
                vs_cmd = [
                    "winget", "install", "--id", "Microsoft.VisualStudio.2022.BuildTools", "-e",
                    "--accept-package-agreements", "--accept-source-agreements",
                    "--override", '--add Microsoft.VisualStudio.Workload.VCTools --includeRecommended --quiet --wait --norestart'
                ]
                import subprocess
                with console.console.status("[bold cyan]Installing Visual Studio Build Tools (C++ workloads) in the background... This may take 10-20 minutes.", spinner="bouncingBar"):
                    rc = subprocess.run(vs_cmd).returncode
                if rc != 0:
                    console.warn("Visual Studio Build Tools installation failed or was cancelled.")
                    tool_ok = False
                else:
                    console.success("Visual Studio Build Tools installed successfully.")

        if not ctx.detected.get("sycl_compiler", False):
            if ctx.dry_run:
                console.info("(dry-run) skipping Intel oneAPI install.")
            else:
                oneapi_url = "https://registrationcenter-download.intel.com/akdlm/IRC_NAS/bae85ab1-cfcd-4251-8d42-a0c27949ea33/intel-oneapi-toolkit-2026.0.0.193_offline.exe"
                installer = Path.home() / "intel-oneapi-toolkit-offline.exe"
                
                if not installer.is_file():
                    console.info(f"Downloading Intel oneAPI Installer (~4GB) from Intel servers...")
                    dl_rc = subprocess.run(["curl", "-L", "-o", str(installer), oneapi_url]).returncode
                    if dl_rc != 0:
                        console.error("Failed to download Intel oneAPI installer.")
                        tool_ok = False
                
                if installer.is_file() and tool_ok:
                    oneapi_cmd = [str(installer), "-s", "-a", "--silent", "--eula", "accept", "-p=NEED_VS2022_INTEGRATION=1"]
                    with console.console.status("[bold cyan]Executing oneAPI installer silently... This may take 10-20 minutes.", spinner="bouncingBar"):
                        rc = subprocess.run(oneapi_cmd).returncode
                    if rc != 0:
                        console.warn("Intel oneAPI Toolkit installation failed.")
                        tool_ok = False
                    else:
                        console.success("Intel oneAPI Toolkit installed successfully.")

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
            # markup=False: the [tool.<platform>] header must not be parsed as
            # rich markup (it would silently vanish from the preview).
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

        # Imported lazily to avoid a heavy import at module load.
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
