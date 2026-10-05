# project.py
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026 Sushi Systems
# Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.
# Commercial use requires a licence from Sushi Systems.
"""Project build / test / run / clean / doxygen logic.

This replaces the project/*.sh and project/*.bat scripts. It holds SushiRuntime's own
build policy — which cache variables it passes, which targets and test suites it has,
its sanitizer lanes and configure stamp — and drives cmake and ctest through
``sushicore.cmake_driver.CMakeDriver`` with the snapshotted build env. See cli/README.md.
"""

from __future__ import annotations

import enum
import hashlib
import os
import shutil
from pathlib import Path

from sushicore import cmake_cache
from sushicore.cmake_driver import CMakeDriver
from sushicore.proc import Runner

from .. import console
from ..config import Config, PROFILE, find_project_root, load_config
from ..env import load_build_env
from . import discovery
from .install_hint import toolchain_install_command

# The ASan tree is separate from build/, CUDA off and Release; see cli/README.md.
ASAN_FLAGS = "-fsanitize=address -fno-omit-frame-pointer -g -O1"

# The Windows ASan flags differ from the Linux ones in three ways; see cli/README.md.
ASAN_FLAGS_WINDOWS = (
    "-Xarch_host -fsanitize=address -fsanitize-address-use-after-return=never "
    "-fno-omit-frame-pointer -g -O1 -D_DISABLE_STL_ANNOTATION=1")

# The C driver has no device pass and takes no -Xarch_host; see cli/README.md.
ASAN_C_FLAGS_WINDOWS = ("-fsanitize=address -fsanitize-address-use-after-return=never "
                        "-fno-omit-frame-pointer -g -O1")

# An ASan build leaves the benchmark suite out; see cli/README.md.
ASAN_BENCHMARKS = "OFF"
STAMP_NAME = ".sushiruntime_dist"


class BuildType(str, enum.Enum):
    release = "release"
    debug = "debug"
    relwithdebinfo = "relwithdebinfo"
    asan = "asan"


class DistributedRole(str, enum.Enum):
    """Names the role `sr run --distributed` forwards to the application."""

    master = "master"
    worker = "worker"


class Suite(str, enum.Enum):
    # The values are the CTest labels tests/CMakeLists.txt assigns from GTest suite prefixes.
    unit = "unit"
    regression = "regression"
    integration = "integration"
    functional = "functional"   # umbrella: unit + regression + integration
    stress = "stress"
    performance = "performance"
    benchmark = "benchmark"     # umbrella: stress + performance
    package = "package"         # installs, then builds and runs a consumer
    all = "all"


# Each suite maps to a `ctest -L` regex, an umbrella to an alternation; see cli/README.md.
_SUITE_LABEL_REGEX = {
    Suite.unit: "^unit$",
    Suite.regression: "^regression$",
    Suite.integration: "^integration$",
    Suite.functional: "^(unit|integration|regression)$",
    Suite.stress: "^stress$",
    Suite.performance: "^performance$",
    Suite.benchmark: "^(stress|performance)$",
    Suite.package: "^package$",
}


_CMAKE_BUILD_TYPE = {
    BuildType.release: "Release",
    BuildType.debug: "Debug",
    BuildType.relwithdebinfo: "RelWithDebInfo",
    BuildType.asan: "Release",  # ASan stays Release; flags injected manually
}


def thread_sanitizer_applies(is_windows: bool, build_type: BuildType) -> bool:
    """Report whether cmake/Sanitizers.cmake wires ThreadSanitizer into this build.

    Mirrors that file's `if(NOT (MSVC OR (WIN32 AND Clang)))` guard: every Windows
    toolchain this CLI configures is either MSVC or the Clang-based intel/llvm
    driver, so the guard excludes Windows outright regardless of toolchain, and
    TSan only ever attaches to a Debug or RelWithDebInfo build on another platform.
    """
    if is_windows:
        return False
    return build_type in (BuildType.debug, BuildType.relwithdebinfo)


