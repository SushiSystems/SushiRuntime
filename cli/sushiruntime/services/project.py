# project.py
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026-present Mustafa Garip & Sushi Systems
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
import re
import shutil
from pathlib import Path

from sushicore import cmake_cache
from sushicore.cmake_driver import CMakeDriver
from sushicore.proc import Runner
from sushicore.provision.sushi_packages import PackageError

from .. import console
from ..config import Config, PROFILE, find_project_root, load_config
from ..env import load_build_env
from . import discovery, library_packages
from .install_hint import toolchain_install_command

# The ASan tree is separate from build/, CUDA off, Release and UBSan too; see cli/README.md.
ASAN_FLAGS = ("-fsanitize=address,undefined -fno-sanitize-recover=undefined "
              "-fno-omit-frame-pointer -g -O1")

# The Windows ASan flags differ from the Linux ones in three ways; see cli/README.md.
ASAN_FLAGS_WINDOWS = (
    "-Xarch_host -fsanitize=address -fsanitize-address-use-after-return=never "
    "-fno-omit-frame-pointer -g -O1 -D_DISABLE_STL_ANNOTATION=1")

# CMAKE_CXX_FLAGS of a Windows tree before the ASan flags; it holds no -fsycl, see cli/README.md.
BASE_CXX_FLAGS = ""

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


class Label(str, enum.Enum):
    # The values are the CTest labels cmake/SushiRuntimeTests.cmake assigns from suite prefixes.
    small = "small"
    medium = "medium"
    large = "large"
    quarantined = "quarantined"
    package = "package"         # installs, then builds and runs a consumer


# With no label, `sr test` runs every size and leaves out the package.
_DEFAULT_LABEL_REGEX = "^(small|medium|large)$"
# A quarantined test keeps its size label, so every run but the quarantined one excludes it.
_QUARANTINED_LABEL_REGEX = "^quarantined$"


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


def _build_dir(root: Path, asan: bool, requested: str | None = None) -> Path:
    """Return the tree of this build: *requested* below *root* when given, else the default one."""
    if requested:
        return root / requested
    return root / ("build-asan" if asan else "build")


_RUNNER = Runner(console, PROFILE.program)
_DRIVER = CMakeDriver(console, _RUNNER)

_API_SITE_DIR = Path("build") / "docs" / "api-site"
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


def _run_environment(cfg: Config, build_dir: Path) -> dict[str, str]:
    """Return the environment a binary of *build_dir* runs under.

    It is the build environment with the oneAPI library path repaired, and the run-time
    folders of the library packages the tree linked first on the search path.
    """
    env = load_build_env(cfg, build_dir)
    _neutralize_runtime_ld(cfg, env)
    library_packages.prepend_run_dirs(cfg, env, library_packages.read_run_dirs(build_dir))
    return env


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
        BASE_CXX_FLAGS,
        ASAN_FLAGS,
        ASAN_FLAGS_WINDOWS,
        ASAN_C_FLAGS_WINDOWS,
        ASAN_BENCHMARKS,
    ])
    return hashlib.sha256(material.encode("utf-8")).hexdigest()[:12]


