"""SYCL toolchain selection (`sr toolchain`).

Persists the chosen toolchain into config.local.toml so every later build picks
it up. The choice flows to CMake as -DSR_SYCL_TOOLCHAIN at configure time.
"""

from __future__ import annotations

import shutil
from pathlib import Path

from rich.table import Table

from .. import console
from ..config import TOOLCHAINS, deps_dir, load_config, set_toolchain

# Short, user-facing description for each toolchain shown by `sr toolchain`.
_DESCRIPTIONS = {
    "intel-llvm": "Primary, vendor-neutral. intel/llvm nightly (clang++ -fsycl).",
    "adaptivecpp": "Secondary, vendor-neutral, smallest. AdaptiveCpp (acpp).",
    "oneapi": "Intel oneAPI (icpx / icx-cl). For VTune/Advisor profiling.",
}


def _toolchain_installed(name: str, cfg) -> bool:
    """Best-effort check: is the compiler for *name* actually present?"""
    win = cfg.is_windows
    tdir = deps_dir() / "toolchains"
    if name == "intel-llvm":
        root = cfg.expand(cfg.llvm_root) if cfg.llvm_root else str(tdir / "llvm-sycl")
        return (Path(root) / "bin" / ("clang++.exe" if win else "clang++")).is_file()
    if name == "adaptivecpp":
        if cfg.acpp_exe and Path(cfg.expand(cfg.acpp_exe)).is_file():
            return True
        if any((tdir / "adaptivecpp" / "bin" / n).is_file() for n in ("acpp.bat", "acpp")):
            return True
        return shutil.which("acpp") is not None
    if name == "oneapi":
        if cfg.icx_compiler and Path(cfg.expand(cfg.icx_compiler)).is_file():
            return True
        return bool(shutil.which("icx-cl") or shutil.which("icpx") or shutil.which("icx"))
    return False


def _install_hint(name: str) -> str:
    """The command that installs toolchain *name*."""
    return f"ss install --customize   (pick '{name}')"


def show() -> int:
    """Print a table of toolchains: which is active, which are installed, how to get the rest."""
    cfg = load_config()
    console.header("SYCL Toolchains")

    table = Table(show_header=True, header_style=console.accent)
    table.add_column("", width=3, justify="center")          # active marker
    table.add_column("Toolchain", style="bold")
    table.add_column("Status")
    table.add_column("Description", style="dim")
    table.add_column("Install with", style="cyan")

    for name in TOOLCHAINS:
        active = name == cfg.toolchain
        installed = _toolchain_installed(name, cfg)
        status = "[green]installed[/green]" if installed else "[yellow]not installed[/yellow]"
        get_it = "" if installed else _install_hint(name)
        table.add_row(
            "[bold green]→[/bold green]" if active else "",
            name,
            status,
            _DESCRIPTIONS[name],
            get_it,
        )

    console.console.print(table)
    console.info(f"Active: [bold]{cfg.toolchain}[/bold]  "
                 f"(switch with [cyan]sr toolchain <name>[/cyan])")
    if not _toolchain_installed(cfg.toolchain, cfg):
        console.warn(f"The active toolchain '{cfg.toolchain}' is not installed yet — "
                     f"run [cyan]{_install_hint(cfg.toolchain)}[/cyan] to install it.")
    return 0


def select(toolchain: str) -> int:
    """Persist the chosen toolchain, warning clearly if it is not installed."""
    toolchain = toolchain.strip().lower()
    if toolchain not in TOOLCHAINS:
        console.error(f"Unknown toolchain '{toolchain}'. "
                      f"Choose one of: {', '.join(TOOLCHAINS)}.")
        return 1

    cfg = load_config()
    path = set_toolchain(toolchain)
    console.success(f"Toolchain set to '{toolchain}'.")
    console.console.print(f"[dim]{path}[/dim]")

    if not _toolchain_installed(toolchain, cfg):
        # The whole point of the message the user asked for: tell them exactly
        # which profile to install when the chosen toolchain isn't present.
        hint = _install_hint(toolchain)
        console.warn(
            f"'{toolchain}' is not installed yet. Builds will fail until you install it:\n"
            f"    [bold cyan]{hint}[/bold cyan]"
        )
        if toolchain == "oneapi":
            console.info("oneAPI is several GB; use [cyan]ss install --customize[/cyan] "
                         "to install it on its own instead of everything.")
    else:
        console.info("Run [cyan]sr build --clean[/cyan] to reconfigure with the new toolchain.")
    return 0