def _build_dir(root: Path, asan: bool) -> Path:
    return root / ("build-asan" if asan else "build")


_RUNNER = Runner(console, PROFILE.program)
_DRIVER = CMakeDriver(console, _RUNNER)

_DOXYGEN_HINT = (
    "  - Windows: winget install DimitriVanHeesch.Doxygen"
    "  (or: choco install doxygen.install)\n"
    "  - Linux:   apt-get install -y doxygen graphviz\n"
    "  - macOS:   brew install doxygen graphviz\n"
    "  - or set doxygen_exe in config.local.toml to an existing doxygen binary.")


def _neutralize_runtime_ld(cfg: Config, env: dict[str, str]) -> None:
    """Strip setvars' LD_LIBRARY_PATH from a *run-time* env for the oneAPI build.

    The oneAPI binary needs setvars at BUILD time, but not at RUN time: its
    build-tree RPATH already resolves the exact oneAPI libs it linked. Letting
    setvars' LD_LIBRARY_PATH ride along prepends a second libur_loader/umf/tcm
    set ahead of them, which mismatches the CUDA UR adapter and segfaults in
    SYCL device discovery (urVirtualMemMap). Restore the parent shell's known-
    good path instead of dropping it outright, so libcuda under nvidia/lib64
    stays reachable.
    """
    if cfg.toolchain != "oneapi":
        return
    shell_ld = os.environ.get("LD_LIBRARY_PATH")
    if shell_ld is not None:
        env["LD_LIBRARY_PATH"] = shell_ld
    else:
        env.pop("LD_LIBRARY_PATH", None)


def _flags_fingerprint() -> str:
    """A short digest of every compiler-flag constant this module can pass.

    These end up in the cache as CMAKE_CXX_FLAGS, which is as configure-affecting
    as any option — but they are constants in this file rather than arguments, so
    editing one changes what a fresh tree would get while leaving an existing tree
    untouched. That is how a fix to the ASan flags produced a build that kept
    failing with the old flags on its command line, indistinguishable from the fix
    not working. Folding them into the stamp makes an edit here self-healing.
    """
    material = " ".join([
        ASAN_FLAGS,
        ASAN_FLAGS_WINDOWS,
        ASAN_C_FLAGS_WINDOWS,
        ASAN_BENCHMARKS,
    ])
    return hashlib.sha256(material.encode("utf-8")).hexdigest()[:12]


def _configure_stamp(dist: str, build_type: str, gpu_backend: str,
                     tests: bool, toolchain: str, cxx: str) -> str:
    """A stable key of every configure-affecting knob.

    A single-config generator (Ninja) bakes the build type into the cache, so a
    stamp that tracked only ``dist`` let ``-t release`` then ``-t debug`` reuse
    the tree and silently hand back a Release binary. Keying on the full tuple
    forces a reconfigure whenever any of these change — including the flag
    constants themselves, via _flags_fingerprint().

    ``toolchain`` and ``cxx`` are here because leaving them out broke the one
    command whose whole job is to change them. ``sr toolchain adaptivecpp``
    followed by ``sr build`` matched the old stamp, so configure was skipped, the
    intel-llvm tree was reused, and acpp was never invoked — while
    SR_SYCL_TOOLCHAIN in that tree's cache still read intel-llvm, so nothing
    disagreed with anything. They decide both -DSR_SYCL_TOOLCHAIN and
    -DCMAKE_CXX_COMPILER, which makes them as configure-affecting as the build
    type, and the only warning was a printed hint on a branch a developer
    selecting a not-yet-installed toolchain never reaches.
    """
    return (f"{dist}|{build_type}|{gpu_backend}|{'ON' if tests else 'OFF'}"
            f"|{toolchain}|{cxx}|{_flags_fingerprint()}")


