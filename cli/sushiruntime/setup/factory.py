"""Composition root: assemble the pipeline with platform-specific dependencies.

This is the one place that knows which concrete implementations to wire together
(Dependency Inversion in practice). Everything downstream depends only on the
abstractions, so swapping a package manager or dependency source for a test fake
happens here, not in the steps.
"""

from __future__ import annotations

from ..config import Config, load_config
from .dependency_source import IDependencySource, TomlDependencySource
from .package_managers import (
    AptManager,
    DirectDownloadWindowsManager,
    DnfManager,
    IPackageManager,
    PacmanManager,
    VcpkgManager,
    WingetManager,
    YumManager,
    ZypperManager,
)
from .pipeline import InstallContext, InstallPipeline
from .steps import (
    ConfigureStep,
    DetectStep,
    InstallDepsStep,
    UninstallStep,
    VerifyStep,
)

STEP_NAMES = ("detect", "install", "configure", "verify", "all")


def _managers_for(cfg: Config) -> list[IPackageManager]:
    if cfg.is_windows:
        return [WingetManager(), DirectDownloadWindowsManager(), VcpkgManager(cfg)]
    return [AptManager(), DnfManager(), YumManager(), PacmanManager(), ZypperManager()]


def build_pipeline(
    *,
    only: str = "all",
    gpu: bool = False,
    dry_run: bool = False,
    oneapi: bool = False,
    cfg: Config | None = None,
    source: IDependencySource | None = None,
    managers: list[IPackageManager] | None = None,
) -> tuple[InstallPipeline, InstallContext]:
    """Build the installer pipeline and its execution context.

    ``only`` selects a single step ('detect'|'install'|'configure'|'verify') or
    the whole sequence ('all'). ``source``/``managers`` can be injected for tests.
    """
    cfg = cfg or load_config()
    source = source or TomlDependencySource()
    managers = managers if managers is not None else _managers_for(cfg)

    all_steps = {
        "detect":    DetectStep(source),
        "install":   InstallDepsStep(source, managers),
        "configure": ConfigureStep(),
        "verify":    VerifyStep(),
    }

    if only == "all":
        ordered = [
            all_steps["detect"],
            all_steps["install"],
            all_steps["configure"],
            all_steps["verify"],
        ]
    elif only in all_steps:
        ordered = [all_steps[only]]
    else:
        raise ValueError(f"Unknown step '{only}'. Choose from {STEP_NAMES}.")

    ctx = InstallContext(cfg=cfg, gpu=gpu, dry_run=dry_run, oneapi=oneapi)
    return InstallPipeline(ordered), ctx


def build_uninstall_pipeline(
    *,
    gpu: bool = False,
    dry_run: bool = False,
    everything: bool = False,
    cfg: Config | None = None,
    source: IDependencySource | None = None,
    managers: list[IPackageManager] | None = None,
) -> tuple[InstallPipeline, InstallContext]:
    """Build a single-step pipeline that removes what the installer placed."""
    cfg = cfg or load_config()
    source = source or TomlDependencySource()
    managers = managers if managers is not None else _managers_for(cfg)

    step = UninstallStep(source, managers)
    ctx = InstallContext(cfg=cfg, gpu=gpu, dry_run=dry_run, everything=everything)
    return InstallPipeline([step]), ctx
