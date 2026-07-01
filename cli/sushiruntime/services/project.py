"""Project build / test / run / clean / doxygen logic.

This replaces the project/*.sh and project/*.bat scripts; the cmake / ctest
invocations are issued directly via subprocess with the snapshotted build env.
"""

from __future__ import annotations

import enum
import os
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
    # suite-name prefix convention).
    unit = "unit"
    regression = "regression"
    integration = "integration"
    functional = "functional"   # umbrella: unit + regression + integration
    stress = "stress"
    performance = "performance"
    benchmark = "benchmark"     # umbrella: stress + performance
    all = "all"


# Each suite maps to a CTest label-selection regex (`ctest -L`). The single
# categories anchor an exact match; the two umbrellas (functional / benchmark)
# are an alternation over their sub-labels rather than a label of their own,
# because a baked-in umbrella label does not survive gtest_discover_tests on
# current CMake (see tests/CMakeLists.txt). `all` selects everything (no -L).
_SUITE_LABEL_REGEX = {
    Suite.unit: "^unit$",
    Suite.regression: "^regression$",
    Suite.integration: "^integration$",
    Suite.functional: "^(unit|integration|regression)$",
    Suite.stress: "^stress$",
    Suite.performance: "^performance$",
    Suite.benchmark: "^(stress|performance)$",
}


_CMAKE_BUILD_TYPE = {
    BuildType.release: "Release",
    BuildType.debug: "Debug",
    BuildType.relwithdebinfo: "RelWithDebInfo",
    BuildType.asan: "Release",  # ASan stays Release; flags injected manually
}


def _build_dir(root: Path, asan: bool) -> Path:
    return root / ("build-asan" if asan else "build")


def _cmake(cfg: Config) -> str:
    """The cmake executable: the configured path if set, else bare 'cmake'."""
    return cfg.expand(cfg.cmake_exe) if cfg.cmake_exe else "cmake"


def _ctest(cfg: Config) -> str:
    """The ctest executable: the configured path if set, else bare 'ctest'."""
    return cfg.expand(cfg.ctest_exe) if cfg.ctest_exe else "ctest"


def _resolve_exe(name: str, env: dict[str, str]) -> str:
    """Return the full path to *name* from env PATH, or *name* itself as fallback.

    Using the full path sidesteps Windows PATH case-sensitivity bugs when a
    plain-dict env is passed to subprocess (os.environ uses 'Path', overlays
    may use 'PATH' — subprocess sees both and picks unpredictably).
    """
    env_path = next((v for k, v in env.items() if k.upper() == "PATH"), None)
    found = shutil.which(name, path=env_path) or shutil.which(name)
    return found or name


def _run(cmd: list[str], env: dict[str, str], cwd: Path) -> int:
    # Resolve the executable to a full path so subprocess.CreateProcess never
    # relies on PATH lookup from the env dict (which may have case-mismatched keys
    # on Windows when cmake/ninja live in scoop shims).
    resolved = list(cmd)
    resolved[0] = _resolve_exe(cmd[0], env)
    console.command(subprocess.list2cmdline(resolved))
    try:
        return subprocess.run(resolved, cwd=str(cwd), env=env).returncode
    except FileNotFoundError:
        console.error(
            f"Executable not found: '{cmd[0]}'.\n"
            f"  - it is not on PATH and no explicit path is configured.\n"
            f"  - set its path in config.local.toml (e.g. cmake_exe / ctest_exe / ninja_exe), or\n"
            f"  - re-run `ss doctor` to check the shared dependency tree.")
        return 1