def _stamp_differs(build_dir: Path, stamp_key: str) -> bool:
    """Whether the tree's configure stamp disagrees with *stamp_key*, or is absent.

    Not part of CMakeDriver.needs_configure: STAMP_NAME and its contents are this CLI's
    own vocabulary (see _configure_stamp), not a cmake cache entry, so the generic driver
    has no way to express it. Called alongside the driver's own check, never folded into it.
    """
    stamp = build_dir / STAMP_NAME
    if stamp.is_file():
        return stamp.read_text().strip() != stamp_key
    # A tree without a stamp is reconfigured once, so it matches the requested configuration.
    return True


def _resolve_compiler(cfg: Config, force: bool,
                      env: dict[str, str] | None = None,
                      asan: bool = False) -> tuple[str, str] | None:
    """Resolve (cc, cxx) for the selected toolchain, validating availability.

    The pair is derived from the toolchain (or an explicit config override). If
    the chosen C++ compiler is on PATH it is used as-is. If it is missing the
    build stops with guidance — unless ``force`` is set, in which case it falls
    back to clang++/clang (which CMake then treats as the 'intel-llvm' toolchain).

    Availability is checked against the snapshotted build ``env`` PATH, not just
    the parent process PATH: the oneAPI ``icpx`` only appears after setvars is
    sourced (which ``load_build_env`` does), so the parent shell need not have it.

    For ASan builds the oneapi toolchain (icpx) is not suitable: Intel's ASan
    support is limited and icpx is absent outside the container. When asan=True
    and the selected toolchain is oneapi, the resolver automatically tries
    adaptivecpp (acpp) then intel-llvm (clang++) before giving up.

    Returns the (cc, cxx) pair, or None to signal the caller to abort.
    """
    search_path = (env or {}).get("PATH")
    cc, cxx = cfg.resolved_compilers()

    # ASan + oneapi: icpx has limited sanitizer support; prefer clang-based compilers.
    if asan and cfg.toolchain == "oneapi":
        for fallback_cc, fallback_cxx in [("gcc", "acpp"), ("clang", "clang++")]:
            if shutil.which(fallback_cxx, path=search_path) is not None:
                console.info(
                    f"ASan: toolchain 'oneapi' (icpx) does not support sanitizers; "
                    f"using '{fallback_cxx}' instead.")
                return fallback_cc, fallback_cxx
        console.error(
            "ASan requires a clang-based compiler (acpp or clang++), "
            "but neither was found on PATH.\n"
            "  - inside the container: run `sr docker run` first, or\n"
            "  - switch toolchain: `sr toolchain adaptivecpp` or `sr toolchain intel-llvm`")
        return None

    if shutil.which(cxx, path=search_path) is not None:
        return cc, cxx

    if not force:
        console.error(
            f"Toolchain '{cfg.toolchain}' needs '{cxx}', which is not on PATH.\n"
            f"  - install it, or\n"
            f"  - switch toolchain with `sr toolchain <intel-llvm|adaptivecpp|oneapi>`, or\n"
            f"  - re-run with --force to fall back to clang++/clang.")
        return None

    if shutil.which("clang++", path=search_path) is not None:
        console.warn(f"--force: '{cxx}' not found; falling back to clang++/clang "
                     f"(builds as the 'intel-llvm' toolchain).")
        return "clang", "clang++"

    console.error(f"--force: neither '{cxx}' nor clang++ found on PATH.")
    return None


