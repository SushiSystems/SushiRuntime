# The SushiRuntime CLI

`sr` builds the library, runs its tests, runs the example programs and manages the development
container. It runs CMake, CTest and Docker with the toolchain paths it reads from two
configuration files. Every build and test in this repository goes through it.

For a first build, read [the introduction](../getting_started/INTRODUCTION.md).

## Installing the CLI

The CLI is a Python package in `cli/`. It installs two names for one command: `sr` and
`sushiruntime`. The examples below use `sr`.

```bash
hub install-cli sushiruntime      # always an editable install
```

`hub` is the SushiStack CLI and installs through pipx. To install the package yourself:

```bash
pipx install ./cli               # or:  pip install ./cli   (inside a venv/conda env)
```

To remove it:

```bash
pipx uninstall sushiruntime-cli
```

### Where the CLI reads its paths

- `cli/config.toml` is committed and holds the defaults every machine shares.
- `cli/config.local.toml` is ignored by git and holds one machine's absolute paths: the oneAPI
  and Visual Studio roots, the vcpkg location, the compiler executables.

`sr setup` writes `config.local.toml`. `hub install` does the same for every checkout of a
SushiStack workspace. To set paths by hand, copy the commented Windows block of `config.toml`;
both files use the same `[tool.<platform>]` sections.

`sr config` prints the resolved values and the source of each.

## Command overview

| Command | Does |
| --- | --- |
| `sr toolchain` | Chooses the SYCL toolchain: `intel-llvm`, `adaptivecpp` or `oneapi` |
| `sr build`, `test`, `run`, `clean`, `docs` | Build, test, run and document the C++ project |
| `sr package` | Installs the built tree to a prefix and archives it with CPack |
| `sr container` | Builds and runs the development container |
| `sr config` | Prints the resolved configuration |
| `sr env` | Prints the environment builds run under |
| `sr setup` | Installs what this checkout needs to build |
| `sr doctor` | Reports whether this machine is ready to build |
| `sr link`, `unlink` | Record this checkout in a SushiStack workspace, or remove it |

Every command takes `--help`. `sr container` with no subcommand prints the help of its group.

`sr --version` prints the CLI's distribution name and version. `sr --describe` prints every
command and its options as JSON, for a tool that drives the CLI.

`sr doxygen` and `sr docker` are hidden aliases of `sr docs` and `sr container` until
2027-03-25. Neither `--help` nor `--describe` lists them.

`sr --help` groups the commands under Toolchain, Project, Diagnostics and Container, and each
command's help ends with examples.

## `sr toolchain`

`sr toolchain <name>` records the choice in `cli/config.local.toml`. Every later `sr build`
passes it to CMake as `-DSR_SYCL_TOOLCHAIN` and derives the compiler from it.

```bash
sr toolchain                  # list the toolchains and show which one is active
sr toolchain intel-llvm       # intel/llvm nightly (clang++ -fsycl), the default
sr toolchain adaptivecpp      # AdaptiveCpp (acpp)
sr toolchain oneapi           # Intel oneAPI (icpx, icx-cl)
```

After a switch, run `sr build --clean` so CMake configures with the new toolchain. The
environment variable `SR_SYCL_TOOLCHAIN` overrides the recorded choice for one command.

## `sr build`

Configures when needed and builds the library and its test programs.

```bash
sr build                  # Release build (the default)
sr build --type debug     # Debug build
sr build --type asan      # AddressSanitizer, in a separate build-asan/ tree, CUDA off
sr build --distributed    # also compile the distributed module
sr build --no-cuda        # build the CPU/OpenCL path instead of detecting accelerators
sr build --no-test        # skip the test programs (SR_BUILD_TESTS=OFF)
sr build --clean          # delete the build tree first
sr build --jobs 8         # compile with eight parallel jobs
sr build --force          # fall back to clang++/clang when the selected toolchain's
                          # compiler is missing
```

`--type` (`-t`) takes `release`, `debug`, `relwithdebinfo` or `asan`. The `asan` type builds
into `build-asan/` with CUDA off, so it does not touch `build/`.

`--jobs N` (`-j N`) passes `--parallel N` to the compile step. Without it the build tool picks
the job count, and `CMAKE_BUILD_PARALLEL_LEVEL` applies when set.