def _run_drained(cmd: list[str], env: dict[str, str], cwd: Path) -> int:
    """Run *cmd* while actively draining its output, echoing it line by line.

    Unlike :func:`_run` (which lets the child inherit our stdout), this pipes the
    child's stdout/stderr and reads them in this process. That matters for
    ``ctest`` on Windows: its ``gtest_discover_tests`` step spawns the test binary
    to enumerate cases, and when ctest's stdout is an inherited, slowly-drained
    pipe (our own stdout, itself piped by the caller's shell) the discovery child
    intermittently stalls and registers nothing — surfacing as "No tests were
    found". Draining the pipe ourselves keeps that enumeration reliable, while we
    re-emit every line so the live test output is unchanged for the user.
    """
    resolved = list(cmd)
    resolved[0] = _resolve_exe(cmd[0], env)
    console.command(subprocess.list2cmdline(resolved))
    try:
        proc = subprocess.Popen(
            resolved, cwd=str(cwd), env=env,
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
            text=True, bufsize=1)
    except FileNotFoundError:
        console.error(
            f"Executable not found: '{cmd[0]}'.\n"
            f"  - it is not on PATH and no explicit path is configured.\n"
            f"  - set its path in config.local.toml (e.g. cmake_exe / ctest_exe / ninja_exe), or\n"
            f"  - re-run `ss doctor` to check the shared dependency tree.")
        return 1
    assert proc.stdout is not None
    for line in proc.stdout:
        # markup=False: child output (ctest/GTest logs) carries literal "[file:line]"
        # tags that Rich would otherwise parse as console markup and abort on.
        console.console.print(line.rstrip("\n"), markup=False, highlight=False, soft_wrap=True)
    return proc.wait()


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


def _cache_home_dir(build_dir: Path) -> str | None:
    """Return the source dir a build tree was configured against, or None.

    Read from CMakeCache.txt's CMAKE_HOME_DIRECTORY. Used to detect a build tree
    carried over from another machine/path (e.g. a Docker volume build mounted at
    /workspace, reused on the Windows host): those absolute paths no longer exist,
    so CTest's `if(EXISTS ...)` guards fail and ctest reports "No tests were
    found" even though the sources are fine. Reconfiguring from scratch fixes it.
    """
    cache = build_dir / "CMakeCache.txt"
    if not cache.is_file():
        return None
    try:
        for line in cache.read_text(errors="ignore").splitlines():
            if line.startswith("CMAKE_HOME_DIRECTORY:"):
                return line.partition("=")[2].strip()
    except OSError:
        return None
    return None


def _stale_build_tree(build_dir: Path, root: Path) -> bool:
    """True if the build tree was configured for a different source directory.

    A path mismatch (case- and separator-insensitive) means the cache's baked-in
    absolute paths are wrong for this checkout, so a clean reconfigure is required.
    """
    home = _cache_home_dir(build_dir)
    if not home:
        return False
    return os.path.normcase(os.path.normpath(home)) != \
        os.path.normcase(os.path.normpath(str(root)))