def _configure_args_linux(cfg: Config, build_dir: Path, build_type: str,
                          gpu_backend: str, dist: str, asan: bool, tests: bool,
                          cc: str, cxx: str) -> list[str]:
    args = [
        _DRIVER.cmake(cfg), "-B", str(build_dir), "-G", cfg.generator,
        f"-DCMAKE_BUILD_TYPE={build_type}",
        f"-DSR_SYCL_TOOLCHAIN={cfg.toolchain}",
        f"-DCMAKE_C_COMPILER={cc}",
        f"-DCMAKE_CXX_COMPILER={cxx}",
        # cmake/gpu/DetectGpu resolves the vendor from "auto"; "cpu" forces the OpenCL path.
        f"-DSR_GPU_BACKEND={gpu_backend}",
        f"-DSR_ENABLE_DISTRIBUTED={dist}",
        f"-DSR_BUILD_TESTS={'ON' if tests else 'OFF'}",
    ]
    if asan:
        args += [
            f"-DCMAKE_C_FLAGS={ASAN_FLAGS}",
            f"-DCMAKE_CXX_FLAGS={ASAN_FLAGS}",
            "-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address",
            "-DCMAKE_SHARED_LINKER_FLAGS=-fsanitize=address",
            f"-DSR_BUILD_BENCHMARKS={ASAN_BENCHMARKS}",
        ]
    if cfg.use_vcpkg and cfg.vcpkg_root:
        vcpkg = cfg.expand(cfg.vcpkg_root)
        args += [
            f"-DCMAKE_TOOLCHAIN_FILE={vcpkg}/scripts/buildsystems/vcpkg.cmake",
            f"-DVCPKG_ROOT={vcpkg}",
        ]
    return args


def _check_windows_compiler(cfg: Config, env: dict[str, str]) -> int:
    """Fail early (with guidance) if the toolchain's compiler is not installed.

    Without this, an unresolved compiler (e.g. selecting the adaptivecpp toolchain
    before acpp is built) surfaces only as a cryptic 'is not a full path' CMake
    error several lines into configure.
    """
    cxx = cfg.resolved_windows_compiler()
    found = Path(cxx).is_file() or _RUNNER.resolve_exe(cxx, env) != cxx
    if found:
        return 0
    install = f"Install it with `{toolchain_install_command(cfg.toolchain)}`"
    hint = {
        "adaptivecpp": f"AdaptiveCpp (acpp) is not installed. {install}, or switch "
                       "back with `sr toolchain intel-llvm`.",
        "intel-llvm": f"The intel/llvm bundle (clang++) is not installed. {install}.",
        "oneapi": f"Intel oneAPI (icx-cl) is not installed. {install}, or set "
                  "icx_compiler in config.local.toml.",
    }.get(cfg.toolchain, "")
    console.error(
        f"Toolchain '{cfg.toolchain}' compiler '{cxx}' was not found.\n  - {hint}")
    return 1


def _check_sanitizer_runtime(cfg: Config) -> int:
    """Fail early if the selected intel/llvm bundle ships no AddressSanitizer runtime.

    An intel/llvm bundle can carry the compiler and no compiler-rt at all, in
    which case `lib/clang/<n>/` holds headers and nothing else. Configure then
    dies inside CMake's compiler probe on a missing
    `clang_rt.asan_dynamic_runtime_thunk.lib`, which reads as a broken build
    rather than as "this bundle cannot sanitize" — and the fix is to point at a
    bundle that can, not to change anything in the tree.

    :param cfg: Loaded CLI configuration.
    :return: 0 when a runtime is present or the check does not apply, 1 otherwise.
    """
    if not cfg.is_windows or cfg.toolchain != "intel-llvm":
        return 0

    root = cfg.resolved_llvm_root()
    if not root:
        return 0  # the compiler check above owns the "no bundle at all" case

    if any((Path(root) / "lib" / "clang").glob("*/lib/**/clang_rt.asan*")):
        return 0

    console.error(
        f"The intel/llvm bundle at {root} ships no AddressSanitizer runtime "
        f"(no clang_rt.asan* under lib/clang/*/lib).\n"
        f"  - an ASan build cannot link against it, and this is a property of the "
        f"bundle, not of the project.\n"
        f"  - install a bundle that carries compiler-rt with "
        f"`{toolchain_install_command(cfg.toolchain)}`, or point "
        f"llvm_root in cli/config.local.toml at one that does.")
    return 1


