# The SushiRuntime CLI

SushiRuntime ships with a small command-line tool that drives everything you do
day to day: building the C++ library, running its tests, launching the example
programs, and managing the Docker image. It is a thin wrapper around CMake,
CTest, and Docker that reads your machine-specific toolchain paths from a config
file so you don't have to retype long compiler flags.

This guide explains every command in plain English. If you just want to get the
project compiling and running for the first time, start with
[INTRODUCTION.md](INTRODUCTION.md) instead — it walks you through setup and your
first program.

## Installing the CLI

The CLI is a Python package that lives in the `cli/` folder. Install it once and
it puts two commands on your `PATH`:

- **`sr`** — short name.
- **`sushiruntime`** — long name.

They are identical; use whichever you prefer. Every example below uses `sr`.

To install:

```bash
python cli/install.py            # add --editable for a live dev install
```

On Windows this installs through **pipx**; on Linux and macOS it installs with
**pip** into your current environment. You can also install the package
directly if you'd rather manage it yourself:

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

If you ran the one-shot installer (`sr setup all`), `config.local.toml` was
written for you. If you're setting paths by hand, copy the commented Windows
block from `config.toml` as a starting point — it uses the same
`[tool.<platform>]` section layout.

If you're ever unsure what the CLI is actually using, run `sr config show` to
see the final resolved values and where each one came from.

## Command overview

Commands are grouped under these subcommands:

| Group        | What it's for                                               |
|--------------|-------------------------------------------------------------|
| `sr toolchain` | Choose the SYCL toolchain (intel-llvm / adaptivecpp / oneapi) |
| `sr setup`   | First-time install: detect, install deps, configure, verify |
| `sr project` | Build, test, run, and document the C++ project              |
| `sr docker`  | Build and run the development container                     |
| `sr config`  | Show the resolved configuration                             |
| `sr env`     | Show the environment your builds run under                  |

Run any command with `--help` to see its options. Running a group with no
subcommand (e.g. `sr project`) prints that group's help.

## `sr toolchain` — choose the SYCL toolchain

SushiRuntime builds against one of three SYCL toolchains. `sr toolchain` records
your choice in `cli/config.local.toml`, and every later `sr project build`
passes it to CMake as `-DSR_SYCL_TOOLCHAIN` and derives the matching compiler.

```bash
sr toolchain                  # list the toolchains and show which one is active
sr toolchain intel-llvm       # intel/llvm nightly (clang++ -fsycl) — primary (default)
sr toolchain adaptivecpp      # AdaptiveCpp (acpp) — secondary, vendor-neutral
sr toolchain oneapi           # Intel oneAPI — icpx/icx-cl, supported, for VTune/Advisor profiling
```

After switching, reconfigure with `sr project build --clean` so CMake picks up
the new toolchain. To override per-invocation without persisting, set the
`SR_SYCL_TOOLCHAIN` environment variable.

## `sr setup` — first-time install

The setup command is a four-phase pipeline: **detect → install → configure →
verify**. The all-in-one form runs every phase; you can also run any single
phase on its own. All phases accept `--gpu` to include the CUDA/GPU
dependencies, and most accept `--dry-run` to print what *would* happen without
changing anything.

```bash
sr setup all              # do everything: detect, install, configure, build + smoke-test
sr setup detect           # inventory tools, compiler, dependencies, GPU — changes nothing
sr setup install          # install missing deps (apt on Linux, winget + vcpkg on Windows)
sr setup configure        # write cli/config.local.toml from the tool paths it probed
sr setup verify           # build the project and run the functional smoke test
```

Common combinations:

```bash
sr setup all --gpu        # include the CUDA backend dependencies
sr setup all --dry-run    # preview the whole pipeline without touching the machine
```

`sr setup detect` and `sr setup verify` only inspect or build, so they don't
take `--dry-run`.

## `sr project` — build, test, run

This is the group you'll use most.

### `sr project build`

Configures (if needed) and builds the library and its test binaries.

```bash
sr project build                  # Release build (the default)
sr project build --type debug     # Debug build
sr project build --type asan      # AddressSanitizer — built in a separate build-asan/ tree, CUDA off
sr project build --distributed    # also compile the optional master-worker layer
sr project build --no-cuda        # Linux only: turn the SYCL CUDA backend off
sr project build --no-test        # skip compiling the test suite (SR_BUILD_TESTS=OFF)
sr project build --clean          # delete the build tree first, then build from scratch
```

The `--type` (`-t`) option accepts `release`, `debug`, `relwithdebinfo`, or
`asan`. The `asan` type is special: it builds into an isolated `build-asan/`
directory with CUDA disabled, so it never collides with your normal `build/`
tree. Flags can be combined, e.g. `sr project build --type debug --clean`.

