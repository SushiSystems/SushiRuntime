# Contributing to SushiRuntime

How to get a working build, what a change must contain and how it lands.

Contributions from outside Sushi Systems are not accepted yet. SushiRuntime is source-available
under the PolyForm Noncommercial License 1.0.0; see [`LICENSE`](../LICENSE) and
[`COMMERCIAL.md`](../COMMERCIAL.md).

Read [`ARCHITECTURE.md`](architecture/ARCHITECTURE.md) before a change that crosses modules. It
lists the modules, which of them are public, and how a run moves through them.

## 1. Getting set up

On an empty machine, SushiStack's installer installs `hub` and runs `hub install`:

```bash
curl -fsSL https://sushisystems.io/install.sh | bash
```

```powershell
irm https://sushisystems.io/install.ps1 | iex
```

With only this checkout, install the CLI and let it install what `cli/sushistack.deps.toml`
declares:

```bash
hub install-cli sushiruntime
sr setup
sr build
sr test
```

Every build and test goes through `sr`. Do not call CMake, Ninja or CTest directly in this
repository. [`CLI_GUIDE.md`](guides/CLI_GUIDE.md) lists the commands and says where the
toolchain paths of one machine are kept (`cli/config.local.toml`).

## 2. Before you start

- Open an issue for anything beyond a small fix, so the approach is agreed first.
- One logical change per commit. A refactor and a change of behaviour are two commits.
- Keep the module boundaries. A module includes lower tiers and the same-tier modules that
  `cmake/SushiRuntimeTiers.cmake` lists; the build refuses another include, and
  `python tools/layering/check_layering.py .` reports it without a build.
- A header of a public module includes no header of an internal one. What names an engine type
  is compiled into the library, from `source/`.
- A change to a public name, signature, header path or layout is a breaking change. Its commit
  subject carries `!` and its changelog entry says what breaks.

## 3. Building and testing a change

```bash
sr build                  # Release
sr build --type debug     # Debug
sr build --type asan      # AddressSanitizer, in the build-asan/ tree
sr build --distributed    # also compile the distributed module

sr test                           # small, medium and large
sr test --label small             # one size; tests/README.md gives the sizes
sr test --module scheduler        # the tests of one module
sr test --filter 'Steal.*'        # ctest -R over test names
sr bench                          # the benchmark programs
```

A change is ready when:

1. The tests pass (`sr test`).
2. A change to the scheduler, the USM registry, epoch reclamation, the dependency tracker or
   reference counting also passes an ASan build (`sr build --type asan`, then `sr test --asan`).
   `sr test --repeat N` re-runs a test until it fails.
3. New behaviour has tests. `tests/` mirrors `modules/`: the test of the header
   `modules/<tier>/<module>/include/SushiRuntime/<module>/<name>.hpp` is
   `tests/<tier>/<module>/test_<name>.cpp`, and its benchmark `benchmark_<name>.cpp` beside it.
   A test that crosses modules belongs to the highest-tier module it uses. A fixed defect gets a
   `Regression_` test in the file of the unit the defect lives in. The kind of a test is the
   prefix of its suite name, never a folder; `tools/testing/check_test_mirror.py` checks the
   mirror.
4. A change to the distributed module passes `sr build --distributed` and
   `sr test --distributed`, which run the protocol over the in-process fabric and TCP loopback.

## 4. Coding style

Match the file you edit.

- **Allman braces.** The opening brace is on its own line for functions, types, lambdas and
  control blocks. A one-line accessor may stay on one line.
- **Nested namespaces are written out**, one `namespace` per level, not `namespace A::B`.
- **C++17.** No `<format>`, `<concepts>`, `<source_location>` or another C++20 facility. The
  `core` module has `bit.hpp`, `span.hpp` and `result.hpp` for what C++17 lacks.
- **No exception leaves the library.** A cause outside the program returns `Result<T>`. A rule
  the caller broke stops the process through `SR_VERIFY`, `SR_ASSERT` or `SR_FATAL`. Code that
  calls SYCL or a caller's callable goes through `Detail::translate_exceptions`.