def _configure_args_windows(cfg: Config, build_dir: Path, build_type: str,
                            gpu_backend: str, dist: str, asan: bool,
                            tests: bool) -> list[str]:
    vcpkg = cfg.expand(cfg.vcpkg_root)
    triplet = cfg.vcpkg_triplet
    asan_flags = f" {ASAN_FLAGS_WINDOWS}" if asan else ""

    # -DSR_SYCL_TOOLCHAIN is explicit so CMakeLists does not infer it; see cli/README.md.
    cxx = cfg.resolved_windows_compiler()
    args = [
        _DRIVER.cmake(cfg), "-B", str(build_dir), "-G", "Ninja",
        f"-DCMAKE_BUILD_TYPE={build_type}",
        f"-DSR_SYCL_TOOLCHAIN={cfg.toolchain}",
        f"-DCMAKE_TOOLCHAIN_FILE={vcpkg}/scripts/buildsystems/vcpkg.cmake",
        f"-DVCPKG_ROOT={vcpkg}",
        f"-DVCPKG_TARGET_TRIPLET={triplet}",
        f"-DCMAKE_MAKE_PROGRAM={cfg.expand(cfg.ninja_exe)}",
        f"-DCMAKE_CXX_COMPILER={cxx}",
        f"-DPKG_CONFIG_EXECUTABLE={cfg.expand(cfg.pkgconf_exe)}",
        f"-DCMAKE_PREFIX_PATH={vcpkg}/installed/{triplet}",
        f"-DSR_GPU_BACKEND={gpu_backend}",
        f"-DSR_ENABLE_DISTRIBUTED={dist}",
        f"-DSR_BUILD_TESTS={'ON' if tests else 'OFF'}",
    ]
    if asan:
        args.append(f"-DSR_BUILD_BENCHMARKS={ASAN_BENCHMARKS}")
    if cfg.rc_exe:
        args.append(f"-DCMAKE_RC_COMPILER={cfg.expand(cfg.rc_exe)}")
    # acpp adds its own SYCL flags; -fsycl means something only to the clang-based drivers.
    if cfg.toolchain == "adaptivecpp":
        if asan:
            args += [f"-DCMAKE_CXX_FLAGS={ASAN_FLAGS}"]
    else:
        args += [f"-DCMAKE_CXX_FLAGS=-fsycl{asan_flags}"]
    return args


