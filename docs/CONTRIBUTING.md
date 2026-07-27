# Contributing to SushiRuntime

Thanks for your interest in improving SushiRuntime. This document explains how to
get a working build, the conventions we expect in a change, and how to get a pull
request merged.

By contributing you agree that your contributions are licensed under the
project's **Apache License, Version 2.0** (see [`LICENSE`](LICENSE)).

> New to the codebase? Read [`ARCHITECTURE.md`](ARCHITECTURE.md)
> first — it explains the layers, the lock-free data structures, the work-stealing
> scheduler, and the memory model that a non-trivial change will touch.

---

## 1. Getting set up

The fastest path from a fresh machine to a built tree is SushiStack's one-shot
installer, which installs `ss` (SushiStack owns dependency provisioning for the
whole stack) and runs `ss install`:

```bash
curl -fsSL https://sushisystems.io/install.sh | bash
```

```powershell
irm https://sushisystems.io/install.ps1 | iex
```

If you already have a SYCL toolchain (Intel oneAPI DPC++ or an intel/llvm
`clang++ -fsycl`), CMake ≥ 3.20, hwloc, and GoogleTest, install the CLI and build
directly:

```bash
ss install-cli sushiruntime  # puts `sr` / `sushiruntime` on PATH
sr build             # Release (default)
sr test              # fast functional suite
```

See [`CLI_GUIDE.md`](CLI_GUIDE.md) for the full command
reference and how machine-specific toolchain paths are resolved
(`cli/config.local.toml`).

---

## 2. Before you start coding

- **Open an issue first** for anything beyond a small fix, so the approach can be
  agreed before you invest time. Use the issue templates.
- **Keep changes focused.** One logical change per pull request. A refactor and a
  behavior change in the same PR are hard to review and hard to revert.
- **Match the layer boundaries.** Higher layers depend on lower ones, never the
  other way around (see ARCHITECTURE §2). New cross-layer coupling needs a reason.

---

## 3. Building and testing your change

```bash
sr build                  # Release
sr build --type debug     # Debug
sr build --type asan      # AddressSanitizer (isolated build-asan/ tree)
sr build --distributed    # also compile the master-worker layer

sr test --suite functional        # unit + regression + integration (run this often)
sr test --suite benchmark          # stress + performance (heavier)
sr test --suite all
sr test --suite functional --filter 'Steal.*'   # ctest -R over test names
```

Expectations for a mergeable change:

1. **The functional suite passes** (`sr test --suite functional`). It runs
   in CI on every push/PR.
2. **Concurrency changes are checked under sanitizers.** Anything touching the
   scheduler, memory pool, EBR, dependency tracker, or refcounting must pass an
   ASan build (`sr build --type asan && sr test --asan`). TSan
   (`--type debug`) is strongly encouraged for lock-free changes. Use
   `sr test --repeat N` to hunt flakiness.
3. **New behavior ships with tests.** Put fast, deterministic tests in
   `tests/functional/` (unit/regression/integration); heavy stress/perf probes go
   in `tests/benchmark/`. Fixed bugs get a sentinel in
   `tests/functional/regression/`.
4. **Distributed changes** are validated with `sr build --distributed` and
   the `*Distributed*` tests, which run the full protocol over the in-process
   fabric and TCP loopback without needing a cluster.

---

## 4. Coding style

The codebase has a deliberate, consistent style. Match the file you're editing.

- **Allman braces.** Opening brace on its own line — functions, types, lambdas,
  control blocks. Trivial one-line accessors (`int x() const { return x_; }`) may
  stay compact.
- **Nested namespaces, Allman.** Write `namespace A\n{\n namespace B\n{` — not
  `namespace A::B`.
- **C++17.** The codebase targets C++17. Do not use `<format>`, `<concepts>`,
  `<source_location>`, or other C++20/23 facilities. `<bit>`, `<span>`, and an
  `std::expected`-style `Result<T, E>` are provided as shims under `core/`
  (`bit.hpp`, `span.hpp`, `result.hpp`) — use those, not the standard headers.
