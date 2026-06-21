"""Tool-path probing and ``config.local.toml`` rendering.

Kept separate from the steps so ``ConfigureStep`` stays a thin orchestrator: this
module knows *where tools live*, the step knows *when to write them out*.

The values produced here mirror the fields the CLI's ``Config`` already reads
(see ``config.py``), so the generated ``config.local.toml`` plugs straight into
the existing layered-config + env-snapshot machinery with no other changes.
"""

from __future__ import annotations

import glob
import shutil
import subprocess
from pathlib import Path

from ..config import Config

# Common Windows install roots probed when a tool is not already on PATH.
_VS_VCVARS_GLOBS = [
    r"C:/Program Files/Microsoft Visual Studio/2022/*/VC/Auxiliary/Build/vcvars64.bat",
    r"C:/Program Files (x86)/Microsoft Visual Studio/2022/*/VC/Auxiliary/Build/vcvars64.bat",
]
_ONEAPI_ROOTS = [
    r"C:/Program Files (x86)/Intel/oneAPI",
    r"C:/Program Files/Intel/oneAPI",
]
_ICX_GLOBS = [
    r"C:/Program Files (x86)/Intel/oneAPI/compiler/*/bin/icx-cl.exe",
    r"C:/Program Files/Intel/oneAPI/compiler/*/bin/icx-cl.exe",
]


def _first_glob(patterns: list[str]) -> str:
    for pat in patterns:
        hits = sorted(glob.glob(pat))
        if hits:
            return hits[-1]  # latest version when sorted
    return ""


def _first_existing(paths: list[str]) -> str:
    for p in paths:
        if Path(p).exists():
            return p
    return ""


def _scoop_shim(name: str) -> str:
    """Return the scoop shim path for *name* if it exists, else ''.

    Scoop installs to ~/scoop/shims (or $SCOOP/shims), which a non-interactive
    shell often lacks on PATH even when the tool is installed.
    """
    import os
    roots = [os.environ.get("SCOOP", ""), str(Path.home() / "scoop")]
    for root in roots:
        if not root:
            continue
        shim = Path(root) / "shims" / f"{name}.exe"
        if shim.is_file():
            return str(shim)
    return ""


def _toolchains_dir() -> Path:
    """Where `sr setup` installs the intel-llvm bundle and AdaptiveCpp.

    Mirrors ``toolchains.toolchains_dir`` without importing it (that module pulls
    in package_managers, which imports this one — an import cycle).
    """
    import os
    local = os.environ.get("LOCALAPPDATA", "")
    base = Path(local) if local else Path.home() / ".local"
    return base / "SushiRuntime" / "toolchains"


def _discover_installed_toolchains(cfg: Config, values: dict[str, str]) -> None:
    """Record the paths of toolchains installed by `sr setup`, if present."""
    base = _toolchains_dir()
    clang = base / "llvm-sycl" / "bin" / ("clang++.exe" if cfg.platform == "windows" else "clang++")
    if clang.is_file():
        values["llvm_root"] = str(base / "llvm-sycl")
    for name in ("acpp.bat", "acpp"):
        acpp = base / "adaptivecpp" / "bin" / name
        if acpp.is_file():
            values["acpp_exe"] = str(acpp)
            break


def binary_works(cmd: str) -> bool:
    """Return True only if *cmd* is on PATH (or an absolute path) and runs cleanly.

    Tries --version first; falls back to -version for tools that use that flag.
    A missing binary, a crash, or a non-zero exit all return False.
    """
    try:
        if subprocess.run([cmd, "--version"], capture_output=True, timeout=15).returncode == 0:
            return True
        return subprocess.run([cmd, "-version"], capture_output=True, timeout=15).returncode == 0
    except Exception:
        return False


def find_sycl_compiler(cfg: Config) -> tuple[str | None, str]:
    """Return (compiler, location) for the active SYCL toolchain, or (None, '').

    Existence of the binary is not enough — a partial install can leave the exe
    on disk with missing DLLs. The binary is verified by running --version.
    """
    if cfg.platform == "windows":
        icx = cfg.expand(cfg.icx_compiler) if cfg.icx_compiler else ""
        if icx and Path(icx).is_file() and binary_works(icx):
            return ("icx-cl", icx)
        hit = _first_glob(_ICX_GLOBS) or shutil.which("icx-cl") or shutil.which("icx") or ""
        if hit and binary_works(hit):
            return ("icx-cl", hit)
        return (None, "")
    # Linux: prefer oneAPI icpx, then intel/llvm clang++.
    for cc in ("icpx", "clang++"):
        path = shutil.which(cc)
        if path and binary_works(path):
            return (cc, path)
    return (None, "")


