# cli

The `sr` command line interface. `sushiruntime/` parses commands and holds this repository's
build policy; the generic machinery (config loading, console, process runner, CMake driver)
comes from sushicore, which ships in the SushiStack repository. `tests/` is the pytest suite:
`cd cli && python -m pytest -q`.

## sushiruntime/config.py

Configuration is layered, lowest precedence first: built-in defaults, `config.toml`,
`config.local.toml`, then `SR_*` environment variables. The active platform's
`[tool.<platform>]` table is merged over the common `[tool]` table, so one file describes both
Linux and Windows.

The config plumbing is domain-agnostic and shared by every Sushi CLI. The generic build-tool
schema (cmake, ninja and vcpkg paths) and the skeleton that loads the layers and writes the
`[tool]` section live in sushicore. This repository adds only the SYCL-specific fields.

`PROFILE` is how SushiRuntime describes itself to the shared machinery. Unlike the heads of
the stack it names no siblings: it is the bottom, and the owner of the toolchain bundle the
others borrow. It carries six overrides of its own, for the toolchain selection no other
module makes. SushiRuntime is the module that selects a SYCL toolchain, so it is the one that
cares about the oneAPI, Intel and CUDA environment variables listed in `env_of_interest`.

`TOOLCHAINS` lists the SYCL toolchains a user can select and must match `SR_SYCL_TOOLCHAIN` in
`CMakeLists.txt`: intel-llvm is primary, adaptivecpp secondary, oneapi supported.

`INTEL_LLVM_BUNDLE_DIRS` names the directories an intel/llvm bundle may sit under, in
preference order. `hub install` writes `llvm-sycl`, the bundle SushiEngine configures with and
the one SushiStack builds the GPU adapters for (SushiStack's
`docs/design/GPU_BACKEND_PROVISIONING.md`, section 4), so it comes first. `llvm-sycl-nightly`
is an older manual install. A `llvm_root` written into `config.local.toml` wins over both.

`TOOLCHAIN_COMPILERS` gives the default compiler pair per toolchain, so `sr toolchain <name>`
is enough to switch. acpp is C++-only, so the C slot of adaptivecpp holds a plain C compiler.
The project builds CXX only, which leaves `cc` unused in practice, but the value stays valid.

In `Config`, `llvm_root` is the root of the intel/llvm bundle (it holds `bin/clang++`) for the
intel-llvm toolchain, and `acpp_exe` is the AdaptiveCpp compiler for the adaptivecpp
toolchain. On Windows these are how the non-oneAPI toolchains provide a SYCL compiler;
`sr setup` and `hub install` discover them and write them to `config.local.toml`.

## sushiruntime/console.py

The module is a thin wrapper around sushicore. The theme, icon and renderer logic and its
`[cli]` config schema live there, shared with every other Sushi CLI; sushicore's README says
how to change colours.

The console is built lazily, by `sushicore.cli_console.LazyConsole`. Constructing it needs
this repository's `cli/` directory, and locating that fails outside a checkout. Building at
import time would make every `sr` invocation abort with "Not inside a SushiRuntime project",
`sr --help` included, which Typer can serve with no project at all. Module attributes resolve
on first access through PEP 562, so importing the module never touches the filesystem or the
project layout.

## sushiruntime/env.py

A parent process cannot `source setvars.sh` or `call vcvars64.bat` and inherit the result. The
CLI runs those scripts in a child shell, dumps the resulting environment, and reuses that
dictionary for every cmake and ctest subprocess it spawns. Sourcing oneAPI's setvars is slow,
so the snapshot is cached on disk, keyed by the configuration that produced it. An unchanged
config skips the shell.

The cache key includes the toolchain, because the toolchain gates whether oneAPI's setvars is
sourced: switching between intel-llvm and oneapi has to rebuild the snapshot. The intel-llvm
and adaptivecpp toolchains inject their own bin directories into the snapshot's `PATH`, so a
change to either invalidates the cache. The key holds the resolved `llvm_root`, not the raw
setting. When the setting is empty the bundle is probed for, and swapping which bundle is
present has to invalidate the snapshot just as pinning a different one would.

