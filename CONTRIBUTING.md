# Contributing to SushiRuntime

Thanks for your interest in improving SushiRuntime. This document explains how to
get a working build, the conventions we expect in a change, and how to get a pull
request merged.

By contributing you agree that your contributions are licensed under the
project's **Apache License, Version 2.0** (see [`LICENSE`](LICENSE)).

> New to the codebase? Read [`docs/guides/ARCHITECTURE.md`](docs/guides/ARCHITECTURE.md)
> first — it explains the layers, the lock-free data structures, the work-stealing
> scheduler, and the memory model that a non-trivial change will touch.

---

## 1. Getting set up

The fastest path from a fresh machine to a built, smoke-tested tree is the
one-shot installer:

```bash
bash install.sh            # Linux/macOS  (add --gpu for CUDA)
powershell -ExecutionPolicy Bypass -File install.ps1   # Windows (add -Gpu)
```

If you already have a SYCL toolchain (Intel oneAPI DPC++ or an intel/llvm
`clang++ -fsycl`), CMake ≥ 3.20, hwloc, and GoogleTest, install the CLI and build
directly:

```bash
python cli/install.py        # puts `sr` / `sushiruntime` on PATH
sr project build             # Release (default)
sr project test              # fast functional suite
```

See [`docs/guides/CLI_GUIDE.md`](docs/guides/CLI_GUIDE.md) for the full command
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
sr project build                  # Release
sr project build --type debug     # Debug
sr project build --type asan      # AddressSanitizer (isolated build-asan/ tree)
sr project build --distributed    # also compile the master-worker layer

sr project test --suite functional        # unit + regression + integration (run this often)
sr project test --suite benchmark          # stress + performance (heavier)
sr project test --suite all
sr project test --suite functional --filter 'Steal.*'   # ctest -R over test names
```

Expectations for a mergeable change:

1. **The functional suite passes** (`sr project test --suite functional`). It runs
   in CI on every push/PR.
2. **Concurrency changes are checked under sanitizers.** Anything touching the
   scheduler, memory pool, EBR, dependency tracker, or refcounting must pass an
   ASan build (`sr project build --type asan && sr project test --asan`). TSan
   (`--type debug`) is strongly encouraged for lock-free changes. Use
   `sr project test --repeat N` to hunt flakiness.
3. **New behavior ships with tests.** Put fast, deterministic tests in
   `tests/functional/` (unit/regression/integration); heavy stress/perf probes go
   in `tests/benchmark/`. Fixed bugs get a sentinel in
   `tests/functional/regression/`.
4. **Distributed changes** are validated with `sr project build --distributed` and
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
- **C++20.** `<format>`, `<bit>`, `<concepts>`, `<source_location>`, etc. are fair
  game; the code already relies on them.
- **Comments are few and useful.** A short plain-English line that explains *why*,
  not a restatement of the code. Delete redundant narration.
- **No separator comments.** Lines like `// <-----------`, `// =========`, or
  `// --------` are forbidden everywhere in the codebase.
- **Lock-free code carries its reasoning.** If you touch an atomic, name the
  memory order and say what it synchronizes with. These are the comments we keep.
- **Naming.** Types `PascalCase`, functions/variables `snake_case`, members
  trailing underscore (`impl_`), constants `UPPER_SNAKE`. Follow the surrounding
  code.
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

- `@brief` — one sentence on *why* the function exists.
- A following line (or two) on *how* it works, when the mechanism is not obvious.
- `@param` for each parameter — its meaning, units, ownership, or valid range.
- `@return` for the result — what it is and what each outcome signals.
- `@throws` / `@warning` when a caller must know about failure or a precondition.

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

## 5. Commits and pull requests

- Write imperative, scoped commit messages: `fix(scheduler): …`,
  `feat(distributed): …`, `docs: …`, `test(memory): …`.
- Rebase onto the current `main` before opening the PR; keep history clean.
- In the PR description, state **what** changed, **why**, and **how you tested it**
  (which suites, sanitizers, hardware). Link the issue.
- CI must be green. Reviewers look for correctness (especially memory ordering and
  the cross-context USM invariant), test coverage, and that the change respects
  the layer boundaries.

---

## 6. Adding a C++ dependency

All C++ library dependencies live in a single manifest, `cli/dependencies.toml`
(the installer's source of truth). Add a table:

```toml
[mylib]
description   = "What it is"
required      = true
linux_apt     = ["libmylib-dev"]
windows_vcpkg = ["mylib"]
check_cmd     = ["pkg-config", "--exists", "mylib"]   # optional idempotency check
```

The installer picks it up automatically. If the *build* needs it, also add the
`find_package` / `pkg_check_modules` call in `CMakeLists.txt`; if the Docker image
needs it, add it to the `Dockerfile` apt line.

---

## 7. Reporting bugs and requesting features

Use the GitHub issue templates. A good bug report includes the platform and
toolchain, the build type, the exact command, and the smallest reproducer you can
manage. For anything that looks like a **security** issue, do **not** open a public
issue — follow [`SECURITY.md`](SECURITY.md).

---

## 8. Questions

Open a GitHub Discussion, join the community Discord (see [`SECURITY.md`](SECURITY.md)
for the current invite), or email **mustafagarip@sushisystems.io**.

Please be respectful in all project spaces — see
[`CODE_OF_CONDUCT.md`](CODE_OF_CONDUCT.md).
