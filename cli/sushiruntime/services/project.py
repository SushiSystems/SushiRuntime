"""Project build / test / run / clean / doxygen logic.

This replaces the project/*.sh and project/*.bat scripts; the cmake / ctest
invocations are issued directly via subprocess with the snapshotted build env.
"""

from __future__ import annotations

import enum
import shutil
import subprocess
from pathlib import Path

from .. import console
from ..config import Config, find_project_root, load_config
from ..env import load_build_env
from . import discovery

# Isolated ASan tree. Kept separate from the CUDA `build/` so it is never
# clobbered: ASan forces CUDA OFF (the CUDA UR adapter's urVirtualMemReserve
# collides with ASan's reserved VA) and stays a Release build (so CMakeLists'
# Debug|RelWithDebInfo auto-TSan block never fires -- TSan and ASan can't coexist).
ASAN_FLAGS = "-fsanitize=address -fno-omit-frame-pointer -g -O1"
STAMP_NAME = ".sushi_dist"


class BuildType(str, enum.Enum):
    release = "release"
    debug = "debug"
    relwithdebinfo = "relwithdebinfo"
    asan = "asan"


class Suite(str, enum.Enum):
    # Fine-grained CTest labels (assigned in tests/CMakeLists.txt from the GTest
    # suite-name prefix convention). Each value is passed verbatim to `ctest -L`.
    unit = "unit"
    regression = "regression"
    integration = "integration"
    functional = "functional"   # umbrella: unit + regression + integration
    stress = "stress"
    performance = "performance"
    benchmark = "benchmark"     # umbrella: stress + performance
    all = "all"


_CMAKE_BUILD_TYPE = {
    BuildType.release: "Release",
    BuildType.debug: "Debug",
    BuildType.relwithdebinfo: "RelWithDebInfo",
    BuildType.asan: "Release",  # ASan stays Release; flags injected manually
}


def _build_dir(root: Path, asan: bool) -> Path:
    return root / ("build-asan" if asan else "build")


def _run(cmd: list[str], env: dict[str, str], cwd: Path) -> int:
    console.command(subprocess.list2cmdline(cmd))
    return subprocess.run(cmd, cwd=str(cwd), env=env).returncode


def _needs_configure(build_dir: Path, generator: str, dist: str) -> bool:
    if not build_dir.is_dir():
        return True
    sentinel = "build.ninja" if generator == "Ninja" else "Makefile"
    if not (build_dir / sentinel).is_file():
        return True
    stamp = build_dir / STAMP_NAME
    if stamp.is_file():
        return stamp.read_text().strip() != dist
    return dist != "OFF"


def _resolve_compiler(cfg: Config, force: bool) -> tuple[str, str] | None:
    """Resolve (cc, cxx) for the selected toolchain, validating availability.

    The pair is derived from the toolchain (or an explicit config override). If
    the chosen C++ compiler is on PATH it is used as-is. If it is missing the
    build stops with guidance — unless ``force`` is set, in which case it falls
    back to clang++/clang (which CMake then treats as the 'llvm' toolchain).

    Returns the (cc, cxx) pair, or None to signal the caller to abort.
    """
    cc, cxx = cfg.resolved_compilers()
    if shutil.which(cxx) is not None:
        return cc, cxx

    if not force:
        console.error(
            f"Toolchain '{cfg.toolchain}' needs '{cxx}', which is not on PATH.\n"
            f"  - install it, or\n"
            f"  - switch toolchain with `sr select <adaptivecpp|llvm|oneapi>`, or\n"
            f"  - re-run with --force to fall back to clang++/clang.")
        return None

    if shutil.which("clang++") is not None:
        console.warn(f"--force: '{cxx}' not found; falling back to clang++/clang "
                     f"(builds as the 'llvm' toolchain).")
        return "clang", "clang++"

    console.error(f"--force: neither '{cxx}' nor clang++ found on PATH.")
    return None