- **Names.** Types are `PascalCase`; functions and variables are `snake_case`; members end in an
  underscore; constants are `K_UPPER_SNAKE`; enumerators are `PascalCase`. An acronym stays in
  capitals inside a name (`OperationID`, `USMRequest`). No abbreviated type name (`Statistics`,
  not `Stats`), no `I` prefix on an interface, no `get_` prefix, and the helper namespace is
  `Detail`. `python tools/naming/check_naming.py` reports a breach.
- **Namespaces.** A public name is declared in `SushiRuntime`, once, by the header that defines
  it. A name of an internal module is in `SushiRuntime::Internal::<Module>`.
- **Build-system names start with `sushiruntime_`.** Every CMake target, function and macro of
  this repository carries the full program name, because a parent project that adds this tree
  shares one flat namespace of targets with it.
- **CMake modules are included by absolute path**: `include("${SR_CMAKE_DIR}/X.cmake")` from the
  top level and `include("${CMAKE_CURRENT_LIST_DIR}/X.cmake")` from another module.
  `CMAKE_MODULE_PATH` takes the first match, which under a parent project is the parent's file.

### Comments

A comment says what a thing does, in Doxygen form. The reason for a design goes in the module's
`README.md` or in a design document, and the comment cites the path.

- Every source file opens with the license block and a `@file` block.
  `tools/licensing/write_license_block.py` writes the license block; do not type it.
- Every symbol carries a block of at most eight lines. `@brief` is one sentence that starts with
  its verb. `@param`, `@return` and `@pre` appear only when they state a rule the caller keeps
  or a meaning the brief does not give.
- Inside a body, a `//` comment is one line that states an invariant the code relies on.
- No history, no `TODO`, no separator line, no commented-out code and no `@date`.

```cpp
            /**
             * @brief Runs steps until a wall-clock budget elapses.
             * @param budget Wall-clock limit; at least one step always runs.
             * @return The RunReport, or the Error that ended the run.
             */
            template <class Rep, class Period>
            [[nodiscard]] Result<RunReport, Error>
            run_for(std::chrono::duration<Rep, Period> budget) noexcept
```

Source: `modules/surface/api/include/SushiRuntime/api/graph/plumbing/step_driver.hpp`

`python tools/documentation/check_source_comments.py modules` checks these rules.

## 5. Documentation is part of the change

A change that makes a sentence of the manual false corrects that sentence in the same commit.

- **Every change adds one line to the changelog.** [`CHANGELOG.md`](reference/CHANGELOG.md) is
  the only changelog. The entry goes on top of `## Unreleased`, in the commit of the change:
  `- <YYYY-MM-DD> — <scope>: <Past-tense verb> <what changed> (<where>).` It is one sentence of
  at most 240 characters, cites at most five places in backticks and gives no reason.
- **A fact about one module goes in that module's `README.md`.** The manual links to it and
  does not repeat it.
- **A document goes where the tree says.** A manual page goes in `getting_started/`,
  `architecture/`, `guides/` or `reference/`. Intent with an open phase goes in `design/`, with
  a status line and a row in [`design/README.md`](design/README.md). Open work goes in
  [`design/REMAINING_WORK.md`](design/REMAINING_WORK.md), the only backlog. What an agent writes
  while it works goes in its folder under `agent/`. Finished material moves to `archive/` and is
  not edited there. [`DOCUMENTATION_STYLE_GUIDE.md`](DOCUMENTATION_STYLE_GUIDE.md) says how a
  page is written.
- **A code block in the manual is taken from a file the build compiles**, an example or a test,
  and the page names the file. A change that renames a symbol corrects every page that uses it.
- **A change to the public interface** updates [`INTRODUCTION.md`](getting_started/INTRODUCTION.md),
  the README of the module, and [`ARCHITECTURE.md`](architecture/ARCHITECTURE.md) when a module
  or the path of a run changes.
- **A change to the CLI** updates [`CLI_GUIDE.md`](guides/CLI_GUIDE.md) and the commands quoted
  in this page and in the root [`README.md`](../README.md).