On Windows every intel-llvm environment gets compiler-rt's `lib/clang/*/lib/windows` directory
on `PATH`. compiler-rt ships AddressSanitizer's runtime as a DLL beside its import library, so
an instrumented binary cannot start unless that directory is on `PATH`. The directory holds
nothing but compiler-rt, so it is inert for a build without a sanitizer. Adding it only for
`--asan` would leave `sr run` on an ASan binary failing with a bare "DLL not found".

On Windows with vcpkg the port's `bin` directory also goes on `PATH`. vcpkg's applocal
deployment copies a target's direct dependents next to it. A library pulled in transitively,
such as hwloc, reached through `sushiruntime.dll` and not by the test executable, can be
missing from a fresh build tree, and the binary then fails to start with a bare "DLL not
found". With the port directory on `PATH` any freshly built binary runs, whatever was copied
where. This does not replace deployment for a shipped build.

On Linux `_snapshot_linux` sources oneAPI's setvars only for the oneapi toolchain. Sourcing it
for intel-llvm or adaptivecpp prepends oneAPI's lib directory to `LD_LIBRARY_PATH`. At run
time the binary then loads oneAPI's `libsycl` and `libur_loader` while the CUDA backend still
resolves the llvm-sycl `libur_adapter_cuda`. That Unified Runtime version mismatch segfaults
during SYCL device discovery, in `urVirtualMemMap`. The intel-llvm and adaptivecpp toolchains
ship on `PATH` and need no setvars, so returning `None` keeps their environment clean.

## sushiruntime/services/diag.py

Read-only troubleshooting. When a build picks the "wrong" compiler or a path looks off,
`sr config` answers what the CLI resolved and where each value came from (default,
`config.toml` or an environment override), and `sr env` answers which environment cmake and
ctest run under. Both are `sushicore.diag.Diagnostics`. What this file supplies is
SushiRuntime's own: the sanitizer build tree, which no other module in the stack has, and
which is why `sr env` takes a flag the others do not.

## sushiruntime/services/discovery.py

The walk over the build tree is shared; see `sushicore.discovery.ExecutableIndex`. This module
contributes only which directories to prune, and it does not spell them out either. They are
the sibling checkouts `PROFILE` already names, because a module that builds a dependency
in-tree must not offer that dependency's executables as its own.

## sushiruntime/services/packaging.py

Packaging is the release action: install to a prefix, then archive that prefix. It does not
check whether the result works. That question has a pass or fail answer and so belongs with
the tests, as `sr test --suite package`, which installs to the build tree's own stage
directory and then builds and runs a consumer against it with nothing else on `PATH`.

## sushiruntime/services/project.py

### The ASan tree

AddressSanitizer builds go to an isolated `build-asan` tree, kept apart from the CUDA `build/`
so that neither clobbers the other. ASan forces CUDA off, because the CUDA Unified Runtime
adapter's `urVirtualMemReserve` collides with the virtual address range ASan reserves. The
tree stays a Release build so that the automatic TSan block for Debug and RelWithDebInfo in
`CMakeLists.txt` never fires; TSan and ASan cannot coexist. For the same reason `sr run --asan`
pins the run to the OpenCL CPU device, so the SYCL runtime never loads the CUDA adapter.

### ASAN_FLAGS_WINDOWS

Windows needs three adjustments the Linux driver does not.

`-Xarch_host`. `-fsycl` compiles every translation unit twice, once for the host and once for
the SPIR-V device, and AddressSanitizer exists only for the host. Passed unqualified, the
Windows driver reports `-Woption-ignored` for the device pass, and the tree's `-Werror` turns
that into a failure on the first source file. Scoping the flag to the host pass leaves no
diagnostic to suppress. Suppressing it would not have worked anyway, since the target-level
`-Werror` lands after `CMAKE_CXX_FLAGS` on the command line and promotes everything again.