The test suite is compiled by default. Pass `--no-test` to skip it
(`SR_BUILD_TESTS=OFF`) for a faster, library-only build — useful when you only
need the runtime to profile or to debug a kernel under `compute-sanitizer`.

### `sr project test`

Runs the test suite through CTest. Tests are grouped by label, and you pick a
group with `--suite` (`-s`):

```bash
sr project test                          # functional suite (the default)
sr project test --suite functional       # unit + regression + integration (fast, run these often)
sr project test --suite benchmark        # stress + performance (heavy)
sr project test --suite all              # every test
```

The full list of suite values is: `unit`, `regression`, `integration`,
`functional` (umbrella for those three), `stress`, `performance`, `benchmark`
(umbrella for the last two), and `all`.

Useful options:

```bash
sr project test --suite functional --filter 'Steal.*'   # only tests whose name matches the regex
sr project test --asan                                   # run the build-asan/ tree instead of build/
sr project test --distributed                            # run against a distributed-enabled build
sr project test --repeat 50                              # re-run each test up to 50x, stop on first failure
```

`--filter` (`-f`) is a `ctest -R` regex matched against `Suite.Case` test names.
`--repeat` (`-r`) is handy for hunting down flaky tests — it uses CTest's
`--repeat until-fail`.

For GoogleTest-level options that CTest doesn't expose (shuffling, break on
failure), run the binary directly with `sr project run` and pass flags after
`--` (see below).

### `sr project run`

Runs a built executable. With no target it runs the default from config
(`sr_functional_tests`).

```bash
sr project run                              # run the default target
sr project run sr_functional_tests          # run a specific binary by name
sr project run --sort                        # interactively pick from the list of executables
```

The target name is matched exactly first, then by substring, so you usually only
need part of the name.

Anything after `--` is forwarded straight to the program. This is how you pass
GoogleTest flags:

```bash
sr project run sr_benchmark_tests -- --gtest_filter='AllocatorBench.*'
sr project run sr_functional_tests -- --gtest_shuffle --gtest_break_on_failure
```

For the distributed demo, `--distributed=master|worker` forwards a role to the
application's `main()`:

```bash
sr project run sr_distributed_demo --distributed=worker --master=127.0.0.1:5555
sr project run sr_distributed_demo --distributed=master --port=5555 --workers=1
```

(Only `master` and `worker` are accepted; anything else is ignored with a
warning.) Add `--asan` to run binaries from the `build-asan/` tree.

### `sr project clean` and `sr project doxygen`

```bash
sr project clean       # remove the build/ and build-asan/ trees
sr project doxygen     # build the fluent API reference into docs/api-site/html
```

`sr project doxygen` runs Doxygen against the repo-root `Doxyfile` (scoped to the
public `include/SushiRuntime/api` surface). Doxygen is an external tool, not
vendored: install it (`winget install DimitriVanHeesch.Doxygen` on Windows,
`apt-get install doxygen graphviz` on Linux, `brew install doxygen graphviz` on
macOS) or point `doxygen_exe` in `cli/config.local.toml` at an existing binary.
The command prints the right install line if it cannot find one.

## `sr docker` — the development container

A `Dockerfile` provides a complete SYCL toolchain (an intel/llvm nightly bundle,
the `intel-llvm` toolchain) plus the Intel OpenCL CPU runtime, so you can build
and run — including the CUDA backend — without installing a compiler on your host.

```bash
sr docker build              # build the image (intel-llvm + adaptivecpp toolchains)
sr docker build --no-cache   # rebuild every layer from scratch, ignoring Docker's cache
sr docker build --oneapi     # also install the Intel oneAPI DPC++ compiler (icpx; several GB)
sr docker run                # start the container with the repo mounted
sr docker run --admin        # run with --privileged --cap-add=SYS_ADMIN (needed for profiling)
sr docker run --no-gpu       # omit --gpus all (headless / CI environments)
```

By default the image ships the `intel-llvm` (primary) and `adaptivecpp`
toolchains. The `oneapi` toolchain (`icpx`) is large, so it is opt-in: build
with `--oneapi`, then inside the container `sr toolchain oneapi && sr project
build` just works — the CLI sources `/opt/intel/oneapi/setvars.sh` for you.

For GPU passthrough you need the NVIDIA Container Toolkit installed on the host.

## `sr config` and `sr env` — diagnostics

When something isn't building the way you expect, these two commands show you
what the CLI actually decided to do.

```bash
sr config show         # print the resolved config and where each value came from

sr env dump            # print the environment cmake/ctest/run subprocesses run under
sr env dump --all      # show every variable, not just the build-relevant ones
sr env dump --asan     # inspect the build-asan/ tree's environment instead
```

## Building without the CLI

The CLI is a convenience, not a requirement — it only runs CMake and CTest for
you. If you'd rather drive CMake yourself, see the **Building with CMake
directly** section of the top-level [README](../../README.md).
