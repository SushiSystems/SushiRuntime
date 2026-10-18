# Frequently asked questions

Short answers for a developer meeting SushiRuntime for the first time: what it is, what it
needs, how to build and consume it, what it leaves out, how it is licensed and where a problem
goes. Each answer links to the manual page that holds the detail.

## What is SushiRuntime?

SushiRuntime is a C++17 runtime library for task-based parallel computing on CPUs and GPUs
through SYCL. You describe work as a graph of tasks and declare which memory each task reads
and writes. The runtime detects the read-after-write, write-after-read and write-after-write
hazards between tasks, orders only the tasks that conflict, and runs the rest in parallel on
the devices it discovers.

The entry point is the fluent API under `modules/surface/api/include/SushiRuntime/api/`. You allocate data as
`Buffer<T>` or `State<T>` handles, record work with the verbs of `Graph`, and call `run()`. The graph
compiles on its first run and the compiled plan is replayed for every later step. The
[introduction](../getting_started/INTRODUCTION.md) has complete programs, and
[the architecture](../architecture/ARCHITECTURE.md) describes the layers underneath.

## What do I need to build it?

The [README](../../README.md) lists the requirements:

- a SYCL 2020 compiler, one of the three toolchains named below
- CMake 3.20 or newer
- hwloc, for topology discovery
- GoogleTest, for the test suite
- a C++17 standard library

`sr setup` installs the C++ libraries and the SYCL toolchain that `cli/sushistack.deps.toml`
declares into `~/.sushisystems`, and writes the paths it found to `cli/config.local.toml`.
`sr doctor` then reports whether the machine is ready to build, and each failed row names the
command that fixes it. Both commands are covered in [the CLI guide](CLI_GUIDE.md).

## How do I build it and run the tests?

Install the `sr` CLI, pick a toolchain, then build and test:

```bash
hub install-cli sushiruntime
sr toolchain intel-llvm
sr build
sr test
```

`hub install-cli` comes from SushiStack. Without it, `pipx install ./cli` installs the same
package from this checkout. `sr build` makes a release build by default, and `--type` accepts
`debug`, `relwithdebinfo` and `asan`. `sr test` runs the small, medium and large tests by default, and
`--label` selects one size, `quarantined` or `package`; `sr bench` runs the benchmarks.
[The CLI guide](CLI_GUIDE.md) lists every command and flag.

## Can I build it without a local compiler?

Yes. The repository carries `.config/docker/Dockerfile`, with an intel/llvm nightly SYCL bundle and the
Intel OpenCL CPU runtime. `sr container build` builds the image and `sr container run` starts
it with the repository mounted. GPU passthrough needs the NVIDIA Container Toolkit on the host.

## Which SYCL toolchains and GPU backends does it support?

Three SYCL toolchains, selected with `sr toolchain` or the CMake option `SR_SYCL_TOOLCHAIN`:

- intel-llvm (`clang++ -fsycl` from the intel/llvm nightly bundle): the primary toolchain, the
  default, and the one the Docker image uses
- AdaptiveCpp (`acpp`): the secondary toolchain
- Intel oneAPI DPC++ (`icpx` on Linux, `icx-cl` on Windows): supported, and kept for the Intel
  profiling tools

The GPU backend comes from `-DSR_GPU_BACKEND`, which takes `auto`, `cuda`, `rocm`, `intel`,
`cpu` or `none`. The default `auto` detects the installed GPU toolkit and otherwise builds the
portable CPU/OpenCL path. `sr build --no-cuda` builds that CPU/OpenCL path
directly. The build-time options are in §7 of
[the architecture](../architecture/ARCHITECTURE.md).

## How do I use SushiRuntime from my own project?

Install the built tree to a prefix, point `CMAKE_PREFIX_PATH` at it, and link the exported
target:

```cmake
find_package(SushiRuntime REQUIRED)
target_link_libraries(my_simulation PRIVATE SushiRuntime::SushiRuntime)
```

The runtime's kernels are header templates, so they are compiled in your translation units and
not in the library. Every translation unit of yours that touches the graph API must therefore
be compiled by a SYCL compiler, and the headers and the library must come from the same
release. `SushiRuntime::version_matches()` checks the second at start-up.