`_DISABLE_STL_ANNOTATION`. MSVC's STL links `stl_asan.lib` for its container annotations, and
that library ships with the Visual Studio "C++ AddressSanitizer" component, not with the
compiler, so a stock Build Tools install does not have it. Turning the annotations off keeps
the lane buildable there. The cost is container-overflow detection inside the annotated
containers; heap overflow, stack overflow, use-after-free and double-free reporting are
unaffected.

The umbrella macro is used, not the per-container ones. Naming the containers one by one
(`_DISABLE_STRING_ANNOTATION`, `_DISABLE_VECTOR_ANNOTATION`) went stale the moment MSVC's STL
learned to annotate one more: 14.51 added `std::optional`, whose annotation kept emitting the
`stl_asan.lib` autolink and broke the link with `could not open 'stl_asan.lib'`.
`_DISABLE_STL_ANNOTATION` is the documented switch that covers all of them (see
`__msvc_sanitizer_annotate_container.hpp`) and will cover whatever is added next.

`-fsanitize-address-use-after-return=never`. ASan's use-after-return detection moves locals
into a heap-allocated "fake stack", and on the MSVC ABI that code generation produces EH state
tables the CRT unwinder cannot read. Any throw issued from inside a catch handler, a bare
`throw;` rethrow or a fresh throw alike, then faults in `__FrameHandler3::GetUnwindTryBlock` on
a null read before it reaches any handler. The fault was reduced to a 20-line program with no
SushiRuntime code in it, so the cause is the toolchain, not the tree. The same program is
correct at `-O0` for a rethrow and still dies for a throw-copy, which is why the flag is the
fix and lowering the optimisation level is not.

The setting costs nothing here. The fake stack is what the flag removes, and its reporting is
off unless `ASAN_OPTIONS` carries `detect_stack_use_after_return=1`, which this CLI has never
set (`_inject_sanitizer_options` in `env.py` handles `LSAN_OPTIONS` only). Heap overflow,
use-after-free, double-free and global overflow reporting are all unaffected; this was
verified against a deliberate heap-buffer-overflow with the flag on.

These adjustments are Windows only. The Itanium ABI unwinder Linux uses is not affected, and
`ASAN_FLAGS` keeps full use-after-return instrumentation there.

### ASAN_C_FLAGS_WINDOWS

The C driver has no device pass, so it takes the plain form: `-Xarch_host` means something
only to an offloading compilation.

The string is deliberately not passed to CMake as `CMAKE_C_FLAGS`. Both projects declare
`project(... LANGUAGES CXX)` and nothing calls `enable_language(C)`, so C is never enabled,
the C compiler probe never runs, and CMake reported `CMAKE_C_COMPILER` and `CMAKE_C_FLAGS` as
"manually-specified variables not used" on every configure. The string is kept because it
feeds the ASan configure stamp, and because it is the right value to pass on the day something
does enable C. Finding the use-after-return tuning again would be the expensive part.

### ASAN_BENCHMARKS

The benchmark binary installs a global `operator new` and `operator delete` replacement to
count allocations (`tests/benchmark/performance/test_recompose_churn_perf.cpp`), which is the
same thing AddressSanitizer replaces. On Windows the two collide as a duplicate-symbol link
error. Excluding the suite is right in any case: ASan's redzones and quarantine make its
timings and allocation counts meaningless, so an ASan benchmark run would report numbers
nobody should read. The sanitizer lane exists for correctness; the benchmark lane runs
uninstrumented.

### Suites

The fine-grained suites are CTest labels, assigned in `tests/CMakeLists.txt` from the GTest
suite-name prefix convention. Each suite maps to a label-selection regex for `ctest -L`. A
single category anchors an exact match. The two umbrellas, functional and benchmark, are an
alternation over their sub-labels and not a label of their own, because a baked-in umbrella
label does not survive `gtest_discover_tests` on current CMake (see `tests/CMakeLists.txt`).
`all` selects everything and passes no `-L`.

### Configure

A build tree with no stamp, from before the stamp existed or from a hand-run cmake, is
reconfigured once so that the tree is known to match the requested configuration.