- **Comments are few and useful.** A short plain-English line that explains *why*,
  not a restatement of the code. Delete redundant narration.
- **No historical references in comments or Doxygen.** Do not mention the PR,
  issue, contributor, or past implementation that a change replaced — those belong
  in the commit message. A comment that says "we used to do X" or "fixed in #123"
  rots immediately and misleads future readers. If removing the sentence would not
  confuse anyone reading the code cold, remove it.
- **No separator comments.** Lines like `// <-----------`, `// =========`, or
  `// --------` are forbidden everywhere in the codebase.
- **Lock-free code carries its reasoning.** If you touch an atomic, name the
  memory order and say what it synchronizes with. These are the comments we keep.
- **Naming.** Types `PascalCase`, functions/variables `snake_case`, members
  trailing underscore (`impl_`), constants `UPPER_SNAKE`. Follow the surrounding
  code. No abbreviated type or struct names — `Statistics` not `Stats`,
  `Execution` not `Exec`, `Coordinate` not `Coord`. The namespace for the
  public API layer is `API`, not `Api`.
- **Every source file carries the Apache 2.0 license header** used across the
  tree. Copy it verbatim into new files.

When in doubt, run [`/simplify`] mentally: prefer reuse and the smaller change.

### Doxygen documentation

Every public function carries a Doxygen comment that answers two questions in
plain English: **why** the function exists (what problem it solves for the
caller) and **how** it does it (the mechanism, in one or two technical lines).
Then document **every** parameter and the return value — leave nothing open.

Keep it tight. The rule is completeness, not length: technical, objective, plain
English, no filler and no restating the obvious. If a `@param` only repeats the
parameter name, the line is noise — say what the value *means* or constrains.

This is the shape to follow — one `@brief` line, a single short line of context,
then every parameter and the return:

```cpp
/**
 * @brief Creates a host task that records its ID into a shared list.
 *
 * The optional delay widens the window for ordering bugs to surface.
 * @param task_id The ID of the task to record.
 * @param execution_order Reference to the shared list of executed task IDs.
 * @param order_mutex Mutex protecting the shared execution order list.
 * @param delay Optional artificial delay before recording the ID.
 * @return A callable that matches the SYCL host task signature.
 */
inline auto make_order_recorder(int task_id,
                                std::vector<int>& execution_order,
                                std::mutex& order_mutex,
                                std::chrono::milliseconds delay = std::chrono::milliseconds(0));
```

The tags, in order:

- `@brief` — one sentence on *why* the function exists.
- One short line on *how* it works or what to watch for — only when the mechanism
  is not obvious from the brief. Often a single line is enough; omit it when the
  brief already says everything.
- `@param` for each parameter — its meaning, units, ownership, or valid range.
- `@return` for the result — what it is and what each outcome signals.
- `@throws` / `@warning` only when a caller must know about failure or a
  precondition. Do not add them by reflex.

When the mechanism genuinely needs more than a line, expand the body — but stop as
soon as the caller knows enough:

```cpp
/**
 * @brief Routes a kernel to the queue that owns the memory it touches.
 *
 * USM is bound to the context it was allocated on, so a kernel must run on the
 * device that owns its buffers. This looks up the owning queue recorded at
 * allocation time and falls back to the default device queue when the pointer
 * was not allocated through the runtime.
 *
 * @param ptr A USM pointer the kernel reads or writes. May be null.
 * @return The queue whose context owns @p ptr, or the default queue if unknown.
 * @throws SushiException If @p ptr belongs to a context that has been torn down.
 */
sycl::queue& queue_for(void* ptr);
```

---

## 5. Documentation is part of the change

Documentation is not a follow-up task — it ships in the same pull request as the
code it describes. A reviewer should never have to discover that a guide, the
README, or the changelog now contradicts the tree. If your change makes any
existing sentence false, fixing that sentence is part of the change.

Treat the following as hard requirements, not suggestions:

- **Changelog entries start at v1.0.0.** The project has not yet been assigned a
  semantic version — we are in prototype stage. Do not add entries to
  [`CHANGELOG.md`](CHANGELOG.md) until the project cuts its v1.0.0
  release. Once versioned, every change updates the changelog with an entry
  under `## [Unreleased]` in the right group (`Added` / `Changed` / `Fixed` /
  `Removed` / `Deprecated`). This will be the one file *no* user-visible change
  may skip.
- **A new or changed feature updates the user-facing guides.** When you add,
  rename, or change the behavior of public API, you must update
  [`INTRODUCTION.md`](INTRODUCTION.md) (the getting-started
  tour and its code samples must compile against the current API) and
  [`ARCHITECTURE.md`](ARCHITECTURE.md) (every class,
  constant, and algorithm it names must still exist). Update the top-level
  [`README.md`](README.md) when the change touches what the project *is* or how a
  first-time reader gets started.
- **Any CLI change updates the CLI docs.** If you add, rename, or change a
  `sr` / `sushiruntime` command, flag, or its output, update
  [`CLI_GUIDE.md`](CLI_GUIDE.md) — the full command
  reference — *and* the build/test snippets in this file and the README that
  invoke it. Command help text (Typer `--help`) and the guide must agree.
- **Distributed changes update the distributed guide.** A change to the
  master-worker layer, its protocol, its build switch (the CMake option
  `SR_ENABLE_DISTRIBUTED`, exposed as `sr build --distributed`), or its runtime
  knobs (deadlines, version fencing) updates
  [`DISTRIBUTED_GUIDE.md`](DISTRIBUTED_GUIDE.md).
- **A new dependency updates the dependency surface.** See §7 — the entry in
  `../cli/sushistack.deps.toml`, the `CMakeLists.txt` find call, and the `Dockerfile`
  apt line must move together.
- **Public API carries Doxygen.** New or changed public functions follow the
  Doxygen rules in §4. The header *is* documentation — it feeds the fluent API
  reference site (`sr doxygen`, scoped to `include/SushiRuntime/api` by the
  repo-root `Doxyfile`), so a missing `@param` on a public symbol is a doc gap CI
  reports.

Code samples in the guides are checked against the real API, mirroring what the
integration tests do — a sample that no longer compiles is a broken doc. Before
opening the PR, grep the docs for any symbol, command, or flag you renamed and fix
every hit.

The PR description (§6) must say which docs you touched, or state explicitly that
the change is doc-invisible (an internal refactor with no API, CLI, or behavior
change). "Docs later" is not accepted.

---

## 6. Commits and pull requests

### Branch names

Branch off the current `main` with a type prefix and a short, lowercase,
hyphenated description of the work:

```
feature/handle-based-graph-add
fix/steal-race-on-empty-deque
chore/bump-googletest
docs/cli-guide-setup-section
```

Use `feature/` for new functionality, `fix/` for bug fixes, `chore/` for
build/tooling/dependency upkeep, `docs/` for documentation-only work. Keep it to
a few words — the branch names what it does, it is not the commit message.

### Commit headers

Every commit header is `type: short summary`:

- **Prefix** with the type: `feat:`, `fix:`, `chore:`, `docs:`, `test:`,
  `refactor:`. Add a scope when it sharpens the line: `fix(scheduler):`,
  `feat(distributed):`.
- **Lowercase** the summary, imperative mood, no trailing period. Short and
  precise — say what the commit does, not how it feels.

```
feat(distributed): add version fencing to the pull deadline
fix(memory): release the EBR epoch on the early-return path
chore: bump hwloc to 2.11
```

### Commit body and PR description

When a commit or PR needs explaining, write the body as bullet points. Each bullet
starts with a past-tense verb describing what the change did, then the object:

```
- Added handle-based Graph::add() with automatic dependency tracking.
- Changed IExecutionEngine::execute to be non-consuming so graphs replay.
- Removed the hand-wired dependency edges from the integration tests.
```

