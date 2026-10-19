# Test sizes and CI stages: plan

**Status:** Shipped — 2026-10-16. See `REPORT.md`.

The rules are in SushiSkills `ac2242a`: `testing`, section "Sizes, scopes and names";
`continuous-integration`, sections "Stages" and "Shape"; `repository-layout/TESTS.md`, section
"Sizes are labels".

## Where the tree stands

- `cmake/SushiRuntimeTests.cmake` labels a test by its GoogleTest suite prefix: `Unit_`,
  `Integration_` and `Regression_` with a 120 second limit, `Stress_` and `Performance_` with
  900 seconds. The module name is a second label.
- 585 suites: 440 `Unit_`, 140 `Integration_`, 3 `Regression_`, 2 `Stress_`, 2 `Performance_`.
- `sr test --suite` maps a suite to a `ctest -L` regex; `--module` runs one module.
- `.github/workflows/ci.yml` calls cmake and ctest directly, holds the nightly benchmark job
  behind `if: schedule`, and has no `gate` job, no job timeout, no concurrency group and no
  cache. The pyflakes step ends in `|| true`.

## Tasks, in hierarchy order

Each task is one build and one acceptance criterion.

1. **Size mechanism.** The suite prefix becomes the size: `Small_`, `Medium_`, `Large_`.
   `sushiruntime_add_test_program` registers each prefix with its one size label and its
   timeout, 60, 300 and 900 seconds. A test named in a quarantine list in the same file gains
   `quarantined`. Benchmark programs are no longer registered with CTest.
   *Accepted when* `ctest --print-labels` through `sr` lists only the three sizes, the module
   names and `quarantined`.
2. **Sizing every suite.** Each of the 581 test suites is renamed by what its file uses: no
   context, queue, thread, sleep or file is `Small_`; a SYCL context, a thread, localhost or a
   temporary file is `Medium_`; a test that needs a GPU and skips without one is `Large_`. The
   two `Stress_` and two `Performance_` suites move into their benchmark programs as plain
   benchmarks. READMEs that name a suite are corrected in the same commit.
   *Accepted when* the suite passes as before, split across the three sizes.
3. **CLI.** `sr test --label <size>` replaces `--suite`; `--label quarantined` runs the
   quarantined tests; `--module` stays. A `sr bench` runs the benchmark programs. `cli/README.md`
   and the CLI tests follow.
   *Accepted when* `sr test --label small` and `--label medium` together run what `sr test`
   ran.
4. **ci.yml.** Pull request and push to `main`:
   - `checks`: `python tools/check_all.py .` and the tool tests;
   - `linux`: `sr build`, `sr test --label small`, `sr test --label medium`;
   - `windows`: `sr build`, the same two runs;
   - `sanitizers`: `sr build --type asan` with UBSan, `sr test --asan --label small`;
   - `cli`: the CLI tests, pyflakes without `|| true`;
   - `gate`: needs every job above;
   - post-merge only: TSan over small and medium, and SushiBLAS built against this commit.

   Every job sets `timeout-minutes`, prints its `sr` command first, and caches ccache output by
   compiler, flags and build-file hash. A concurrency group cancels an older run of the same pull
   request. The `docs` job stays.
   *Accepted when* the workflow runs green on a pull request.
5. **nightly.yml.** Everything post-merge runs, then `sr bench` and `sr test --label
   quarantined`, the latter reported and never gating.
   *Accepted when* a manual dispatch of it completes.
6. **Documentation.** `docs/CONTRIBUTING.md`, `tests/README.md`, the changelog and
   `REMAINING_WORK.md`.

## Open for the owner

- **SushiEngine downstream.** SushiEngine is private, so a post-merge job reads it only with a
  token stored as a repository secret, which is a GitHub setting. The plan builds SushiBLAS,
  which is public, and leaves SushiEngine out until a secret exists.
- **Pull requests only.** The rule says `main` takes changes through pull requests and `gate` is
  the required check. The owner chose on 2026-10-15 that this work merges to `main` directly, with
  no pull request. The plan adds `gate`; requiring it is a GitHub setting and stays with the
  owner.
- **Windows runner.** CI builds Linux only today. The Windows job needs the intel-llvm toolchain
  on `windows-latest`; how long its install and build take is measured in task 4.