GPU backend selection is delegated to CMake, `cmake/gpu/DetectGpu`, on every platform. It
resolves the vendor: nvidia to cuda, amd to rocm, intel, or cpu. "auto" detects which vendors
have both a toolkit and hardware and targets them. Where nothing answers it builds the
portable CPU/OpenCL path, so a missing CUDA toolkit does not hard-fail with "cannot find
libdevice". "cpu" forces that path, and ASan and `--no-cuda` select it. DetectGpu is the single
source of truth; the CLI does not probe nvcc itself.

Windows used to be frozen to "cpu" on the grounds that it has no nvptx toolchain. That stopped
being true: the host compiles `-fsycl-targets=spir64,nvidia_gpu_sm_89` with the bundled
intel/llvm and the installed CUDA toolkit, and `hub install` offers that toolkit as a
component. The freeze meant provisioning a toolkit the build could never use, with no way to
override it. It also made the two CLIs in the workspace disagree about the same machine, since
SushiEngine passed no backend at all and got "auto". Detection is one question with one
answer, and it is asked in one place. `--no-cuda` now has an effect on Windows, where it used
to print that it had none.

On Windows the compiler is toolchain-specific: intel-llvm uses clang++, oneapi uses icx-cl,
adaptivecpp uses acpp. `-DSR_SYCL_TOOLCHAIN` is passed explicitly so that `CMakeLists.txt`
does not force it from the compiler name (icx-cl implying oneapi, for example), which would
diverge from the environment the CLI snapshots. AdaptiveCpp injects its own SYCL flags through
`add_sycl_to_target`; `-fsycl` means something only to the clang-based intel-llvm and oneapi
drivers.

### Test and run environment

`ctest` spawns the test binaries, so it hits the same run-time Unified Runtime mismatch as
`sr run`, and both neutralize setvars' `LD_LIBRARY_PATH` for the oneAPI build. Both also clear
an inherited `CUDA_LAUNCH_BLOCKING`: a stray `CUDA_LAUNCH_BLOCKING=1` serializes every kernel
and silently breaks the async overlap tests.

## sushiruntime/services/select.py

When the chosen toolchain is not installed, `sr toolchain` names the exact profile to install.
That message is what the user asked the command for.

## tests/conftest.py

`sushiruntime` imports sushicore, the shared core underneath every Sushi CLI, at module load,
so the suite cannot be collected without it.

The tests used to run against a hand-written stub of that package. The stub was a liability
twice over. It had to grow every time sushicore did, and a test could end up pinning the
imitation and not the contract; one of them asserted on an attribute only the fake had.
sushicore now ships inside the SushiStack repository, so the real package is reachable,
installed in the environment or sitting next door, and the tests use it.

## tests/test_console_lazy.py

Regression tests for the lazy console. `console.py` used to build the console at import time,
which called `config_dir()`, then `find_project_root()`, and raised `SystemExit` outside a
checkout. Because `cli.py` imports console transitively, every `sr` invocation died with "Not
inside a SushiRuntime project", including `sr --help`, which Typer can answer with no project.

Four of the five CLIs fixed this independently and sushiengine's copy never did, so
`se --help` outside a checkout failed for as long as the copies existed. The lifecycle now
lives in `sushicore.cli_console.LazyConsole`. These tests pin that this CLI is wired to it,
which is the part a shared implementation cannot check on its own.

## tests/test_docker.py

`sr docker` used to hand-roll its own subprocess runner and did not go through
`sushicore.proc.Runner`. That copy printed `" ".join(cmd)`, which quotes an argument with a
space wrongly. It passed the bare "docker" name as `argv[0]` where a resolved path belonged.
And it let a missing docker binary raise an unhandled `FileNotFoundError` in place of the
shared not-found guidance.

Routing it through `Runner` fixes all three but changes real behaviour that had no coverage.
The tests pin what `docker.build` and `docker.run` hand to it: the argv for every flag
combination, and that the interactive `docker run -it` invocation still lets `Runner.run`
inherit this process's stdio exactly as `subprocess.run(cmd, cwd=str(cwd))` did.

## tests/test_gpu_backend_delegation.py

Detection is one question, asked in one place, on every platform.

