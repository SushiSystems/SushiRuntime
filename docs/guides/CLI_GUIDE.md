# The SushiRuntime CLI

SushiRuntime ships with a small command-line tool that drives everything you do
day to day: building the C++ library, running its tests, launching the example
programs, and managing the Docker image. It is a thin wrapper around CMake,
CTest, and Docker that reads your machine-specific toolchain paths from a config
file so you don't have to retype long compiler flags.

This guide explains every command in plain English. If you just want to get the
project compiling and running for the first time, start with
[INTRODUCTION.md](../getting_started/INTRODUCTION.md) instead — it walks you through setup and your
first program.

## Installing the CLI

The CLI is a Python package that lives in the `cli/` folder. Install it once and
it puts two commands on your `PATH`:

- **`sr`** — short name.
- **`sushiruntime`** — long name.

They are identical; use whichever you prefer. Every example below uses `sr`.

To install:

```bash
hub install-cli sushiruntime      # always an editable install
```

This installs through **pipx**, into an isolated environment, on every platform.
You can also install the package directly if you'd rather manage it yourself:

```bash
pipx install ./cli               # or:  pip install ./cli   (inside a venv/conda env)
```

To remove it later:

```bash
pipx uninstall sushiruntime-cli
```

### How the CLI finds your compiler

The CLI reads toolchain paths from two files:

- **`cli/config.toml`** — committed, shared defaults (the same for everyone).
- **`cli/config.local.toml`** — your machine's absolute paths (oneAPI / Visual
  Studio roots, vcpkg location, compiler executables). This file is gitignored,
  so your personal paths never get committed.

`sr setup` writes `config.local.toml` for you. `hub install` does the same for
every checkout in a SushiStack workspace at once. If you're setting paths by
hand, copy the commented Windows block from `config.toml` as a starting
point — it uses the same `[tool.<platform>]` section layout.

If you're ever unsure what the CLI is actually using, run `sr config` to
see the final resolved values and where each one came from.

## Command overview

Commands are grouped under these subcommands:

| Group        | What it's for                                               |
|--------------|-------------------------------------------------------------|
| `sr toolchain` | Choose the SYCL toolchain (intel-llvm / adaptivecpp / oneapi) |
| `sr build` / `test` / `run` / `clean` / `docs` | Build, test, run, and document the C++ project |
| `sr package` | Install the built tree to a prefix and archive it with CPack |
| `sr container` | Build and run the development container                   |
| `sr config`  | Show the resolved configuration                             |
| `sr env`     | Show the environment your builds run under                  |
| `sr setup`   | Install what this checkout needs to build                   |
| `sr doctor`  | Report whether this machine is ready to build               |
| `sr link` / `unlink` | Register this checkout in a SushiStack workspace, or remove it |

Run any command with `--help` to see its options. Running `sr container` with no
subcommand prints that group's help.

`sr --version` prints the CLI's distribution name and version. `sr --describe` prints
every command and its options as JSON, for a tool that drives the CLI.

Two commands were renamed: `sr doxygen` is now `sr docs`, and `sr docker` is now
`sr container`. The old words still run until 2027-03-25 and print one line that names
the new spelling; set `SR_NO_DEPRECATION_NOTICE=1` to silence it. They are listed in
neither `--help` nor `--describe`.

`sr --help` groups the commands under Toolchain, Project, Diagnostics and Container,
and each command's own help ends with examples. On a colour terminal the root screen
shows the logo. On a dark terminal add `[cli]` and `background = "dark"` to
`cli/config.local.toml` to give it a glow; `COLORFGBG` is read when the terminal sets
it, and Windows Terminal does not.

## `sr toolchain` — choose the SYCL toolchain

SushiRuntime builds against one of three SYCL toolchains. `sr toolchain` records
your choice in `cli/config.local.toml`, and every later `sr build`
passes it to CMake as `-DSR_SYCL_TOOLCHAIN` and derives the matching compiler.

```bash
sr toolchain                  # list the toolchains and show which one is active
sr toolchain intel-llvm       # intel/llvm nightly (clang++ -fsycl) — primary (default)
sr toolchain adaptivecpp      # AdaptiveCpp (acpp) — secondary, vendor-neutral
sr toolchain oneapi           # Intel oneAPI — icpx/icx-cl, supported, for VTune/Advisor profiling
```