The ASan tree leaves the benchmark programs out. They replace the global `operator new` and
`operator delete` to count allocations, which AddressSanitizer also replaces; on Windows the
two collide at link time. Run benchmarks from `build/`.

AddressSanitizer on Windows needs an intel/llvm bundle that ships compiler-rt. When
`lib/clang/<version>/lib/windows/clang_rt.asan_dynamic-x86_64.lib` is missing from the
toolchain, the link fails and a newer bundle is the fix. The CLI handles three more details
there:

- It passes `-Xarch_host -fsanitize=address`. `-fsycl` compiles every translation unit for the
  host and for the SPIR-V device, and no device AddressSanitizer exists; the unqualified flag
  draws a `-Woption-ignored` that `-Werror` turns into a failure.
- It defines `_DISABLE_STL_ANNOTATION=1`. MSVC's STL container annotations need `stl_asan.lib`,
  which ships with a Visual Studio component and not with the compiler. Container-overflow
  detection inside STL containers is lost; heap overflow, stack overflow, use-after-free and
  double-free are still detected.
- It passes `-fsanitize-address-use-after-return=never`, so use-after-return detection is off
  on Windows. With it on, a `throw` from inside a `catch` handler faults in the MSVC unwinder.

ThreadSanitizer is added to Debug and RelWithDebInfo builds outside Windows; see
`cmake/Sanitizers.cmake`.

## `sr test`

Runs tests through CTest. `--label` (`-l`) picks one label:

```bash
sr test                          # small, medium and large (the default)
sr test --label small            # one size: small, medium or large
sr test --label quarantined      # the intermittent tests KNOWN_ISSUES.md names
sr test --label package          # install to a prefix, then build and run a consumer
```

`tests/README.md` gives what each size may use and its time limit.

The `package` label checks the installed package from outside. It installs the built tree to a
prefix, then configures, compiles and runs `tests/package/` against that prefix with nothing
else on the path. A missing export, a leaked private flag or a header the install rule forgot
fails there. It is slower than the sized tests because it configures and compiles a second
CMake project.

```bash
sr test --filter 'Steal.*'                       # only tests whose name matches the regex
sr test --asan                                   # run the build-asan/ tree instead of build/
sr test --distributed                            # run against a build made with --distributed
sr test --module scheduler                       # the tests of one module, whatever their size
sr test --repeat 50                              # re-run each test up to 50 times, stop on the first failure
```

`--filter` (`-f`) is a `ctest -R` regex matched against `Suite.Case` names. `--repeat` (`-r`)
uses CTest's `--repeat until-fail`.

For a GoogleTest option CTest does not expose, run the program with `sr run` and pass the
option after `--`.

## `sr bench`

Runs every `sr_<module>_benchmarks` program in `build/tests/`, one after another. A benchmark
measures and is not a test, so CTest does not register these programs.

```bash
sr bench                         # every benchmark program
sr bench --filter 'Stress_*'     # passed to each program as --gtest_filter
```

## `sr run`

Runs a built executable. With no target it runs the default of the configuration,
`sr_api_tests`.

```bash
sr run                              # run the default target
sr run sr_api_tests                 # run a program by name
sr run --sort                       # pick from the list of executables
```

The name is matched exactly first, then as a substring.

Everything after `--` goes to the program:

```bash
sr run sr_execution_benchmarks -- --gtest_filter='Performance_GraphCompileBench.*'
sr run sr_api_tests -- --gtest_shuffle --gtest_break_on_failure
```

The two programs of the distributed demo take their own flags the same way:

```bash
sr run sr_distributed_demo
sr run sr_distributed_worker -- --master 127.0.0.1:5555
```

`--distributed ROLE` appends `--distributed=<role>` to the program's arguments, for a program
that reads its role from that flag. It accepts `master` and `worker`; any other value stops the
command with exit code 2 before the program starts. `--asan` runs programs from `build-asan/`.

## `sr clean` and `sr docs`

```bash
sr clean       # remove the build/ and build-asan/ trees
sr docs        # build the API reference into build/docs/api-site/html
sr docs bundle --release 1.0.0   # pack the manual and the reference for docs.sushisystems.io
```