def _configure_args_linux(cfg: Config, build_dir: Path, build_type: str,
                          cuda: str, dist: str, asan: bool,
                          cc: str, cxx: str) -> list[str]:
    args = [
        "cmake", "-B", str(build_dir), "-G", cfg.generator,
        f"-DCMAKE_BUILD_TYPE={build_type}",
        f"-DSR_SYCL_TOOLCHAIN={cfg.toolchain}",
        f"-DCMAKE_C_COMPILER={cc}",
        f"-DCMAKE_CXX_COMPILER={cxx}",
        f"-DSR_ENABLE_CUDA={cuda}",
        f"-DSR_ENABLE_DISTRIBUTED={dist}",
    ]
    if asan:
        args += [
            f"-DCMAKE_C_FLAGS={ASAN_FLAGS}",
            f"-DCMAKE_CXX_FLAGS={ASAN_FLAGS}",
            "-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address",
            "-DCMAKE_SHARED_LINKER_FLAGS=-fsanitize=address",
        ]
    if cfg.use_vcpkg and cfg.vcpkg_root:
        vcpkg = cfg.expand(cfg.vcpkg_root)
        args += [
            f"-DCMAKE_TOOLCHAIN_FILE={vcpkg}/scripts/buildsystems/vcpkg.cmake",
            f"-DVCPKG_ROOT={vcpkg}",
        ]
    return args


def _configure_args_windows(cfg: Config, build_dir: Path, build_type: str,
                            dist: str, asan: bool) -> list[str]:
    vcpkg = cfg.expand(cfg.vcpkg_root)
    triplet = cfg.vcpkg_triplet
    asan_flags = f" {ASAN_FLAGS}" if asan else ""
    args = [
        "cmake", "-B", str(build_dir), "-G", "Ninja",
        f"-DCMAKE_BUILD_TYPE={build_type}",
        f"-DCMAKE_TOOLCHAIN_FILE={vcpkg}/scripts/buildsystems/vcpkg.cmake",
        f"-DVCPKG_ROOT={vcpkg}",
        f"-DVCPKG_TARGET_TRIPLET={triplet}",
        f"-DCMAKE_MAKE_PROGRAM={cfg.expand(cfg.ninja_exe)}",
        f"-DCMAKE_CXX_COMPILER={cfg.expand(cfg.icx_compiler)}",
        f"-DCMAKE_C_COMPILER={cfg.expand(cfg.icx_compiler)}",
        f"-DCMAKE_CXX_FLAGS=-fsycl{asan_flags}",
        f"-DCMAKE_C_FLAGS={asan_flags.strip()}",
        f"-DPKG_CONFIG_EXECUTABLE={cfg.expand(cfg.pkgconf_exe)}",
        f"-DCMAKE_PREFIX_PATH={vcpkg}/installed/{triplet}",
        "-DSR_ENABLE_CUDA=OFF",  # no nvptx toolchain on the Windows host
        f"-DSR_ENABLE_DISTRIBUTED={dist}",
        "-DSYCL_LIB_SUFFIX=9",
    ]
    return args


def build(build_type: BuildType, distributed: bool = False,
          no_cuda: bool = False, clean: bool = False, force: bool = False) -> int:
    console.header("Project Build")
    root = find_project_root()
    cfg = load_config()
    asan = build_type == BuildType.asan
    dist = "ON" if distributed else "OFF"
    cmake_build_type = _CMAKE_BUILD_TYPE[build_type]
    build_dir = _build_dir(root, asan)

    if clean:
        clean_tree(root, asan)

    if distributed:
        console.info("Distributed master-worker layer: ON")
    if asan:
        console.info("AddressSanitizer: ON (isolated build-asan tree, CUDA OFF)")

    # CUDA: Linux defaults ON (unless --no-cuda or ASan); Windows is always OFF.
    if cfg.is_windows:
        cuda = "OFF"
        if no_cuda:
            console.warn("--no-cuda is a no-op on Windows (CUDA is always OFF).")
    else:
        cuda = "OFF" if (no_cuda or asan) else "ON"

    if cfg.is_windows and build_type in (BuildType.debug, BuildType.relwithdebinfo):
        console.warn(f"--type {build_type.value} enables TSan, which is untested "
                     "with icx-cl on Windows.")

    env = load_build_env(cfg, build_dir)

    if _needs_configure(build_dir, cfg.generator, dist):
        console.info(f"Configuring CMake... (type={build_type.value}, "
                     f"distributed={dist}, cuda={cuda})")
        if build_dir.is_dir():
            shutil.rmtree(build_dir, ignore_errors=True)
        if cfg.is_windows:
            args = _configure_args_windows(cfg, build_dir, cmake_build_type, dist, asan)
        else:
            resolved = _resolve_compiler(cfg, force)
            if resolved is None:
                return 1
            cc, cxx = resolved
            args = _configure_args_linux(cfg, build_dir, cmake_build_type,
                                         cuda, dist, asan, cc, cxx)
        rc = _run(args, env, cwd=root)
        if rc != 0:
            console.error("CMake configure failed.")
            return rc
        build_dir.mkdir(parents=True, exist_ok=True)
        (build_dir / STAMP_NAME).write_text(dist)

    console.info("Building...")
    rc = _run(["cmake", "--build", str(build_dir), "--config", cmake_build_type],
              env, cwd=root)
    if rc == 0:
        console.success("Build completed successfully!")
    else:
        console.error("Build failed.")
    return rc


