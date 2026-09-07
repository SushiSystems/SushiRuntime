"""Layered configuration loading for the SushiRuntime CLI.

Precedence (lowest to highest):
    built-in defaults -> config.toml -> config.local.toml -> SR_* env vars

The active platform's ``[tool.<platform>]`` table is merged over the common
``[tool]`` table, so a single file describes both Linux and Windows.
"""

from __future__ import annotations

import os
import platform

from dataclasses import dataclass
from pathlib import Path

# Domain-agnostic config plumbing shared by every Sushi* CLI. The generic build-
# tool schema (cmake/ninja/vcpkg paths) and the layered-load / [tool]-write
# skeleton live in sushicore; this repo adds only the SYCL-specific fields below.
from sushicore.config_base import ToolConfig, write_tool_section
from sushicore.module_config import ModuleConfig
from sushicore.profile import ModuleProfile
from sushicore.workspace import has_marker, resolve_env_path, walk_up


#: How SushiRuntime describes itself to the shared machinery. Unlike the heads
#: of the stack it names no siblings -- it is the bottom, and the owner of the
#: toolchain bundle they borrow -- and it carries six overrides of its own, for
#: the toolchain selection no other module makes.
PROFILE = ModuleProfile(
    name="SushiRuntime",
    program="sr",
    env_prefix="SR",
    root_marker="CMakeLists.txt",
    default_target="sr_functional_tests",
    extra_env_overrides={
        "toolchain": "SR_SYCL_TOOLCHAIN",
        "cc": "SR_CC",
        "oneapi_root": "SR_ONEAPI_ROOT",
        "icx_compiler": "SR_ICX",
        "llvm_root": "SR_LLVM_ROOT",
        "acpp_exe": "SR_ACPP",
    },
    # SushiRuntime is the module that actually selects a SYCL toolchain, so it
    # is the one that cares about the oneAPI, Intel and CUDA variables.
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


def sushistack_home(root: Path | None = None) -> Path | None:
    """Locate the enclosing SushiStack workspace root, or None when standalone.

    SushiStack is the umbrella that provisions one shared dependency tree for the
    whole stack. When the runtime is checked out inside a workspace
    (``<workspace>/sushiruntime``), its toolchain and vcpkg come from
    ``<workspace>/dependencies`` rather than a repo-local tree. Resolution:
    ``SUSHISTACK_HOME`` env var, then a walk up for the ``.sushistack`` marker.
    """
    home = resolve_env_path("SUSHISTACK_HOME")
    if home:
        return home
    try:
        start = root or find_project_root()
    except SystemExit:
        return None
    return walk_up(start, has_marker(".sushistack"))


def deps_dir() -> Path:
    """The single self-contained directory the shared toolchain lives in.

    Everything vendorable — the intel/llvm bundle, AdaptiveCpp, a portable CMake
    and Ninja, and the vcpkg tree with its C++ library ports — lands under here,
    so a user can see exactly what was fetched and reclaim it all by deleting one
    folder. Inside a SushiStack workspace this is the shared
    ``<workspace>/dependencies`` (provisioned by ``hub install`` and used by every
    module); standalone it is ``<repo>/dependencies`` (git-ignored). Override with
    ``SUSHISTACK_DEPS_DIR``. Falls back to a user-local path when not inside a
    checkout.

    System-level prerequisites that cannot live in one folder (the host C++
    compiler — MSVC+SDK on Windows, gcc and a few -dev packages on Linux — and
    CUDA) are intentionally *not* placed here; the installer reports them instead.
    """
    override = os.environ.get("SUSHISTACK_DEPS_DIR")
    if override:
        return Path(override)
    home = sushistack_home()
    if home:
        return home / "dependencies"
    try:
        return find_project_root() / "dependencies"
    except SystemExit:
        local = os.environ.get("LOCALAPPDATA", "")
        base = Path(local) if local else Path.home() / ".local"
        return base / "SushiRuntime" / "dependencies"


# The SYCL toolchains a user can select. Must match SR_SYCL_TOOLCHAIN in
# CMakeLists.txt: intel-llvm (primary), adaptivecpp (secondary), oneapi (supported).
TOOLCHAINS = ("intel-llvm", "adaptivecpp", "oneapi")

# Directory names `hub install` may have unpacked the intel/llvm bundle under, in
# preference order. Two names exist because the installer names the bundle for
# what it is (a nightly) while this repo's older default assumed a plain
# `llvm-sycl`, and a machine that has been through both installers ends up with
# both directories side by side. Probing in order rather than hardcoding one name
# keeps the two repos from disagreeing about a path neither fully owns; a
# `llvm_root` written into config.local.toml still wins over all of them.
INTEL_LLVM_BUNDLE_DIRS = ("llvm-sycl-nightly", "llvm-sycl")

# Default compiler pair (cc, cxx) per toolchain. Used when the config does not
# pin an explicit compiler, so `sr toolchain <name>` is enough to switch.
TOOLCHAIN_COMPILERS = {
    # (cc, cxx). acpp is C++-only, so the C slot uses a plain C compiler; the
    # project builds CXX only, so cc is effectively unused but kept valid.
    "adaptivecpp": ("gcc", "acpp"),
    "intel-llvm": ("clang", "clang++"),
    "oneapi": ("icx", "icpx"),
}

# Maps a Config field to the SR_* env var that overrides it.
_ENV_OVERRIDES = PROFILE.env_overrides()


@dataclass
class Config(ToolConfig):
    """Resolved, platform-specific tool configuration for the SYCL runtime build.

    Inherits the generic host build-tool fields (cmake/ninja/vcpkg paths, etc.)
    from :class:`ToolConfig` and adds the SYCL toolchain selection and the
    compiler roots the runtime provisions.
    """

    # SYCL toolchain selection (intel-llvm | adaptivecpp | oneapi). Persisted by
    # `sr toolchain` and consumed as -DSR_SYCL_TOOLCHAIN at configure time.
    toolchain: str = "intel-llvm"

    # The C compiler. Empty cc/cxx (cxx from ToolConfig) means "derive from the
    # toolchain" via TOOLCHAIN_COMPILERS.
    cc: str = ""

    oneapi_root: str = ""
    icx_compiler: str = ""
    # intel/llvm nightly bundle root (holds bin/clang++) for the intel-llvm
    # toolchain, and the AdaptiveCpp compiler for the adaptivecpp toolchain.
    # On Windows these are how the non-oneAPI toolchains provide a SYCL compiler;
    # they are discovered by `hub install` and written to config.local.toml.
    llvm_root: str = ""
    acpp_exe: str = ""

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
        """The intel/llvm bundle root to build with, or '' when none is present.

        Prefers an explicit ``llvm_root`` from config.local.toml, since a user who
        pinned a path meant it. Otherwise probes the shared dependency tree for
        the names ``hub install`` uses, so a fresh checkout finds the bundle that
        is actually installed instead of the one this repo used to assume.

        :return: An absolute bundle root holding bin/clang++, or '' if no
                 configured or probed candidate has one.
        """
        compiler = "clang++.exe" if self.is_windows else "clang++"

        pinned = self.expand(self.llvm_root)
        if pinned:
            return pinned

        toolchains = deps_dir() / "toolchains"
        for name in INTEL_LLVM_BUNDLE_DIRS:
            candidate = toolchains / name
            if (candidate / "bin" / compiler).is_file():
                return str(candidate)
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
