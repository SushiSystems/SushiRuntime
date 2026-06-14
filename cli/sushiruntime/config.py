"""Layered configuration loading for the SushiRuntime CLI.

Precedence (lowest to highest):
    built-in defaults -> config.toml -> config.local.toml -> SUSHI_* env vars

The active platform's ``[tool.<platform>]`` table is merged over the common
``[tool]`` table, so a single file describes both Linux and Windows.
"""

from __future__ import annotations

import os
import platform

try:
    import tomllib  # Python 3.11+
except ModuleNotFoundError:  # Python 3.10 fallback
    import tomli as tomllib
from dataclasses import dataclass, fields
from pathlib import Path

def find_project_root(start: Path | None = None) -> Path:
    """Locate the repo root by walking up from CWD until CMakeLists.txt is found.

    The CLI is installed (pip/pipx) outside the repo, so the package location
    tells us nothing about where the project lives — the invocation directory
    does. Run any `sr` command from anywhere inside the checkout.
    """
    cur = (start or Path.cwd()).resolve()
    for d in (cur, *cur.parents):
        if (d / "CMakeLists.txt").is_file():
            return d
    raise SystemExit(
        "Not inside a SushiRuntime project: no CMakeLists.txt found in the "
        "current directory or any parent. cd into the repo and try again."
    )


def config_dir(root: Path | None = None) -> Path:
    """Directory holding config.toml / config.local.toml (the repo's cli/)."""
    root = root or find_project_root()
    for name in ("cli", ".tools"):  # current name first, legacy fallback
        if (root / name / "config.toml").is_file():
            return root / name
    return root / "cli"

# Maps a Config field to the SUSHI_* env var that overrides it.
_ENV_OVERRIDES = {
    "cc": "SUSHI_CC",
    "cxx": "SUSHI_CXX",
    "generator": "SUSHI_CMAKE_GENERATOR",
    "vcpkg_root": "SUSHI_VCPKG_ROOT",
    "oneapi_root": "SUSHI_ONEAPI_ROOT",
    "vs_vcvars": "SUSHI_VCVARS",
    "ninja_exe": "SUSHI_NINJA",
    "icx_compiler": "SUSHI_ICX",
    "pkgconf_exe": "SUSHI_PKGCONF",
    "vcpkg_triplet": "SUSHI_VCPKG_TRIPLET",
    "target_bin": "SUSHI_TARGET_BIN",
}


@dataclass
class Config:
    """Resolved, platform-specific tool configuration."""

    # Compilers / build
    cc: str = "icx"
    cxx: str = "icpx"
    generator: str = "Ninja"
    use_vcpkg: bool = False

    # Tool roots / paths (mostly Windows-specific absolutes)
    vcpkg_root: str = ""
    oneapi_root: str = ""
    vs_vcvars: str = ""
    ninja_exe: str = ""
    icx_compiler: str = ""
    pkgconf_exe: str = ""
    vcpkg_triplet: str = "x64-windows"

    # Run defaults
    target_bin: str = "sr_functional_tests"

    # Derived
    platform: str = ""

    @property
    def is_windows(self) -> bool:
        return self.platform == "windows"

    def expand(self, value: str) -> str:
        """Expand ~ and env vars in a path-like config value."""
        return os.path.expandvars(os.path.expanduser(value)) if value else value


def _read_toml(path: Path) -> dict:
    if not path.is_file():
        return {}
    with path.open("rb") as fh:
        return tomllib.load(fh)


def _merge_tool_table(doc: dict, plat: str) -> dict:
    """Merge common [tool] with [tool.<platform>] (platform wins)."""
    tool = dict(doc.get("tool", {}))
    merged = {k: v for k, v in tool.items() if not isinstance(v, dict)}
    plat_table = tool.get(plat, {})
    if isinstance(plat_table, dict):
        merged.update(plat_table)
    return merged


def load_config() -> Config:
    """Load and resolve the layered configuration for the current platform."""
    plat = platform.system().lower()  # 'windows' | 'linux' | 'darwin'

    cfg_dir = config_dir()
    values: dict = {}
    for fname in ("config.toml", "config.local.toml"):
        doc = _read_toml(cfg_dir / fname)
        if doc:
            values.update(_merge_tool_table(doc, plat))

    # Env overrides (highest precedence below CLI flags).
    for field_name, env_var in _ENV_OVERRIDES.items():
        if env_var in os.environ:
            values[field_name] = os.environ[env_var]

    # Coerce booleans that may arrive as strings from env.
    if isinstance(values.get("use_vcpkg"), str):
        values["use_vcpkg"] = values["use_vcpkg"].strip().lower() in ("1", "true", "yes", "on")

    known = {f.name for f in fields(Config)}
    cfg = Config(**{k: v for k, v in values.items() if k in known})
    cfg.platform = plat
    return cfg