def test(suite: Suite, distributed: bool = False, filter: str | None = None,
         asan: bool = False, repeat: int = 0) -> int:
    console.header("Project Test")
    root = find_project_root()
    cfg = load_config()
    build_dir = _build_dir(root, asan)
    if not build_dir.is_dir():
        console.error(f"{build_dir.name}/ not found. Run `sr project build` first.")
        return 1
    if distributed:
        console.info("Note: distributed tests only exist if built with --distributed.")

    env = load_build_env(cfg, build_dir)
    cmd = ["ctest", "--test-dir", str(build_dir), "--output-on-failure"]
    if suite != Suite.all:
        cmd += ["-L", suite.value]
    if filter:
        # gtest_discover_tests registers tests as "Suite.Case"; ctest -R filters
        # those names directly (a richer alternative to --gtest_filter).
        cmd += ["-R", filter]
    if repeat > 0:
        # Re-run each selected test until it fails or N runs pass — the native
        # ctest way to flush out flakiness.
        cmd += ["--repeat", f"until-fail:{repeat}"]
        console.info(f"Repeating each test up to {repeat}x (stop on first failure).")
    return _run(cmd, env, cwd=build_dir)


def run(target: str | None = None, sort: bool = False,
        distributed_role: str | None = None, asan: bool = False,
        app_args: list[str] | None = None) -> int:
    console.header("Project Run")
    root = find_project_root()
    cfg = load_config()
    build_dir = _build_dir(root, asan)
    if not build_dir.is_dir():
        console.error(f"{build_dir.name}/ not found. Run `sr project build` first.")
        return 1

    env = load_build_env(cfg, build_dir)
    # CUDA_LAUNCH_BLOCKING=1 serializes every kernel and silently kills the
    # async/overlap tests; clear any inherited value so a stray one can't poison us.
    env.pop("CUDA_LAUNCH_BLOCKING", None)
    # ASan binaries were built CUDA-OFF; pin to the OpenCL CPU device so the SYCL
    # runtime never loads the CUDA UR adapter (which collides with ASan's VA).
    if asan:
        env.setdefault("ONEAPI_DEVICE_SELECTOR", "opencl:cpu")
        env.setdefault("ASAN_OPTIONS", "detect_leaks=0:abort_on_error=1")
        console.info(f"ASan run: device={env['ONEAPI_DEVICE_SELECTOR']}")

    if sort:
        exe = discovery.select_interactive(build_dir)
    elif target:
        exe = discovery.match_by_name(build_dir, target)
        if exe is None:
            console.error(f"Executable matching '{target}' not found.")
            return 1
    else:
        exe = discovery.match_by_name(build_dir, cfg.target_bin)
        if exe is None:
            console.error(f"Default target '{cfg.target_bin}' not found.")
            return 1

    if exe is None:
        return 1

    forwarded = list(app_args or [])
    if distributed_role:
        forwarded.append(f"--distributed={distributed_role}")

    console.info(f"Executing: {exe.name}")
    console.console.print(f"[dim]{exe}[/dim]")
    return _run([str(exe), *forwarded], env, cwd=root)


def clean_tree(root: Path, asan: bool = False) -> None:
    build_dir = _build_dir(root, asan)
    if build_dir.is_dir():
        console.info(f"Removing {build_dir}...")
        shutil.rmtree(build_dir, ignore_errors=True)
        console.success("Build directory cleaned.")
    else:
        console.info(f"{build_dir.name}/ does not exist, nothing to clean.")


def clean() -> int:
    console.header("Project Clean")
    root = find_project_root()
    clean_tree(root, asan=False)
    clean_tree(root, asan=True)
    return 0


def doxygen() -> int:
    console.header("Doxygen Generation")
    root = find_project_root()
    cfg = load_config()
    env = load_build_env(cfg, _build_dir(root, False))
    doxyfile = root / "Doxyfile"
    if not doxyfile.is_file():
        console.error("Doxyfile not found at repo root.")
        return 1
    (root / "docs").mkdir(exist_ok=True)
    return _run(["doxygen", "Doxyfile"], env, cwd=root)
