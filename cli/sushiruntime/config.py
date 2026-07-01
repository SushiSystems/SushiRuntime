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
# skeleton live in sushicli; this repo adds only the SYCL-specific fields below.
from sushicli.config_base import ToolConfig, load_tool_config, write_tool_section
from sushicli.workspace import has_marker, resolve_env_path, walk_up


def find_project_root(start: Path | None = None) -> Path:
    """Locate the repo root by walking up from CWD until CMakeLists.txt is found.

    The CLI is installed (pip/pipx) outside the repo, so the package location
    tells us nothing about where the project lives — the invocation directory
    does. Run any `sr` command from anywhere inside the checkout.
    """
    root = walk_up(start or Path.cwd(), has_marker("CMakeLists.txt"))
    if root is None:
        raise SystemExit(
            "Not inside a SushiRuntime project: no CMakeLists.txt found in the "
            "current directory or any parent. cd into the repo and try again."
        )
    return root


def config_dir(root: Path | None = None) -> Path:
    """Directory holding config.toml / config.local.toml (the repo's cli/)."""
    root = root or find_project_root()
    for name in ("cli", ".tools"):  # current name first, legacy fallback
        if (root / name / "config.toml").is_file():
            return root / name
    return root / "cli"

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
    ``<workspace>/dependencies`` (provisioned by ``ss install`` and used by every
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
_ENV_OVERRIDES = {
    "toolchain": "SR_SYCL_TOOLCHAIN",
    "cc": "SR_CC",
    "cxx": "SR_CXX",
    "generator": "SR_CMAKE_GENERATOR",
    "vcpkg_root": "SR_VCPKG_ROOT",
    "oneapi_root": "SR_ONEAPI_ROOT",
    "vs_vcvars": "SR_VCVARS",
    "ninja_exe": "SR_NINJA",
    "cmake_exe": "SR_CMAKE",
    "ctest_exe": "SR_CTEST",
    "icx_compiler": "SR_ICX",
    "llvm_root": "SR_LLVM_ROOT",
    "acpp_exe": "SR_ACPP",
    "pkgconf_exe": "SR_PKGCONF",
    "doxygen_exe": "SR_DOXYGEN",
    "vcpkg_triplet": "SR_VCPKG_TRIPLET",
    "target_bin": "SR_TARGET_BIN",
}


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
    # they are discovered by `ss install` and written to config.local.toml.
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

    def llvm_bin(self) -> str:
        """The intel/llvm bundle's bin directory, or '' when not configured."""
        root = self.expand(self.llvm_root)
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


def _shared_config_local() -> Path | None:
    """The workspace-shared config.local.toml ``ss install`` writes, if any.

    Inside a SushiStack workspace the machine-specific tool paths (compiler,
    vcpkg, cmake) are resolved once by ``ss`` and written to ``<home>/cli/
    config.local.toml``; every module reads them from there so nothing is
    configured twice.
    """
    home = sushistack_home()
    return (home / "cli" / "config.local.toml") if home else None


def load_config() -> Config:
    """Load and resolve the layered configuration for the current platform.

    Precedence (low to high): repo config.toml -> workspace-shared
    config.local.toml (written by ``ss``) -> repo config.local.toml -> env.
    """
    plat = platform.system().lower()  # 'windows' | 'linux' | 'darwin'

    cfg_dir = config_dir()
    shared = _shared_config_local()
    sources = [cfg_dir / "config.toml"]
    if shared is not None:
        sources.append(shared)
    sources.append(cfg_dir / "config.local.toml")
    cfg = load_tool_config(Config, sources, plat, _ENV_OVERRIDES)
    # Guard against a stale/typo'd toolchain leaking through from config or env.
    if cfg.toolchain not in TOOLCHAINS:
        cfg.toolchain = "intel-llvm"
    return cfg


def set_toolchain(toolchain: str) -> Path:
    """Persist the selected SYCL toolchain into config.local.toml.

    Writes a top-level ``[tool] toolchain = "..."`` key, preserving any existing
    ``[tool.<platform>]`` tables written by `ss install`. Returns the
    path that was written.
    """
    if toolchain not in TOOLCHAINS:
        raise ValueError(f"Unknown toolchain '{toolchain}'. Choose one of {', '.join(TOOLCHAINS)}.")

    return write_tool_section(
        config_dir() / "config.local.toml",
        {"toolchain": toolchain},
        [
            "# Managed by the SushiRuntime CLI. `sr toolchain` writes the toolchain key;",
            "# `ss install` writes the [tool.<platform>] tool paths.",
        ],
    )