The package uses `SameMinorVersion` compatibility: a request for one minor version rejects the
next. Pin the exact tag you build against and reach the library through one adapter module of
your own; [the integration guide](INTEGRATION.md#version-compatibility) states the rule. `tests/package/` in the
repository is a working consumer. [The integration guide](INTEGRATION.md) covers the build
split, the flags you inherit and the troubleshooting cases.

## Are results reproducible from run to run?

For a fixed graph topology the runtime guarantees schedule-independent results: the same
inputs give the same outputs however the work was spread across workers. Replay with the same
binary on the same device is byte-equal. Equality across architectures is not claimed.

The guarantee depends on floating-point settings. `SR_DETERMINISTIC_FP` is `ON` by default and
forbids fast-math and FMA contraction in the runtime's translation units and, through the
installed package, in yours. Kernels in a translation unit you build with `-ffast-math` or
`/fp:fast` are outside the guarantee. The rebalancer is off by default, and turning it on
trades run-to-run reproducibility for throughput on long batch work. See
[the integration guide](INTEGRATION.md) for the flags and
SushiBLAS for reductions that fold in a fixed order, so a sum gives the same bits at any
worker count.

## Can it spread work across several machines?

Yes, through an optional master-worker layer that offloads individual tasks to remote workers
over TCP, without MPI. It is off by default. `sr build --distributed` or
`-DSR_ENABLE_DISTRIBUTED=ON` turns it on, and with it off none of the distributed code is
compiled.

Compiled code does not cross the network. A task ships an `OperationID` that names a kernel
compiled into the worker's program, plus buffer handles that each worker resolves to its own
memory. A task the policy declines runs its local fallback on the master. A task whose worker dies is sent to a surviving worker, or runs locally when
none remains. [The distributed guide](DISTRIBUTED_GUIDE.md) has
the demo, the wire protocol and the failure model.

## How does it relate to SushiStack and the other Sushi Systems parts?

SushiStack provisions several checkouts at once, and its command line is `hub`. `hub install`
fetches every
toolchain and library the modules of a workspace declare, and SushiRuntime declares its own in
`cli/sushistack.deps.toml`. `hub install-cli sushiruntime` installs the `sr` CLI. `sr link`
and `sr unlink` record this checkout in a workspace's module registry or remove it.

sushicore is the Python package the `sr` CLI takes its shared commands and help page from:
`setup`, `doctor`, `link` and `unlink` come from it and work the same in every module CLI.
SushiRuntime does not need a workspace. With only this checkout, install the CLI and run
`sr setup`. See [the CLI guide](CLI_GUIDE.md).

## What does it not do yet?

SushiRuntime is before 1.0. The interface is not frozen, a minor version may break a consumer,
and security fixes go to the latest `main` with no long-term support branch. The limits the
manual states today:

- A runtime runs one graph at a time. A run entry during another run returns `InvalidState`.
- The distributed layer has no master failover, and nothing about a cluster survives the
  master's process. Master and worker must agree on the byte order and layout of the element
  types.
- The distributed layer authenticates master and worker at the handshake and encrypts nothing.
  It is meant for a trusted LAN.
- A distributed worker runs one kernel at a time, on its device 0.

[Known issues](../reference/KNOWN_ISSUES.md) lists the recorded defects that are not yet fixed,
and §12 of [the distributed guide](DISTRIBUTED_GUIDE.md) lists that layer's limits.

## How is SushiRuntime licensed?

SushiRuntime is source-available and free for non-commercial use under the PolyForm
Noncommercial License 1.0.0. [`LICENSE`](../../LICENSE) is the binding text and lists the
permitted purposes: personal, non-commercial use, and use by the non-commercial organisations
it names.

Any commercial purpose needs a separate, paid licence from Sushi Systems. That includes use
inside a company, use in paid client work, and use in a product or service that is sold.
[`COMMERCIAL.md`](../../COMMERCIAL.md) says how to ask: write to hello@sushisystems.io with
what you want to build and who will use it. Third-party components keep their own licences,
listed in [`NOTICE.md`](../../NOTICE.md).

## Where do I report a problem?

Check [known issues](../reference/KNOWN_ISSUES.md) first. For anything that is not sensitive,
open a GitHub issue or Discussion.

Do not open a public issue for a security problem. Email mustafagarip@sushisystems.io, or use
GitHub's private vulnerability reporting under the repository's Security tab. The
[security policy](../../.github/SECURITY.md) promises an acknowledgement within 72 hours and an
initial assessment within 7 days. It asks for the affected component, the version or commit
hash, reproduction steps, and the platform, toolchain and build configuration.