def resolve_local_config(cfg: Config, gpu: bool = False) -> dict[str, str]:
    """Probe machine-specific tool paths to write into config.local.toml.

    Returns only the fields that were actually found, so we never write empty
    placeholders that would shadow the committed defaults.
    """
    if cfg.platform == "windows":
        return _resolve_windows(cfg)
    return _resolve_linux(cfg)


def _resolve_windows(cfg: Config) -> dict[str, str]:
    values: dict[str, str] = {}

    vcvars = cfg.expand(cfg.vs_vcvars) if cfg.vs_vcvars else ""
    if not (vcvars and Path(vcvars).is_file()):
        vcvars = _first_glob(_VS_VCVARS_GLOBS)
    if vcvars:
        values["vs_vcvars"] = vcvars

    oneapi = cfg.expand(cfg.oneapi_root) if cfg.oneapi_root else ""
    if not (oneapi and Path(oneapi).is_dir()):
        oneapi = _first_existing(_ONEAPI_ROOTS)
    if oneapi:
        values["oneapi_root"] = oneapi

    icx = _first_glob(_ICX_GLOBS) or shutil.which("icx-cl") or shutil.which("icx") or ""
    if icx:
        values["icx_compiler"] = icx

    ninja = shutil.which("ninja") or _scoop_shim("ninja")
    if ninja:
        values["ninja_exe"] = ninja

    # VS BuildTools omits the CMake component, so pin the discovered cmake/ctest
    # (commonly a scoop/standalone install) rather than relying on PATH at build
    # time, where the snapshotted env may not expose them. Fall back to the scoop
    # shims dir, which is often absent from a non-interactive shell's PATH.
    cmake = shutil.which("cmake") or _scoop_shim("cmake")
    if cmake:
        values["cmake_exe"] = cmake
    ctest = shutil.which("ctest") or _scoop_shim("ctest")
    if ctest:
        values["ctest_exe"] = ctest

    pkgconf = shutil.which("pkg-config") or shutil.which("pkgconf")
    vcpkg_root = cfg.expand(cfg.vcpkg_root) if cfg.vcpkg_root else ""
    # Ignore a configured path that points inside a conda/venv tree.
    if vcpkg_root and (".conda" in vcpkg_root or "envs" in vcpkg_root):
        vcpkg_root = ""
    if not (vcpkg_root and Path(vcpkg_root).is_dir()):
        # Prefer the conventional system-wide location, then user home.
        for candidate in ("C:/vcpkg", str(Path.home() / "vcpkg")):
            if Path(candidate).is_dir():
                vcpkg_root = candidate
                break
        else:
            # Not found anywhere — write the desired bootstrap target so
            # VcpkgManager clones there instead of defaulting to home.
            vcpkg_root = "C:/vcpkg"
    if vcpkg_root:
        values["vcpkg_root"] = vcpkg_root
        # pkgconf shipped by vcpkg is the one CMakeLists expects.
        vcpkg_pkgconf = Path(vcpkg_root) / "installed" / (cfg.vcpkg_triplet or "x64-windows") / "tools" / "pkgconf" / "pkgconf.exe"
        if vcpkg_pkgconf.is_file():
            pkgconf = str(vcpkg_pkgconf)
    if pkgconf:
        values["pkgconf_exe"] = pkgconf

    _discover_installed_toolchains(cfg, values)
    return values


def _resolve_linux(cfg: Config) -> dict[str, str]:
    values: dict[str, str] = {}
    compiler, path = find_sycl_compiler(cfg)
    if compiler == "clang++":
        # intel/llvm path: project.py already falls back, but make it explicit.
        values["cxx"] = "clang++"
        values["cc"] = "clang"
    oneapi = cfg.expand(cfg.oneapi_root) if cfg.oneapi_root else ""
    if oneapi and Path(oneapi, "setvars.sh").is_file():
        values["oneapi_root"] = oneapi
    del path
    _discover_installed_toolchains(cfg, values)
    return values


def render_local_config(platform: str, values: dict[str, str]) -> str:
    """Render values into the [tool.<platform>] layout config.py reads."""
    lines = [
        "# Auto-generated by `sr setup`. Machine-specific tool paths.",
        "# Safe to edit; re-running `sr setup configure` backs this up first.",
        "",
        f"[tool.{platform}]",
    ]
    for key in sorted(values):
        val = values[key].replace("\\", "/")
        lines.append(f'{key} = "{val}"')
    lines.append("")
    return "\n".join(lines)
