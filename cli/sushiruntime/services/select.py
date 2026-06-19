"""SYCL toolchain selection (`sr toolchain`).

Persists the chosen toolchain into config.local.toml so every later build picks
it up. The choice flows to CMake as -DSR_SYCL_TOOLCHAIN at configure time.
"""

from __future__ import annotations

from .. import console
from ..config import TOOLCHAINS, load_config, set_toolchain

# Short, user-facing description for each toolchain shown by `sr toolchain`.
_DESCRIPTIONS = {
    "intel-llvm": "intel/llvm nightly (clang++ -fsycl) — primary, vendor-neutral. Recommended.",
    "adaptivecpp": "AdaptiveCpp (acpp) — secondary, vendor-neutral.",
    "oneapi": "Intel oneAPI (icpx/icx-cl) — supported, for VTune/Advisor profiling.",
}


def show() -> int:
    """Print the active toolchain and the available choices."""
    cfg = load_config()
    console.header("SYCL Toolchain")
    for name in TOOLCHAINS:
        marker = "*" if name == cfg.toolchain else " "
        console.console.print(f" {marker} [bold]{name}[/bold] — {_DESCRIPTIONS[name]}")
    console.info(f"Active: {cfg.toolchain} (change with `sr toolchain <name>`)")
    return 0


def select(toolchain: str) -> int:
    """Persist the chosen toolchain, or list choices when given none."""
    toolchain = toolchain.strip().lower()
    if toolchain not in TOOLCHAINS:
        console.error(f"Unknown toolchain '{toolchain}'. "
                      f"Choose one of: {', '.join(TOOLCHAINS)}.")
        return 1
    path = set_toolchain(toolchain)
    console.success(f"Toolchain set to '{toolchain}'.")
    console.console.print(f"[dim]{path}[/dim]")
    console.info("Run `sr project build --clean` to reconfigure with the new toolchain.")
    return 0