After switching, reconfigure with `sr build --clean` so CMake picks up
the new toolchain. To override per-invocation without persisting, set the
`SR_SYCL_TOOLCHAIN` environment variable.

## `sr build` / `test` / `run` / `clean` / `docs`

This is the group you'll use most.

### `sr build`

Configures (if needed) and builds the library and its test binaries.

```bash
sr build                  # Release build (the default)
sr build --type debug     # Debug build
sr build --type asan      # AddressSanitizer — built in a separate build-asan/ tree, CUDA off
sr build --distributed    # also compile the optional master-worker layer
sr build --no-cuda        # build the portable CPU/OpenCL path instead of detecting accelerators
sr build --no-test        # skip compiling the test suite (SR_BUILD_TESTS=OFF)
sr build --clean          # delete the build tree first, then build from scratch
sr build --jobs 8         # compile with eight parallel jobs (-j 8)
sr build --force          # fall back to clang++/clang if the selected toolchain's
                          # compiler is missing, instead of stopping
```

The `--type` (`-t`) option accepts `release`, `debug`, `relwithdebinfo`, or
`asan`. The `asan` type is special: it builds into an isolated `build-asan/`
directory with CUDA disabled, so it never collides with your normal `build/`
tree. Flags can be combined, e.g. `sr build --type debug --clean`.

`--jobs N` (`-j N`) passes `--parallel N` to the compile step. Without it the
build tool picks the job count, and `CMAKE_BUILD_PARALLEL_LEVEL` applies if set.

The ASan tree also excludes the benchmark suite. That suite installs a global
`operator new`/`delete` replacement to count allocations, which is exactly what
AddressSanitizer replaces — the two collide as a duplicate-symbol link error on
Windows — and ASan's redzones and quarantine would make its timings meaningless
anyway. Run benchmarks from the ordinary `build/` tree.

**AddressSanitizer on Windows** needs an intel/llvm bundle that ships
compiler-rt. Bundles have not always carried it on Windows: if
`lib/clang/<version>/lib/windows/clang_rt.asan_dynamic-x86_64.lib` is missing
from your toolchain, the link fails and the fix is a newer bundle, not a newer
LLVM. Two further details are handled by the CLI and worth knowing about:

- `-fsanitize=address` is passed as `-Xarch_host -fsanitize=address`, because
  `-fsycl` compiles every translation unit for the host *and* for the SPIR-V
  device, and there is no device AddressSanitizer. Passed unqualified it draws a
  `-Woption-ignored` that `-Werror` turns into a build failure.
- MSVC's STL container annotations are disabled (`_DISABLE_STL_ANNOTATION`),
  because they require `stl_asan.lib`, which ships with the Visual Studio "C++
  AddressSanitizer" component rather than with the compiler. The cost is
  container-overflow detection inside the STL containers; heap overflow, stack
  overflow, use-after-free and double-free are unaffected.
- `-fsanitize-address-use-after-return=never` is passed, so use-after-return
  detection is off on Windows. With it on, a `throw` from inside a `catch`
  handler faults in the MSVC unwinder.

TSan has no Windows support upstream, so `--type debug`'s ThreadSanitizer is a
Linux-only lane (see `cmake/Sanitizers.cmake`).

The test suite is compiled by default. Pass `--no-test` to skip it
(`SR_BUILD_TESTS=OFF`) for a faster, library-only build — useful when you only
need the runtime to profile or to debug a kernel under `compute-sanitizer`.

### `sr test`

Runs the test suite through CTest. Tests are grouped by label, and you pick a
group with `--suite` (`-s`):

```bash
sr test                          # functional suite (the default)
sr test --suite functional       # unit + regression + integration (fast, run these often)
sr test --suite benchmark        # stress + performance (heavy)
sr test --suite all              # every test
sr test --suite package          # install to a prefix, then build and run a consumer
```

The full list of suite values is: `unit`, `regression`, `integration`,
`functional` (umbrella for those three), `stress`, `performance`, `benchmark`
(umbrella for the last two), `package`, and `all`.

