# config.py
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026 Sushi Systems
# Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.
# Commercial use requires a licence from Sushi Systems.
"""Loads the layered configuration of the SushiRuntime CLI.

See cli/README.md.
"""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

# sushicore owns the generic build-tool schema and loader; this file adds the SYCL fields.
from sushicore.config_base import write_tool_section
from sushicore.module_config import ModuleConfig
from sushicore.profile import ModuleProfile
from sushicore.provision import probe
from sushicore.provision.config import ProvisionSettings


# PROFILE names no siblings, because SushiRuntime is the bottom of the stack; see cli/README.md.
PROFILE = ModuleProfile(
    name="SushiRuntime",
    program="sr",
    env_prefix="SR",
    root_marker="sushi-module.toml",
    default_target="sr_functional_tests",
    extra_env_overrides={
        "toolchain": "SR_SYCL_TOOLCHAIN",
        "cc": "SR_CC",
        "oneapi_root": "SR_ONEAPI_ROOT",
        "icx_compiler": "SR_ICX",
        "llvm_root": "SR_LLVM_ROOT",
        "acpp_exe": "SR_ACPP",
    },
    # SushiRuntime selects the SYCL toolchain, so these variable families matter to it.
    env_of_interest=("CMPLR", "ONEAPI", "SYCL", "CUDA", "INTEL"),
)


class _RuntimeModuleConfig(ModuleConfig):
    """The shared loader, plus this repo's legacy config-directory fallback.

    Every other module keeps its config in ``cli/``. SushiRuntime used to call
    that directory ``.tools`` and still honours the old name when a checkout
    predates the rename, which is the one thing it cannot take from the shared
    implementation.
    """

    def config_dir(self, root: Path | None = None) -> Path:
        root = root or self.find_project_root()
        for name in ("cli", ".tools"):  # current name first, legacy fallback
            if (root / name / "config.toml").is_file():
                return root / name
        return root / "cli"


_MODULE = _RuntimeModuleConfig(PROFILE)

find_project_root = _MODULE.find_project_root
config_dir = _MODULE.config_dir


# TOOLCHAINS must match SR_SYCL_TOOLCHAIN in CMakeLists.txt.
TOOLCHAINS = ("intel-llvm", "adaptivecpp", "oneapi")

# Bundle directory names are listed in preference order; see cli/README.md.
INTEL_LLVM_BUNDLE_DIRS = ("llvm-sycl", "llvm-sycl-nightly")

# Holds the default (cc, cxx) pair per toolchain, used when the config pins no compiler.
TOOLCHAIN_COMPILERS = {
    # acpp is C++-only, so the C slot holds a plain C compiler the CXX-only project never runs.
    "adaptivecpp": ("gcc", "acpp"),
    "intel-llvm": ("clang", "clang++"),
    "oneapi": ("icx", "icpx"),
}

# Maps a Config field to the SR_* env var that overrides it.
_ENV_OVERRIDES = PROFILE.env_overrides()


@dataclass
class Config(ProvisionSettings):
    """Resolved, platform-specific tool configuration for the SYCL runtime build.

    Inherits the host build-tool fields and the toolchain roots (``oneapi_root``,
    ``icx_compiler``, ``llvm_root``, ``acpp_exe``) from :class:`ProvisionSettings`
    and adds the SYCL toolchain selection.
    """

    # llvm_root and acpp_exe locate the non-oneAPI SYCL compilers; see cli/README.md.

    # `sr toolchain` persists this value and configure passes it as -DSR_SYCL_TOOLCHAIN.
    toolchain: str = "intel-llvm"

    # An empty cc or cxx is derived from the toolchain through TOOLCHAIN_COMPILERS.
    cc: str = ""

    # Run defaults
    target_bin: str = "sr_functional_tests"

    def resolved_compilers(self) -> tuple[str, str]:
        """Return (cc, cxx), deriving them from the toolchain when not pinned.

        An explicit cc/cxx in the config always wins; otherwise the pair is
        taken from TOOLCHAIN_COMPILERS so selecting a toolchain is sufficient.
        """
        default_cc, default_cxx = TOOLCHAIN_COMPILERS.get(
            self.toolchain, TOOLCHAIN_COMPILERS["intel-llvm"])
        return (self.cc or default_cc, self.cxx or default_cxx)

    def resolved_llvm_root(self) -> str:
        """Return the intel/llvm bundle root to build with, or '' when none is present.

        A pinned ``llvm_root`` wins; otherwise the first bundle holding bin/clang++
        under the dependency roots is returned.
        """
        pinned = self.expand(self.llvm_root)
        if pinned:
            return pinned

        compiler = "clang++.exe" if self.is_windows else "clang++"
        for name in INTEL_LLVM_BUNDLE_DIRS:
            found = probe.installed_toolchain(name, Path("bin") / compiler)
            if found is not None:
                return str(found.parents[1])
        return ""

    def llvm_bin(self) -> str:
        """The intel/llvm bundle's bin directory, or '' when not configured."""
        root = self.resolved_llvm_root()
        return str(Path(root) / "bin") if root else ""

    def resolved_windows_compiler(self) -> str:
        """Return the C++ compiler to drive the Windows build for the toolchain.

        Windows has no system SYCL compiler, so each toolchain points at its own:
        intel-llvm -> clang++ from the intel/llvm bundle, oneapi -> icx-cl,
        adaptivecpp -> acpp. Falls back to a bare command name when the path is
        not configured so PATH resolution still has a chance.
        """
        if self.toolchain == "oneapi":
            return self.expand(self.icx_compiler) or "icx-cl"
        if self.toolchain == "adaptivecpp":
            return self.expand(self.acpp_exe) or "acpp"
        # intel-llvm (default)
        bin_dir = self.llvm_bin()
        return str(Path(bin_dir) / "clang++.exe") if bin_dir else "clang++"


def load_config() -> Config:
    """Load and resolve the layered configuration for the current platform.

    Precedence (low to high): repo config.toml -> workspace-shared
    config.local.toml (written by ``hub``) -> repo config.local.toml -> env.
    """
    cfg = _MODULE.load(Config)
    # Guard against a stale/typo'd toolchain leaking through from config or env.
    if cfg.toolchain not in TOOLCHAINS:
        cfg.toolchain = "intel-llvm"
    return cfg


def set_toolchain(toolchain: str) -> Path:
    """Persist the selected SYCL toolchain into config.local.toml.

    Writes a top-level ``[tool] toolchain = "..."`` key, preserving any existing
    ``[tool.<platform>]`` tables written by `hub install`. Returns the
    path that was written.
    """
    if toolchain not in TOOLCHAINS:
        raise ValueError(f"Unknown toolchain '{toolchain}'. Choose one of {', '.join(TOOLCHAINS)}.")

    return write_tool_section(
        config_dir() / "config.local.toml",
        {"toolchain": toolchain},
        [
            "# Managed by the SushiRuntime CLI. `sr toolchain` writes the toolchain key;",
            "# `hub install` writes the [tool.<platform>] tool paths.",
        ],
    )