`sr docs` runs Doxygen with `.config/doxygen/Doxyfile`. Doxygen is not vendored: install it
(`winget install DimitriVanHeesch.Doxygen` on Windows, `apt-get install doxygen graphviz` on
Linux, `brew install doxygen graphviz` on macOS) or set `doxygen_exe` in
`cli/config.local.toml`. The command prints the install line when it finds no Doxygen. It writes
the HTML site and, in `build/docs/api-site/xml`, the XML the bundle carries.

`sr docs bundle --release X.Y.Z` runs the same Doxygen build, then packs the pages
`docs/publish.toml` lists, the files they link to and that XML into
`build/docs/bundle/docs-bundle-X.Y.Z.tar.gz`, with the archive's SHA-256 in a `.sha256` file
beside it. `--out` names another folder. The release is three integers, without the `v` of the
tag. The command stops with the page and the line when a published page links to a file that
does not exist. The archive's layout and the keys of `docs/publish.toml` are in
`DOCS_BUNDLE.md`, a reference page of the SushiCore repository.

## `sr package`

Installs the built `build/` tree to a prefix and runs CPack over it.

```bash
sr package                       # install to build/dist, then archive
sr package --prefix ../staging   # install somewhere else
sr package --prefix ../staging --force  # clear a directory sr did not stage
sr package --no-archive          # install only, skip CPack
```

The prefix is emptied before the install, so a file the current build does not produce cannot
stay from an earlier run. `sr package` therefore limits what it empties:

- It never stages into a filesystem root, the home directory, the project root, a directory
  above the project root, or the `build/` tree. `--force` does not change this.
- A `--prefix` that does not exist, or is empty, is used as it is.
- A `--prefix` that holds files is emptied only when `sr package` staged it before, which the
  `.sr-stage` file it writes into every stage tree tells. Any other directory with files needs
  `--force`.
- `build/dist`, the default, is always emptied.

A refusal prints one error line and exits with code 1, and nothing is deleted. The same happens
when the old tree cannot be removed completely.

The build tree must be configured, so run `sr build` first. When the tree has no
`CPackConfig.cmake`, the archive step is skipped with a warning.

`sr package` produces the package and does not check it. `sr test --label package` checks it.

## `sr container`

`.config/docker/Dockerfile` holds an intel/llvm nightly bundle and the Intel OpenCL CPU runtime
on an `nvidia/cuda` base, so a build, with the CUDA backend, needs no compiler on the host.

```bash
sr container build              # build the image (intel-llvm + adaptivecpp toolchains)
sr container build --no-cache   # rebuild every layer, ignoring Docker's cache
sr container build --oneapi     # also install the Intel oneAPI DPC++ compiler (several GB)
sr container run                # start the container with the repository mounted
sr container run --admin        # run with --privileged --cap-add=SYS_ADMIN, for profiling
sr container run --no-gpu       # omit --gpus all
```

The `oneapi` toolchain is opt-in because of its size. After `sr container build --oneapi`,
`sr toolchain oneapi` and `sr build` work inside the container; the CLI sources
`/opt/intel/oneapi/setvars.sh`.

GPU passthrough needs the NVIDIA Container Toolkit on the host.

## `sr config` and `sr env`

```bash
sr config         # print the resolved configuration and the source of each value

sr env            # print the environment cmake, ctest and run subprocesses get
sr env --all      # every variable, not only the build-relevant ones (-a)
sr env --asan     # the environment of the build-asan/ tree
```

## `sr setup`, `sr doctor`, `sr link` and `sr unlink`

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

`sr setup` reads two dependency fragments: the shared base that ships with `sushicore`, and
this repository's `cli/sushistack.deps.toml`. It installs into `~/.sushisystems` (set
`SUSHISYSTEMS_HOME` to move it) and writes the paths it found to `cli/config.local.toml`. A
toolchain already installed in the `dependencies/` tree of a SushiStack workspace counts as
present. `sr` does not read a `dependencies/` folder inside this repository.

`sr doctor` exits non-zero when a check fails, and each failed row names the command that fixes
it.

`sr link` and `sr unlink` take the workspace from `--workspace`, then `SUSHISTACK_HOME`, then
the nearest parent directory that holds a `.sushistack` marker.

`hub install` from SushiStack provisions several checkouts at once.

## What the CLI passes to CMake

`sr build` configures with the options that
[the architecture page](../architecture/ARCHITECTURE.md#7-build-time-configuration) lists.
`sr env` prints the environment those commands run under.