The `package` suite checks the installed package from the outside. It is two
CTest tests joined by a fixture: `Package.InstallsToAPrefix` installs the built
tree into `build/stage`, and `Package.RunsAConsumerWithNothingButThePrefix`
configures, compiles, and runs `tests/package/` against that prefix with nothing
else on the path — so a missing export, a leaked private flag, or a header the
install rule forgot fails here rather than in someone else's project. It is
slower than the functional suite because it configures and compiles a second
CMake project.

Useful options:

```bash
sr test --suite functional --filter 'Steal.*'   # only tests whose name matches the regex
sr test --asan                                   # run the build-asan/ tree instead of build/
sr test --distributed                            # run against a distributed-enabled build
sr test --module scheduler                       # the tests of one module, whatever their kind
sr test --repeat 50                              # re-run each test up to 50x, stop on first failure
```

`--filter` (`-f`) is a `ctest -R` regex matched against `Suite.Case` test names.
`--repeat` (`-r`) is handy for hunting down flaky tests — it uses CTest's
`--repeat until-fail`.

For GoogleTest-level options that CTest doesn't expose (shuffling, break on
failure), run the binary directly with `sr run` and pass flags after
`--` (see below).

### `sr run`

Runs a built executable. With no target it runs the default from config
(`sr_functional_tests`).

```bash
sr run                              # run the default target
sr run sr_functional_tests          # run a specific binary by name
sr run --sort                        # interactively pick from the list of executables
```

The target name is matched exactly first, then by substring, so you usually only
need part of the name.

Anything after `--` is forwarded straight to the program. This is how you pass
GoogleTest flags:

```bash
sr run sr_benchmark_tests -- --gtest_filter='AllocatorBench.*'
sr run sr_functional_tests -- --gtest_shuffle --gtest_break_on_failure
```

For the distributed demo, `--distributed=master|worker` forwards a role to the
application's `main()`:

```bash
sr run sr_distributed_demo --distributed=worker --master=127.0.0.1:5555
sr run sr_distributed_demo --distributed=master --port=5555 --workers=1
```

Only `master` and `worker` are accepted. Any other role stops the command with
exit code 2 before the program starts. Add `--asan` to run binaries from the
`build-asan/` tree.

### `sr clean` and `sr docs`

```bash
sr clean       # remove the build/ and build-asan/ trees
sr docs        # build the fluent API reference into build/docs/api-site/html
sr docs bundle --release 1.0.0   # pack the manual and the reference for docs.sushisystems.io
```

`sr docs` runs Doxygen against `.config/doxygen/Doxyfile` (scoped to the
public `modules/surface/api/include/SushiRuntime/api` surface). Doxygen is an external tool, not
vendored: install it (`winget install DimitriVanHeesch.Doxygen` on Windows,
`apt-get install doxygen graphviz` on Linux, `brew install doxygen graphviz` on
macOS) or point `doxygen_exe` in `cli/config.local.toml` at an existing binary.
The command prints the right install line if it cannot find one. It writes the HTML site and,
beside it in `build/docs/api-site/xml`, the XML the bundle carries.

`sr docs bundle --release X.Y.Z` runs the same Doxygen build, then packs the pages
`docs/publish.toml` lists, the files they link to and that XML into
`build/docs/bundle/docs-bundle-X.Y.Z.tar.gz`, with the archive's SHA-256 in a `.sha256` file
beside it. `--out` names another folder. The release is three integers, without the `v` of the
tag. The command stops with the page and the line when a published page links to a file that
does not exist. The archive's layout and the keys of `docs/publish.toml` are in
`docs/reference/DOCS_BUNDLE.md` of the SushiCore repository.

## `sr package` — produce a redistributable package

Installs the already-built `build/` tree to a prefix and runs CPack over it to
produce the archive.

```bash
sr package                       # install to build/dist, then archive
sr package --prefix ../staging   # install somewhere else
sr package --prefix ../staging --force  # clear a directory sr did not stage
sr package --no-archive          # install only, skip CPack
```