def build(build_type: BuildType, distributed: bool = False,
          no_cuda: bool = False, clean: bool = False, force: bool = False,
          tests: bool = True) -> int:
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
    console.info(f"Tests: {'ON' if tests else 'OFF'}")

    # cmake/gpu/DetectGpu selects the GPU backend on every platform; see cli/README.md.
    if no_cuda or asan:
        gpu_backend = "cpu"
    else:
        gpu_backend = "auto"

    if thread_sanitizer_applies(cfg.is_windows, build_type):
        console.info(f"ThreadSanitizer: ON (--type {build_type.value})")

    env = load_build_env(cfg, build_dir)

    stamp_key = _configure_stamp(dist, cmake_build_type, gpu_backend, tests,
                                 cfg.toolchain, cfg.cxx)
    if _DRIVER.needs_configure(build_dir, cfg.generator, root=root) or \
            _stamp_differs(build_dir, stamp_key):
        console.info(f"Configuring CMake... (type={build_type.value}, "
                     f"distributed={dist}, gpu={gpu_backend}, tests={'ON' if tests else 'OFF'})")
        if build_dir.is_dir():
            shutil.rmtree(build_dir, ignore_errors=True)
        if cfg.is_windows:
            if (rc := _check_windows_compiler(cfg, env)) != 0:
                return rc
            if asan and (rc := _check_sanitizer_runtime(cfg)) != 0:
                return rc
            args = _configure_args_windows(cfg, build_dir, cmake_build_type,
                                           gpu_backend, dist, asan, tests)
        else:
            resolved = _resolve_compiler(cfg, force, env, asan=asan)
            if resolved is None:
                return 1
            cc, cxx = resolved
            args = _configure_args_linux(cfg, build_dir, cmake_build_type,
                                         gpu_backend, dist, asan, tests, cc, cxx)
        rc = _DRIVER.configure(args, cwd=root, env=env)
        if rc != 0:
            console.error("CMake configure failed.")
            return rc
        build_dir.mkdir(parents=True, exist_ok=True)
        (build_dir / STAMP_NAME).write_text(stamp_key)

    console.info("Building...")
    rc = _DRIVER.compile(cfg, build_dir, cwd=root, env=env, config=cmake_build_type)
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
        console.error(f"{build_dir.name}/ not found. Run `sr build` first.")
        return 1
    if cmake_cache.is_stale(build_dir, root):
        console.error(
            f"{build_dir.name}/ was configured for a different source path "
            "(e.g. a Docker volume build reused on this host).\n"
            "  - run `sr build` to reconfigure it cleanly first.")
        return 1
    if distributed:
        console.info("Note: distributed tests only exist if built with --distributed.")

    env = load_build_env(cfg, build_dir)
    # ctest spawns the test binaries, so it needs the same LD_LIBRARY_PATH repair as `run`.
    _neutralize_runtime_ld(cfg, env)
    # An inherited CUDA_LAUNCH_BLOCKING=1 serializes every kernel and breaks the overlap tests.
    env.pop("CUDA_LAUNCH_BLOCKING", None)
    return _DRIVER.ctest_run(
        cfg, build_dir, env,
        label_regex=None if suite is Suite.all else _SUITE_LABEL_REGEX[suite],
        filter=filter, repeat=repeat)


def run(target: str | None = None, sort: bool = False,
        distributed_role: str | None = None, asan: bool = False,
        app_args: list[str] | None = None) -> int:
    console.header("Project Run")
    root = find_project_root()
    cfg = load_config()
    build_dir = _build_dir(root, asan)
    if not build_dir.is_dir():
        console.error(f"{build_dir.name}/ not found. Run `sr build` first.")
        return 1

    env = load_build_env(cfg, build_dir)
    # An inherited CUDA_LAUNCH_BLOCKING=1 serializes every kernel and breaks the overlap tests.
    env.pop("CUDA_LAUNCH_BLOCKING", None)
    _neutralize_runtime_ld(cfg, env)
    # ASan binaries are built CUDA-off, so the run is pinned to the OpenCL CPU device.
    if asan:
        env.setdefault("ONEAPI_DEVICE_SELECTOR", "opencl:cpu")
        env.setdefault("ASAN_OPTIONS", "detect_leaks=0:abort_on_error=1")
        console.info(f"ASan run: device={env['ONEAPI_DEVICE_SELECTOR']}")

    exe = discovery.resolve(build_dir, console, target=target, sort=sort, default=cfg.target_bin)
    if exe is None:
        return 1

    forwarded = list(app_args or [])
    if distributed_role:
        forwarded.append(f"--distributed={distributed_role}")

    console.info(f"Executing: {exe.name}")
    console.console.print(f"[muted]{exe}[/muted]")
    return _RUNNER.run([str(exe), *forwarded], root, env)


def clean_tree(root: Path, asan: bool = False) -> None:
    _DRIVER.clean_tree(_build_dir(root, asan))


def clean() -> int:
    console.header("Project Clean")
    root = find_project_root()
    clean_tree(root, asan=False)
    clean_tree(root, asan=True)
    return 0


def docs() -> int:
    """Builds the API reference with Doxygen and returns its exit code."""
    console.header("Doxygen Generation")
    root = find_project_root()
    cfg = load_config()
    env = load_build_env(cfg, _build_dir(root, False))
    doxyfile = root / "Doxyfile"
    (root / "docs").mkdir(exist_ok=True)
    return _DRIVER.doxygen(cfg, doxyfile, root, env, install_hint=_DOXYGEN_HINT)