Windows was frozen to `-DSR_GPU_BACKEND=cpu` on the grounds that the host has no nvptx
toolchain. That stopped being true: the workspace's intel/llvm bundle compiles
`-fsycl-targets=spir64,nvidia_gpu_sm_89` against the installed CUDA toolkit, and `hub install`
offers that toolkit as a component. The freeze meant provisioning a toolkit the build could
never use, with no flag to override it.

Worse, it made the two CLIs disagree about the same machine. SushiEngine passed no backend at
all and so got `cmake/gpu/DetectGpu`'s "auto", while SushiRuntime forced "cpu": one checkout,
one host, two answers about which hardware to target.

Both CLIs delegate to DetectGpu now. The tests pin the delegation and not the answer, because
the answer is the part that is supposed to depend on the machine.

## tests/test_gpu_backend_resolution.py

GPU backend selection resolves a set of vendors and drops the ones with no hardware.

`cmake/gpu/DetectGpu.cmake` used to elect one vendor: an if/elseif chain that looked for nvcc
and, having found it, never looked for ROCm. A machine holding both cards silently built for
one of them. That is the exact guess `gpu/Cuda.cmake` refuses to make between two NVIDIA
cards, made one level up where nothing was watching. Selection is a set now, and the tests pin
both halves of that: every requested vendor is targeted, and a vendor whose hardware never
answers drops back out and does not poison the build with a triple it cannot fill.

The probes are steered, not mocked, and the host is cut out of the answer. Every
`find_program` in `gpu/` is disarmed by turning off the system search paths
(`CMAKE_FIND_USE_SYSTEM_ENVIRONMENT_PATH` and `CMAKE_FIND_USE_CMAKE_SYSTEM_PATH`), so a probe
succeeds only where a test preset its cache variable. Otherwise `find_program(nvcc)` and
`find_program(nvidia-smi)` answer from whatever the host has installed, the machine running
the suite decides the result, and a developer with a CUDA toolkit installed sees different
selections than CI does; the decline tests would pass or fail depending on whose machine ran
them. Presetting `SR_NVCC` and `SR_HIPCC` is what `find_program` already honours, and
presetting `SR_CUDA_ARCH` and `SR_AMD_ARCH` is the documented way to state an architecture
without probing for one, so a test can put a vendor in either state without owning a card.

## tests/test_gpu_probe_paths.py

The file covers two ways the GPU probes read the machine wrongly, both silent. A bad regex
accepted a malformed line and produced a plausible-looking architecture. An unexpanded glob
found no toolkit and reported the machine as CPU-only. Both then flowed into a build that
compiled and ran, which is why they needed a test and not a fix alone.

`sushiruntime_gpu_versioned_prefixes` is exercised directly against fabricated install trees.
The regex is exercised against the same construct `gpu/Cuda.cmake` uses, not by driving
nvidia-smi, because the inputs that matter are the ones no real driver on the machine emits.

The file has three groups of tests: the compute-capability regex, versioned toolkit prefixes,
and publishing the resolved architecture. In the third, `_publish` reports the cache value
under the key `PUBLISHED=`. The key is not `ARCH=`, because `resolve_arch`'s own diagnostic
ends with it ("-> SR_CUDA_ARCH=89"). Reading the first matching line found that message in
place of the published value and reported the number that had just been thrown away.

## tests/test_toolchain_selection.py

Switching the SYCL toolchain has to switch it, from both directions. Two independent defects
made `sr toolchain <name>` a no-op, and neither said anything.

CMake's side inferred the toolchain from the compiler's file name under a guard that claimed
to check "the caller left the default" but only checked that a compiler had been pinned. An
explicit `-DSR_SYCL_TOOLCHAIN` was overwritten, and every `STREQUAL` dispatch downstream took
the branch for a toolchain nobody asked for.

The CLI's side keyed its configure stamp on the build type, distribution, GPU backend and test
flag, but not on the toolchain. So `sr build`, `sr toolchain adaptivecpp`, `sr build` matched
the old stamp, skipped configure and reused the previous tree. The cache in that tree still
read the old toolchain, so nothing anywhere disagreed.

Both halves had to be fixed for the command to work, which is why both are pinned.
