"""Project build / test / run / clean / doxygen logic.

This replaces the project/*.sh and project/*.bat scripts; the cmake / ctest
invocations are issued directly via subprocess with the snapshotted build env.
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

# Isolated ASan tree. Kept separate from the CUDA `build/` so it is never
# clobbered: ASan forces CUDA OFF (the CUDA UR adapter's urVirtualMemReserve
# collides with ASan's reserved VA) and stays a Release build (so CMakeLists'
# Debug|RelWithDebInfo auto-TSan block never fires -- TSan and ASan can't coexist).
ASAN_FLAGS = "-fsanitize=address -fno-omit-frame-pointer -g -O1"

# Windows needs two adjustments the Linux driver does not.
#
# -Xarch_host: `-fsycl` compiles every translation unit twice, once for the host
# and once for the SPIR-V device, and AddressSanitizer exists only for the host.
# Passed unqualified, the Windows driver reports `-Woption-ignored` for the device
# pass and the tree's `-Werror` turns that into a failure on the first source
# file. Scoping the flag to the host pass leaves no diagnostic to suppress —
# suppressing it would not have worked anyway, since the target-level `-Werror`
# lands *after* CMAKE_CXX_FLAGS on the command line and re-promotes everything.
#
# _DISABLE_STL_ANNOTATION: MSVC's STL links `stl_asan.lib` for its container
# annotations, and that library ships with the Visual Studio "C++ AddressSanitizer"
# component rather than with the compiler, so a stock Build Tools install does not
# have it. Turning the annotations off keeps the lane buildable there. The cost is
# container-overflow detection inside the annotated containers; heap overflow,
# stack overflow, use-after-free and double-free reporting are unaffected.
#
# The umbrella macro, not the per-container ones. Naming the containers
# individually (_DISABLE_STRING_ANNOTATION, _DISABLE_VECTOR_ANNOTATION) went stale
# the moment MSVC's STL learned to annotate one more: 14.51 added std::optional,
# whose annotation kept emitting the `stl_asan.lib` autolink and broke the link
# with `could not open 'stl_asan.lib'`. _DISABLE_STL_ANNOTATION is the documented
# switch that covers all of them (see __msvc_sanitizer_annotate_container.hpp) and
# will keep covering whatever is added next.
#
# -fsanitize-address-use-after-return=never: ASan's use-after-return detection
# moves locals into a heap-allocated "fake stack", and on the MSVC ABI that
# codegen produces EH state tables the CRT unwinder cannot read. Any throw issued
# from inside a catch handler — a bare `throw;` rethrow or a fresh throw, it makes
# no difference — then faults in `__FrameHandler3::GetUnwindTryBlock` on a null
# read, before reaching any handler. Reduced to a 20-line program with no
# SushiRuntime code involved, so this is the toolchain, not the tree; the same
# program is correct at -O0 for a rethrow and still dies for a throw-copy, which
# is why the flag is the fix and lowering the optimisation level is not.
#
# The setting costs nothing here: the fake stack is what the flag removes, and its
# reporting is off unless ASAN_OPTIONS carries detect_stack_use_after_return=1,
# which this CLI has never set (see env.py's _inject_sanitizer_options, which
# handles LSAN_OPTIONS only). Heap overflow, use-after-free, double-free and
# global overflow reporting are all unaffected — verified against a deliberate
# heap-buffer-overflow with the flag on.
#
# Windows only. The Itanium ABI unwinder Linux uses is not affected, and ASAN_FLAGS
# keeps full use-after-return instrumentation there.
ASAN_FLAGS_WINDOWS = (
    "-Xarch_host -fsanitize=address -fsanitize-address-use-after-return=never "
    "-fno-omit-frame-pointer -g -O1 -D_DISABLE_STL_ANNOTATION=1")

# The C driver has no device pass, so it takes the plain form: `-Xarch_host` is
# only meaningful to an offloading compilation.
#
# Not passed to CMake as CMAKE_C_FLAGS, and deliberately so. Both projects declare
# `project(... LANGUAGES CXX)` and nothing calls enable_language(C), so C is never
# enabled, the C compiler probe never runs, and CMake reported CMAKE_C_COMPILER /
# CMAKE_C_FLAGS as "manually-specified variables not used" on every configure. The
# string is kept because it feeds the ASan configure stamp below, and because it is
# the right value to pass on the day something does enable C -- rediscovering the
# use-after-return tuning would be the expensive part.
ASAN_C_FLAGS_WINDOWS = ("-fsanitize=address -fsanitize-address-use-after-return=never "
                        "-fno-omit-frame-pointer -g -O1")

# The benchmark binary installs a global operator new/delete replacement to count
# allocations (tests/benchmark/performance/test_recompose_churn_perf.cpp), which
# is the same thing AddressSanitizer replaces — on Windows the two collide as a
# duplicate-symbol link error. Excluding the suite is the right call regardless:
# ASan's redzones and quarantine make its timings and allocation counts
# meaningless, so an ASan benchmark run would report numbers nobody should read.
# The sanitizer lane exists for correctness; the benchmark lane runs uninstrumented.
ASAN_BENCHMARKS = "OFF"
STAMP_NAME = ".sushiruntime_dist"


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
    package = "package"         # installs, then builds and runs a consumer
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
    Suite.package: "^package$",
}


_CMAKE_BUILD_TYPE = {
    BuildType.release: "Release",
    BuildType.debug: "Debug",
    BuildType.relwithdebinfo: "RelWithDebInfo",
    BuildType.asan: "Release",  # ASan stays Release; flags injected manually
}


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
    # No stamp (a pre-upgrade tree or a hand-run cmake): reconfigure once so the
    # tree is known to match the requested configuration.
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
        # GPU-agnostic: CMake's cmake/gpu/DetectGpu resolves the vendor
        # (nvidia->cuda / amd->rocm / intel / cpu). "auto" lets it detect; "cpu"
        # forces the portable OpenCL path (--no-cuda / ASan). This is the single
        # source of truth — the CLI no longer probes nvcc itself.
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
        f"  - install a bundle that carries compiler-rt with `ss install`, or point "
        f"llvm_root in cli/config.local.toml at one that does.")
    return 1


def _configure_args_windows(cfg: Config, build_dir: Path, build_type: str,
                            gpu_backend: str, dist: str, asan: bool,
                            tests: bool) -> list[str]:
    vcpkg = cfg.expand(cfg.vcpkg_root)
    triplet = cfg.vcpkg_triplet
    asan_flags = f" {ASAN_FLAGS_WINDOWS}" if asan else ""

    # The compiler is toolchain-specific: intel-llvm -> clang++, oneapi -> icx-cl,
    # adaptivecpp -> acpp. Pass -DSR_SYCL_TOOLCHAIN explicitly so CMakeLists does
    # not auto-force it from the compiler name (e.g. icx-cl => oneapi), which would
    # diverge from the environment the CLI actually snapshots.
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
    # AdaptiveCpp (acpp) injects its own SYCL flags via add_sycl_to_target; the
    # -fsycl flag is only meaningful to the clang-based intel-llvm/oneapi drivers.
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

    # GPU backend selection is delegated to CMake (cmake/gpu/DetectGpu), on every
    # platform. "auto" detects which vendors have both a toolkit and hardware and
    # targets them; where nothing answers it builds the portable CPU/OpenCL path,
    # so a missing CUDA toolkit does not hard-fail with "cannot find libdevice".
    #
    # Windows used to be frozen to "cpu" here on the grounds that it has no nvptx
    # toolchain. That stopped being true: this host compiles
    # -fsycl-targets=spir64,nvidia_gpu_sm_89 with the bundled intel/llvm and the
    # installed CUDA toolkit, and `ss install` offers that toolkit as a component --
    # so the freeze meant provisioning a toolkit the build could never use, with no
    # way to override it. It also made the two CLIs in this workspace disagree about
    # the same machine, since SushiEngine passed no backend at all and got "auto".
    # Detection is one question with one answer; it is asked in one place now.
    #
    # ASan and --no-cuda still force CPU, and --no-cuda now means something on
    # Windows rather than printing that it does not.
    if no_cuda or asan:
        gpu_backend = "cpu"
    else:
        gpu_backend = "auto"

    if cfg.is_windows and build_type in (BuildType.debug, BuildType.relwithdebinfo):
        console.warn(f"--type {build_type.value} enables TSan, which is untested "
                     "with icx-cl on Windows.")

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
    # ctest spawns the test binaries, so it hits the same run-time UR mismatch as
    # `run`; neutralize setvars' LD_LIBRARY_PATH for the oneAPI build.
    _neutralize_runtime_ld(cfg, env)
    # Same reason as `run`: a stray CUDA_LAUNCH_BLOCKING=1 serializes every kernel
    # and silently breaks the async/overlap tests, so clear any inherited value.
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
    return _RUNNER.run([str(exe), *forwarded], root, env)


def clean_tree(root: Path, asan: bool = False) -> None:
    _DRIVER.clean_tree(_build_dir(root, asan))


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
    if _RUNNER.resolve_exe(doxy, env) == doxy and not Path(doxy).is_file():
        console.error("Doxygen is not installed or not on PATH.\n" + _DOXYGEN_HINT)
        return 1

    (root / "docs").mkdir(exist_ok=True)
    return _RUNNER.run([doxy, "Doxyfile"], root, env)