- **A change to the distributed module** updates
  [`DISTRIBUTED_GUIDE.md`](guides/DISTRIBUTED_GUIDE.md).
- **The checkers pass before a commit that touches `docs/`.** Run
  `python tools/documentation/check_docs_layout.py .` and
  `python tools/documentation/check_changelog.py .`. Both print nothing when the tree is in
  shape. [`KNOWN_ISSUES.md`](reference/KNOWN_ISSUES.md) lists the defects that stay open.

## 6. Commits and branches

Everyday work lands on `main` in small commits. Work that is risky or takes several days gets a
branch named `feature/<snake_case_name>`, which is rebased onto `main` before it merges and
deleted after.

A commit subject is `type(scope): sentence`, in lower case, in the present tense, under 72
characters, with no full stop:

```
fix(execution): keep a node the coordinator watches off the chain
```

| Type | For |
| --- | --- |
| `feat` | New behaviour |
| `fix` | Wrong behaviour made right |
| `refactor` | The same behaviour in another shape |
| `perf` | The same behaviour with fewer resources |
| `test` | Tests only |
| `docs` | Documentation only |
| `build` | CMake, the CLI, the checkers, the toolchain |
| `chore` | A release, or upkeep with no behaviour |

The scope is the module or the subsystem. A change that breaks a public interface, a file
format or a CLI command writes `type(scope)!:`. The body is optional: plain paragraphs wrapped
at 72 columns that say what changed and what it affects.

Stage by path. A commit holds one logical change with its tests, its module README and its
changelog line.

## 7. Adding a C++ dependency

SushiRuntime declares its dependencies in [`cli/sushistack.deps.toml`](../cli/sushistack.deps.toml).
`sr setup` installs them for this checkout into `~/.sushisystems`, and `hub install` of
SushiStack reads the same file for a workspace.

### Manifest entry

Add a top-level TOML table:

```toml
[hwloc]
description   = "Hardware locality library (topology / NUMA discovery). The runtime links it for the hardware topology layer."
required      = true
linux_apt     = ["libhwloc-dev"]
windows_vcpkg = ["hwloc"]
check_cmd     = ["pkg-config", "--exists", "hwloc"]
```

The block is the `hwloc` entry of `cli/sushistack.deps.toml`.

| Field | Type | Meaning |
| --- | --- | --- |
| `description` | string | The purpose, shown by `hub doctor` |
| `required` | bool | `false`: a missing package warns and does not stop the build |
| `provides` | string | A capability tag. Entries that share one are alternatives: any one of them satisfies the need. The three SYCL toolchains carry `sycl-toolchain` |
| `gpu_only` | bool | `true`: installed only when GPU support is requested |
| `linux_apt` | string list | apt package names; an empty list means not an apt package |
| `windows_vcpkg` | string list | vcpkg port names; an empty list means not a vcpkg port |
| `check_cmd` | string list | A command; exit code 0 means the dependency is installed |

### What else to change

The manifest entry is enough for `sr setup`. The build and the container image are separate:

1. A library the C++ build links needs its `find_package` or `pkg_check_modules` call in
   `cmake/Dependencies.cmake`, and the module that uses it links it.
2. A library the container image needs is added to the `apt-get install` line of
   `.config/docker/Dockerfile`.
3. A third-party component is listed in [`NOTICE.md`](../NOTICE.md) with its licence.

### Checking the addition

```bash
sr setup --dry-run   # the entry appears in the list of what would be installed
sr setup             # install it
```

## 8. Reporting bugs and requesting features

Use the GitHub issue templates. A bug report names the platform, the toolchain, the build type
and the exact command, and holds the smallest program that shows the defect. Do not open a
public issue for a security problem; follow [`SECURITY.md`](../.github/SECURITY.md).

## 9. Questions

Open a GitHub Discussion, use the Discord server named in
[`SECURITY.md`](../.github/SECURITY.md), or email mustafagarip@sushisystems.io. The
[code of conduct](../.github/CODE_OF_CONDUCT.md) applies in every project space.