Plain English, technical, and objective. State the mechanism and the effect — no
marketing, no adjectives selling the change, no "this finally makes X great".

- The PR description states **what** changed, **why**, and **how you tested it**
  (which suites, sanitizers, hardware), names the docs you touched (see §5), and
  links the issue.
- Rebase onto the current `main` before opening the PR; keep history clean.
- CI must be green. Reviewers look for correctness (especially memory ordering and
  the cross-context USM invariant), test coverage, and that the change respects
  the layer boundaries.

---

## 7. Adding a C++ dependency

Dependency provisioning is owned by [SushiStack](https://sushisystems.io) (the
`ss` CLI), not this repo — it installs shared toolchains and libraries for the
whole stack into one workspace-level `dependencies/` tree. SushiRuntime only
*declares* the dependencies it needs, in its own manifest fragment,
[`cli/sushistack.deps.toml`](../cli/sushistack.deps.toml), which `ss` aggregates
alongside every other module's fragment.

### Manifest entry

Add a top-level TOML table. Every field except `description` is optional and
defaults to the value shown:

```toml
[mylib]
description   = "What it is and why it is needed"   # shown by `ss doctor`
required      = true          # false → warn on missing, don't abort the build
gpu_only      = false         # true → only relevant when GPU support is requested
linux_apt     = ["libmylib-dev"]          # apt package names; [] = not needed on Linux
windows_vcpkg = ["mylib"]                 # vcpkg port names;  [] = not needed on Windows
check_cmd     = ["pkg-config", "--exists", "mylib"]  # optional — exit 0 means already installed
```

**Field reference:**

| Field | Type | Default | Meaning |
|---|---|---|---|
| `description` | string | — | Human-readable purpose; shown in `ss doctor` output. |
| `required` | bool | `true` | `false` → a missing package logs a warning and continues. |
| `gpu_only` | bool | `false` | `true` → entry is only relevant when GPU support is requested. |
| `linux_apt` | string list | `[]` | apt package names. Empty list means the dep is not needed on Linux. |
| `windows_vcpkg` | string list | `[]` | vcpkg port names. Empty list means the dep is not needed on Windows. |
| `check_cmd` | string list | `[]` | Argv to run; exit 0 is treated as "already installed" and skips the package manager. Omit when there is no reliable command-line check. |

**Platform-only entries.** Leave the other platform's list empty to install a
dependency on only one platform:

```toml
[linuxonly]
description   = "Only needed on Linux"
linux_apt     = ["libfoo-dev"]
windows_vcpkg = []            # not installed on Windows
```

### What else to touch

Adding an entry to the manifest is enough for `ss install` to pull in the
package. The build system and Docker image are intentionally outside the
manifest's scope — update them separately if needed:

1. **`CMakeLists.txt`** — if the C++ build needs to find the library, add a
   `find_package(mylib REQUIRED)` or `pkg_check_modules(mylib REQUIRED mylib)`
   call, and link the relevant targets against it.
2. **`Dockerfile`** — if the Docker image needs the library at image-build time,
   add it to the `apt-get install` line in the `Dockerfile`.

These three files move together for any library the C++ build and the container
both consume.

### Verifying the addition

After editing the manifest, run `ss doctor` to confirm the entry is parsed and
displayed correctly, then `ss install` to install it on the current platform:

```bash
ss doctor       # confirm the entry appears in the dependency table
ss install      # install it (skipped automatically if check_cmd passes)
```

---


## 8. Reporting bugs and requesting features

Use the GitHub issue templates. A good bug report includes the platform and
toolchain, the build type, the exact command, and the smallest reproducer you can
manage. For anything that looks like a **security** issue, do **not** open a public
issue — follow [`SECURITY.md`](SECURITY.md).

---

## 9. Questions

Open a GitHub Discussion, join the community Discord (see [`SECURITY.md`](SECURITY.md)
for the current invite), or email **mustafagarip@sushisystems.io**.

Please be respectful in all project spaces — see
[`CODE_OF_CONDUCT.md`](CODE_OF_CONDUCT.md).
