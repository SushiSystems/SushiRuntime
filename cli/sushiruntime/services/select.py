# select.py
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026 Sushi Systems
# Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.
# Commercial use requires a licence from Sushi Systems.
"""SYCL toolchain selection (`sr toolchain`).

Persists the chosen toolchain into config.local.toml so every later build picks
it up. The choice flows to CMake as -DSR_SYCL_TOOLCHAIN at configure time.
"""

from __future__ import annotations

import shutil
from pathlib import Path

from rich.table import Table
from sushicore.provision import probe

from .. import console
from ..config import TOOLCHAINS, load_config, set_toolchain
from .install_hint import toolchain_install_command

# Short, user-facing description for each toolchain shown by `sr toolchain`.
_DESCRIPTIONS = {
    "intel-llvm": "Primary, vendor-neutral. intel/llvm nightly (clang++ -fsycl).",
    "adaptivecpp": "Secondary, vendor-neutral, smallest. AdaptiveCpp (acpp).",
    "oneapi": "Intel oneAPI (icpx / icx-cl). For VTune/Advisor profiling.",
}


def _toolchain_installed(name: str, cfg) -> bool:
    """Best-effort check: is the compiler for *name* actually present?"""
    win = cfg.is_windows
    if name == "intel-llvm":
        root = cfg.resolved_llvm_root()
        return bool(root) and \
            (Path(root) / "bin" / ("clang++.exe" if win else "clang++")).is_file()
    if name == "adaptivecpp":
        if cfg.acpp_exe and Path(cfg.expand(cfg.acpp_exe)).is_file():
            return True
        if any(probe.installed_toolchain("adaptivecpp", Path("bin") / n)
               for n in ("acpp.bat", "acpp")):
            return True
        return shutil.which("acpp") is not None
    if name == "oneapi":
        if cfg.icx_compiler and Path(cfg.expand(cfg.icx_compiler)).is_file():
            return True
        return bool(shutil.which("icx-cl") or shutil.which("icpx") or shutil.which("icx"))
    return False


def show() -> int:
    """Print a table of toolchains: which is active, which are installed, how to get the rest."""
    cfg = load_config()
    console.header("SYCL Toolchains")

    table = Table(show_header=True, header_style=console.accent)
    table.add_column("", width=3, justify="center")          # active marker
    table.add_column("Toolchain", style="bold")
    table.add_column("Status")
    table.add_column("Description", style="muted")
    table.add_column("Install with", style="cmd")

    for name in TOOLCHAINS:
        active = name == cfg.toolchain
        installed = _toolchain_installed(name, cfg)
        status = "[success]installed[/success]" if installed else "[warn]not installed[/warn]"
        get_it = "" if installed else toolchain_install_command(name)
        table.add_row(
            "[success]→[/success]" if active else "",
            name,
            status,
            _DESCRIPTIONS[name],
            get_it,
        )

    console.console.print(table)
    console.info(f"Active: [bold]{cfg.toolchain}[/bold]  "
                 f"(switch with [cmd]sr toolchain <name>[/cmd])")
    if not _toolchain_installed(cfg.toolchain, cfg):
        console.warn(f"The active toolchain '{cfg.toolchain}' is not installed yet — "
                     f"run [cmd]{toolchain_install_command(cfg.toolchain)}[/cmd] to install it.")
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
    console.console.print(f"[muted]{path}[/muted]")

    if not _toolchain_installed(toolchain, cfg):
        # The whole point of the message the user asked for: tell them exactly
        # which profile to install when the chosen toolchain isn't present.
        hint = toolchain_install_command(toolchain)
        console.warn(
            f"'{toolchain}' is not installed yet. Builds will fail until you install it:\n"
            f"    [cmd]{hint}[/cmd]"
        )
        if toolchain == "oneapi":
            console.info("oneAPI is a download of several GB.")
    else:
        console.info("Run [cmd]sr build --clean[/cmd] to reconfigure with the new toolchain.")
    return 0