def _needs_configure(build_dir: Path, generator: str, dist: str,
                     root: Path) -> bool:
    if not build_dir.is_dir():
        return True
    sentinel = "build.ninja" if generator == "Ninja" else "Makefile"
    if not (build_dir / sentinel).is_file():
        return True
    if _stale_build_tree(build_dir, root):
        console.warn("Build tree was configured for a different source path "
                     "(e.g. a Docker volume build); reconfiguring from scratch.")
        return True
    stamp = build_dir / STAMP_NAME
    if stamp.is_file():
        return stamp.read_text().strip() != dist
    return dist != "OFF"


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
                          cuda: str, dist: str, asan: bool, tests: bool,
                          cc: str, cxx: str) -> list[str]:
    args = [
        _cmake(cfg), "-B", str(build_dir), "-G", cfg.generator,
        f"-DCMAKE_BUILD_TYPE={build_type}",
        f"-DSR_SYCL_TOOLCHAIN={cfg.toolchain}",
        f"-DCMAKE_C_COMPILER={cc}",
        f"-DCMAKE_CXX_COMPILER={cxx}",
        f"-DSR_ENABLE_CUDA={cuda}",
        f"-DSR_ENABLE_DISTRIBUTED={dist}",
        f"-DSR_BUILD_TESTS={'ON' if tests else 'OFF'}",
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


def _check_windows_compiler(cfg: Config, env: dict[str, str]) -> int:
    """Fail early (with guidance) if the toolchain's compiler is not installed.

    Without this, an unresolved compiler (e.g. selecting the adaptivecpp toolchain
    before acpp is built) surfaces only as a cryptic 'is not a full path' CMake
    error several lines into configure.
    """
    cxx = cfg.resolved_windows_compiler()
    found = Path(cxx).is_file() or _resolve_exe(cxx, env) != cxx
    if found:
        return 0
    hint = {
        "adaptivecpp": "AdaptiveCpp (acpp) is not installed. Install it with "
                       "`ss install`, or switch back with `sr toolchain intel-llvm`.",
        "intel-llvm": "The intel/llvm bundle (clang++) is not installed. Install it "
                      "with `ss install`.",
        "oneapi": "Intel oneAPI (icx-cl) is not installed. Install it with "
                  "`ss install`, or set icx_compiler in config.local.toml.",
    }.get(cfg.toolchain, "")
    console.error(
        f"Toolchain '{cfg.toolchain}' compiler '{cxx}' was not found.\n  - {hint}")
    return 1


def _configure_args_windows(cfg: Config, build_dir: Path, build_type: str,
                            dist: str, asan: bool, tests: bool) -> list[str]:
    vcpkg = cfg.expand(cfg.vcpkg_root)
    triplet = cfg.vcpkg_triplet
    asan_flags = f" {ASAN_FLAGS}" if asan else ""

    # The compiler is toolchain-specific: intel-llvm -> clang++, oneapi -> icx-cl,
    # adaptivecpp -> acpp. Pass -DSR_SYCL_TOOLCHAIN explicitly so CMakeLists does
    # not auto-force it from the compiler name (e.g. icx-cl => oneapi), which would
    # diverge from the environment the CLI actually snapshots.
    cxx = cfg.resolved_windows_compiler()
    args = [
        _cmake(cfg), "-B", str(build_dir), "-G", "Ninja",
        f"-DCMAKE_BUILD_TYPE={build_type}",
        f"-DSR_SYCL_TOOLCHAIN={cfg.toolchain}",
        f"-DCMAKE_TOOLCHAIN_FILE={vcpkg}/scripts/buildsystems/vcpkg.cmake",
        f"-DVCPKG_ROOT={vcpkg}",
        f"-DVCPKG_TARGET_TRIPLET={triplet}",
        f"-DCMAKE_MAKE_PROGRAM={cfg.expand(cfg.ninja_exe)}",
        f"-DCMAKE_CXX_COMPILER={cxx}",
        f"-DCMAKE_C_COMPILER={cxx}",
        f"-DPKG_CONFIG_EXECUTABLE={cfg.expand(cfg.pkgconf_exe)}",
        f"-DCMAKE_PREFIX_PATH={vcpkg}/installed/{triplet}",
        "-DSR_ENABLE_CUDA=OFF",  # no nvptx toolchain on the Windows host
        f"-DSR_ENABLE_DISTRIBUTED={dist}",
        f"-DSR_BUILD_TESTS={'ON' if tests else 'OFF'}",
        "-DSYCL_LIB_SUFFIX=9",
    ]
    # AdaptiveCpp (acpp) injects its own SYCL flags via add_sycl_to_target; the
    # -fsycl flag is only meaningful to the clang-based intel-llvm/oneapi drivers.
    if cfg.toolchain == "adaptivecpp":
        if asan:
            args += [f"-DCMAKE_CXX_FLAGS={ASAN_FLAGS}",
                     f"-DCMAKE_C_FLAGS={ASAN_FLAGS}"]
    else:
        args += [
            f"-DCMAKE_CXX_FLAGS=-fsycl{asan_flags}",
            f"-DCMAKE_C_FLAGS={asan_flags.strip()}",
        ]
    return args


def _cuda_usable() -> bool:
    """True when an NVIDIA CUDA toolchain can actually build the nvptx64 backend.

    The SYCL CUDA backend needs the CUDA toolkit's libdevice, so a mere NVIDIA
    driver (nvidia-smi) is not enough — nvcc must be present. Checking nvcc keeps
    the Linux build GPU-agnostic: NVIDIA + CUDA => GPU build, anything else =>
    the CPU/OpenCL path, with no confusing libdevice error.
    """
    return shutil.which("nvcc") is not None


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

    # CUDA: Linux defaults ON (unless --no-cuda or ASan); Windows is always OFF.
    if cfg.is_windows:
        cuda = "OFF"
        if no_cuda:
            console.warn("--no-cuda is a no-op on Windows (CUDA is always OFF).")
    elif no_cuda or asan:
        cuda = "OFF"
    elif _cuda_usable():
        cuda = "ON"
    else:
        # GPU-agnostic default: without an NVIDIA GPU + CUDA toolkit the nvptx64
        # backend can't build (clang: "cannot find libdevice for sm_61"), so fall
        # back to the CPU/OpenCL (SPIR) path instead of hard-failing. Provision
        # CUDA with `ss install --gpu`, or force it with a rebuild once nvcc is on
        # PATH; pass --no-cuda to silence this.
        cuda = "OFF"
        console.warn("No usable CUDA toolkit detected (nvcc missing); building the "
                     "CPU/OpenCL path. Run `ss install` to provision the GPU stack, "
                     "or pass --no-cuda to silence this.")

    if cfg.is_windows and build_type in (BuildType.debug, BuildType.relwithdebinfo):
        console.warn(f"--type {build_type.value} enables TSan, which is untested "
                     "with icx-cl on Windows.")

    env = load_build_env(cfg, build_dir)

    if _needs_configure(build_dir, cfg.generator, dist, root):
        console.info(f"Configuring CMake... (type={build_type.value}, "
                     f"distributed={dist}, cuda={cuda})")
        if build_dir.is_dir():
            shutil.rmtree(build_dir, ignore_errors=True)
        if cfg.is_windows:
            if (rc := _check_windows_compiler(cfg, env)) != 0:
                return rc
            args = _configure_args_windows(cfg, build_dir, cmake_build_type, dist, asan, tests)
        else:
            resolved = _resolve_compiler(cfg, force, env, asan=asan)
            if resolved is None:
                return 1
            cc, cxx = resolved
            args = _configure_args_linux(cfg, build_dir, cmake_build_type,
                                         cuda, dist, asan, tests, cc, cxx)
        rc = _run(args, env, cwd=root)
        if rc != 0:
            console.error("CMake configure failed.")
            return rc
        build_dir.mkdir(parents=True, exist_ok=True)
        (build_dir / STAMP_NAME).write_text(dist)

    console.info("Building...")
    rc = _run([_cmake(cfg), "--build", str(build_dir), "--config", cmake_build_type],
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
        console.error(f"{build_dir.name}/ not found. Run `sr build` first.")
        return 1
    if _stale_build_tree(build_dir, root):
        console.error(
            f"{build_dir.name}/ was configured for a different source path "
            "(e.g. a Docker volume build reused on this host).\n"
            "  - run `sr build` to reconfigure it cleanly first.")
        return 1
    if distributed:
        console.info("Note: distributed tests only exist if built with --distributed.")

    env = load_build_env(cfg, build_dir)
    # ctest spawns the test binaries, so it hits the same run-time UR mismatch as
    # `run`; neutralize setvars' LD_LIBRARY_PATH for the oneAPI build.
    _neutralize_runtime_ld(cfg, env)
    cmd = [_ctest(cfg), "--test-dir", str(build_dir), "--output-on-failure"]
    if suite != Suite.all:
        cmd += ["-L", _SUITE_LABEL_REGEX[suite]]
    if filter:
        # gtest_discover_tests registers tests as "Suite.Case"; ctest -R filters
        # those names directly (a richer alternative to --gtest_filter).
        cmd += ["-R", filter]
    if repeat > 0:
        # Re-run each selected test until it fails or N runs pass — the native
        # ctest way to flush out flakiness.
        cmd += ["--repeat", f"until-fail:{repeat}"]
        console.info(f"Repeating each test up to {repeat}x (stop on first failure).")
    return _run_drained(cmd, env, cwd=build_dir)


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
    # CUDA_LAUNCH_BLOCKING=1 serializes every kernel and silently kills the
    # async/overlap tests; clear any inherited value so a stray one can't poison us.
    env.pop("CUDA_LAUNCH_BLOCKING", None)
    _neutralize_runtime_ld(cfg, env)
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

    # Resolve doxygen from config first, then PATH. Doxygen is an external tool we
    # do not vendor, so when it is genuinely missing give the install command for
    # the platform rather than the generic "set cmake_exe" resolver message.
    doxy = cfg.expand(cfg.doxygen_exe) if cfg.doxygen_exe else "doxygen"
    if _resolve_exe(doxy, env) == doxy and not Path(doxy).is_file():
        console.error(
            "Doxygen is not installed or not on PATH.\n"
            "  - Windows: winget install DimitriVanHeesch.Doxygen"
            "  (or: choco install doxygen.install)\n"
            "  - Linux:   apt-get install -y doxygen graphviz\n"
            "  - macOS:   brew install doxygen graphviz\n"
            "  - or set doxygen_exe in config.local.toml to an existing doxygen binary.")
        return 1

    (root / "docs").mkdir(exist_ok=True)
    return _run([doxy, "Doxyfile"], env, cwd=root)