The prefix is emptied before the install, so a file the current build no longer
produces cannot survive from a previous run into the archive. Because of that,
`sr package` chooses carefully what it empties:

- It never stages into a filesystem root, your home directory, the project root,
  any directory above the project root, or the `build/` tree itself. `--force`
  does not change this.
- A `--prefix` that does not exist yet, or is empty, is used as it is.
- A `--prefix` that holds files is emptied only when `sr package` staged it
  before. It knows by the `.sr-stage` file it writes into every stage tree.
  For any other directory with files in it, pass `--force`.
- `build/dist`, the default, is always emptied.

A refusal prints one error line and exits with code 1, and nothing is deleted.
The same happens when the old tree cannot be removed completely.

The build tree must already be configured — run `sr build` first. If the tree
has no `CPackConfig.cmake`, the archive step is skipped with a warning.

`sr package` produces the artifact; it does not check it. `sr test --suite
package` is the check.

## `sr container` — the development container

`.config/docker/Dockerfile` provides a complete SYCL toolchain (an intel/llvm nightly bundle,
the `intel-llvm` toolchain) plus the Intel OpenCL CPU runtime, so you can build
and run — including the CUDA backend — without installing a compiler on your host.

```bash
sr container build              # build the image (intel-llvm + adaptivecpp toolchains)
sr container build --no-cache   # rebuild every layer from scratch, ignoring Docker's cache
sr container build --oneapi     # also install the Intel oneAPI DPC++ compiler (icpx; several GB)
sr container run                # start the container with the repo mounted
sr container run --admin        # run with --privileged --cap-add=SYS_ADMIN (needed for profiling)
sr container run --no-gpu       # omit --gpus all (headless / CI environments)
```

By default the image ships the `intel-llvm` (primary) and `adaptivecpp`
toolchains. The `oneapi` toolchain (`icpx`) is large, so it is opt-in: build
with `--oneapi`, then inside the container `sr toolchain oneapi && sr build`
just works — the CLI sources `/opt/intel/oneapi/setvars.sh` for you.

For GPU passthrough you need the NVIDIA Container Toolkit installed on the host.

## `sr config` and `sr env` — diagnostics

When something isn't building the way you expect, these two commands show you
what the CLI actually decided to do.

```bash
sr config         # print the resolved config and where each value came from

sr env            # print the environment cmake/ctest/run subprocesses run under
sr env --all      # show every variable, not just the build-relevant ones (-a for short)
sr env --asan     # inspect the build-asan/ tree's environment instead
```

## `sr setup`, `sr doctor`, `sr link` and `sr unlink` — provisioning

These four commands come from `sushicore` and work the same in every module CLI.

```bash
sr setup                          # install what this checkout needs, then run doctor
sr setup --dry-run                # list what would be installed and written; change nothing
sr setup --toolchain adaptivecpp  # also install this SYCL toolchain (repeatable)
sr setup --no-gpu                 # skip the toolkit for this machine's GPU
sr setup --yes                    # answer yes to the LLVM download prompt

sr doctor                         # check the tools, toolchains and libraries a build needs
sr doctor --for test              # restrict the report to one check group

sr link --workspace <dir>         # record this checkout in a workspace's module registry
sr unlink --workspace <dir>       # remove it
```

`sr setup` reads two dependency fragments: the shared base that ships with
`sushicore`, and this repo's `cli/sushistack.deps.toml`. It installs into
`~/.sushisystems` (set `SUSHISYSTEMS_HOME` to move it) and writes the paths it
found to `cli/config.local.toml`. A toolchain already installed in a SushiStack
workspace's `dependencies/` tree counts as present. `sr` does not read a
`dependencies/` folder inside this repository.

`sr doctor` exits non-zero when a check fails, and each failed row names the
command that fixes it.

`sr link` and `sr unlink` take the workspace from `--workspace`, then
`SUSHISTACK_HOME`, then the nearest parent directory that holds a `.sushistack`
marker.

To provision several checkouts at once, use `hub install` from SushiStack.

## Building without the CLI

The CLI is a convenience, not a requirement — it only runs CMake and CTest for
you. If you'd rather drive CMake yourself, see the **Building with CMake
directly** section of the [README](../../README.md).