def _configure_stamp(dist: str, build_type: str, gpu_backend: str,
                     tests: bool, toolchain: str, cxx: str,
                     sushi_definitions: tuple[str, ...] = (), lean: bool = False) -> str:
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

    ``sushi_definitions`` are the ``-D<Package>_DIR`` of the library packages and the
    ``-D<NAME>_DIR`` of any pin that is not one: a pin moved to another checkout, or a package
    in another folder, has to reach the cache the same way. ``lean`` leaves out the examples
    and the benchmarks as well as the tests.
    """
    return (f"{dist}|{build_type}|{gpu_backend}|{'ON' if tests else 'OFF'}"
            f"|{toolchain}|{cxx}|{'|'.join(sushi_definitions)}|{_flags_fingerprint()}"
            f"{'|lean' if lean else ''}")


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

    For ASan builds with the oneapi toolchain the resolver prefers a clang-based
    SYCL compiler: adaptivecpp (acpp), then the intel/llvm bundle's clang++. A
    clang++ found only on PATH is not taken, since a system clang has no SYCL.
    With neither, icpx builds the tree itself; it sanitizes the host pass.

    Returns the (cc, cxx) pair, or None to signal the caller to abort.
    """
    search_path = (env or {}).get("PATH")
    cc, cxx = cfg.resolved_compilers()

    # ASan + oneapi: prefer a clang-based SYCL compiler; a system clang++ has no SYCL.
    if asan and cfg.toolchain == "oneapi":
        llvm_bin = cfg.llvm_bin()
        candidates = [("gcc", shutil.which("acpp", path=search_path))]
        if llvm_bin:
            bundle_cxx = Path(llvm_bin) / "clang++"
            candidates.append((str(Path(llvm_bin) / "clang"),
                               str(bundle_cxx) if bundle_cxx.is_file() else None))
        for fallback_cc, fallback_cxx in candidates:
            if fallback_cxx is not None:
                console.info(f"ASan: using '{fallback_cxx}' in place of icpx.")
                return fallback_cc, fallback_cxx
        if shutil.which(cxx, path=search_path) is None:
            console.error(
                "ASan needs acpp, the intel/llvm bundle or icpx, and none was found.\n"
                "  - switch toolchain: `sr toolchain adaptivecpp` or `sr toolchain intel-llvm`")
            return None
        console.info("ASan: no clang-based SYCL compiler found; icpx sanitizes the host pass.")
        return cc, cxx

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
        # icpx passes -fsanitize to the SPIR-V device pass too, which warns under -Werror.
        cxx_flags = ASAN_FLAGS
        if Path(cxx).name == "icpx":
            cxx_flags = " ".join(f"-Xarch_host {flag}" if flag.startswith("-fsanitize")
                                 or flag.startswith("-fno-sanitize") else flag
                                 for flag in ASAN_FLAGS.split())
        args += [
            f"-DCMAKE_C_FLAGS={ASAN_FLAGS}",
            f"-DCMAKE_CXX_FLAGS={cxx_flags}",
            "-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address,undefined",
            "-DCMAKE_SHARED_LINKER_FLAGS=-fsanitize=address,undefined",
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
    # Always passed, so a tree configured with other flags is rewritten to these.
    asan_flags = ASAN_FLAGS if cfg.toolchain == "adaptivecpp" else ASAN_FLAGS_WINDOWS
    cxx_flags = " ".join(flag for flag in (BASE_CXX_FLAGS, asan_flags if asan else "") if flag)
    args += [f"-DCMAKE_CXX_FLAGS={cxx_flags}"]
    return args


#: What `--lean` turns off besides the tests: the example programs and the benchmarks.
_LEAN_ARGS = ("-DSR_BUILD_EXAMPLES=OFF", "-DSR_BUILD_BENCHMARKS=OFF")


def _package_compiler(cfg: Config, force: bool, env: dict[str, str],
                      asan: bool) -> tuple[str, str] | None:
    """Return the (cc, cxx) this build compiles with, or None after printing why it cannot.

    The library packages are built with the same C++ compiler, so it is settled before they are.
    Windows names one compiler per toolchain and checks that it is installed; Linux resolves
    the pair against the PATH of *env*.
    """
    if cfg.is_windows:
        if _check_windows_compiler(cfg, env) != 0:
            return None
        return "", cfg.resolved_windows_compiler()
    return _resolve_compiler(cfg, force, env, asan=asan)


def build(build_type: BuildType, distributed: bool = False,
          no_cuda: bool = False, clean: bool = False, force: bool = False,
          tests: bool = True, jobs: int | None = None, lean: bool = False,
          build_dir: str | None = None) -> int:
    """Configure and build the project.

    Args:
        lean: Builds no tests, examples or benchmarks; what a library package is built as.
        build_dir: The tree to build in, below the project root; the default tree when None.
    """
    console.header("Project Build")
    root = find_project_root()
    cfg = load_config()
    asan = build_type == BuildType.asan
    dist = "ON" if distributed else "OFF"
    cmake_build_type = _CMAKE_BUILD_TYPE[build_type]
    tree = _build_dir(root, asan, build_dir)
    tests = tests and not lean

    if clean:
        _DRIVER.clean_tree(tree)

    if distributed:
        console.info("Distributed master-worker layer: ON")
    if asan:
        console.info("AddressSanitizer: ON (isolated build-asan tree, CUDA OFF)")
    console.info(f"Tests: {'ON' if tests else 'OFF'}")
    if lean:
        console.info("Lean: no tests, examples or benchmarks")

    # cmake/gpu/DetectGpu selects the GPU backend on every platform; see cli/README.md.
    if no_cuda or asan:
        gpu_backend = "cpu"
    else:
        gpu_backend = "auto"

    if thread_sanitizer_applies(cfg.is_windows, build_type):
        console.info(f"ThreadSanitizer: ON (--type {build_type.value})")

    env = load_build_env(cfg, tree)

    compilers = _package_compiler(cfg, force, env, asan)
    if compilers is None:
        return 1
    cc, cxx = compilers
    try:
        packages = library_packages.resolve(
            cfg, root, build_type.value, cxx, console.info, console.warn)
    except PackageError as error:
        console.error(str(error))
        return 1
    run_dirs = packages.run_dirs(cfg.platform)
    library_packages.record_run_dirs(tree, run_dirs)
    library_packages.prepend_run_dirs(cfg, env, run_dirs)

    sushi_definitions = tuple(library_packages.configure_arguments(cfg, root, packages))
    stamp_key = _configure_stamp(dist, cmake_build_type, gpu_backend, tests,
                                 cfg.toolchain, cfg.cxx, sushi_definitions, lean)
    if _DRIVER.needs_configure(tree, cfg.generator, root=root) or             _stamp_differs(tree, stamp_key):
        console.info(f"Configuring CMake... (type={build_type.value}, "
                     f"distributed={dist}, gpu={gpu_backend}, tests={'ON' if tests else 'OFF'})")
        if tree.is_dir():
            shutil.rmtree(tree, ignore_errors=True)
        if cfg.is_windows:
            if asan and (rc := _check_sanitizer_runtime(cfg)) != 0:
                return rc
            args = _configure_args_windows(cfg, tree, cmake_build_type,
                                           gpu_backend, dist, asan, tests)
        else:
            args = _configure_args_linux(cfg, tree, cmake_build_type,
                                         gpu_backend, dist, asan, tests, cc, cxx)
            # A benchmark replaces operator new, which TSan's runtime defines as well.
            if thread_sanitizer_applies(cfg.is_windows, build_type):
                args.append(f"-DSR_BUILD_BENCHMARKS={ASAN_BENCHMARKS}")
        if lean:
            args += _LEAN_ARGS
        # Each library package reaches CMake as <Package>_DIR; CMake never downloads.
        args += list(sushi_definitions)
        rc = _DRIVER.configure(args, cwd=root, env=env)
        if rc != 0:
            console.error("CMake configure failed.")
            return rc
        tree.mkdir(parents=True, exist_ok=True)
        (tree / STAMP_NAME).write_text(stamp_key)
        library_packages.record_run_dirs(tree, run_dirs)

    console.info("Building...")
    rc = _DRIVER.compile(cfg, tree, cwd=root, env=env, config=cmake_build_type, jobs=jobs)
    if rc == 0:
        console.success("Build completed successfully!")
    else:
        console.error("Build failed.")
    return rc


def test(label: Label | None, distributed: bool = False, filter: str | None = None,
         asan: bool = False, repeat: int = 0, module: str | None = None,
         jobs: int | None = None) -> int:
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

    # ctest spawns the test binaries, so it needs the same LD_LIBRARY_PATH repair as `run`.
    env = _run_environment(cfg, build_dir)
    # An inherited CUDA_LAUNCH_BLOCKING=1 serializes every kernel and breaks the overlap tests.
    env.pop("CUDA_LAUNCH_BLOCKING", None)
    # A module's tests carry its folder name as a label, whatever their size.
    if module is not None:
        label_regex = f"^{re.escape(module)}$"
    elif label is not None:
        label_regex = f"^{label.value}$"
    else:
        label_regex = _DEFAULT_LABEL_REGEX
    exclude = None if label is Label.quarantined else _QUARANTINED_LABEL_REGEX
    return _DRIVER.ctest_run(
        cfg, build_dir, env, label_regex=label_regex, exclude_label_regex=exclude,
        filter=filter, repeat=repeat, jobs=jobs)


def bench(distributed: bool = False, filter: str | None = None) -> int:
    console.header("Project Bench")
    root = find_project_root()
    cfg = load_config()
    build_dir = _build_dir(root, False)
    programs = sorted(p for p in (build_dir / "tests").glob("sr_*_benchmarks*")
                      if p.is_file() and p.suffix in ("", ".exe"))
    if not programs:
        console.error("No benchmark program found. Run `sr build` with SR_BUILD_BENCHMARKS=ON.")
        return 1
    if distributed:
        console.info("Note: distributed benchmarks only exist if built with --distributed.")

    env = _run_environment(cfg, build_dir)
    env.pop("CUDA_LAUNCH_BLOCKING", None)
    forwarded = [f"--gtest_filter={filter}"] if filter else []
    failed = 0
    for program in programs:
        console.info(f"Executing: {program.name}")
        if _RUNNER.run([str(program), *forwarded], build_dir / "tests", env) != 0:
            failed += 1
    if failed:
        console.error(f"{failed} of {len(programs)} benchmark programs failed.")
        return 1
    console.success(f"{len(programs)} benchmark programs completed.")
    return 0


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

    env = _run_environment(cfg, build_dir)
    # An inherited CUDA_LAUNCH_BLOCKING=1 serializes every kernel and breaks the overlap tests.
    env.pop("CUDA_LAUNCH_BLOCKING", None)
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
    doxyfile = root / ".config" / "doxygen" / "Doxyfile"
    (root / _API_SITE_DIR).mkdir(parents=True, exist_ok=True)
    return _DRIVER.doxygen(cfg, doxyfile, root, env, install_hint=_DOXYGEN_HINT)
