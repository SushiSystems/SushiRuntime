# Test sizes and CI stages: report

**Status:** landed on `main` on 2026-10-16, run inline.

## What landed

| Commit | Change |
| --- | --- |
| `258b880` | `build(deps)`: sushicore 0.9.0, whose `ctest_run` takes an excluded label. |
| `e0dc00b` | `test!`: every suite prefix is a size, `Small_`, `Medium_` or `Large_`, with a 60, 300 or 900 second limit. `SR_QUARANTINED_TESTS` names a quarantined test, which keeps its size. `cmake/CheckTestSizes.cmake` fails the build of a suite with no size. The benchmark programs leave CTest; `sr bench` runs them. `sr test --suite` became `--label`, and the Linux ASan tree carries UBSan. |
| `fcd8f00` | `ci`: `ci.yml` holds the pre-merge stage behind `gate` and the post-merge jobs; `nightly.yml` adds the benchmarks and the quarantined tests; `release.yml` calls it. |
| `7da2947` | `build(tools)`: the shared checkers from SushiSkills `f191126`, with `check_test_sizes.py`. |
| `2a6b388` | `test(cli)`: every CLI test carries a pytest size mark, and `conftest.py` refuses a test with no size or two. |
| `da354cb`, `2235de4`, `e0257fb` | `fix(cli)`: the sanitizer builds that the first CI runs broke; see below. |
| `fd61b15` | `docs`: the `.github` pages lose their em dashes; `cli/config.local.toml` joins the allowed names. |
| `f3b93b2`, `66fb0cf`, `b42730a`, `3c2adb0` | `ci`: fixes from the branch runs. |

The suites split into 234 small, 783 medium and 6 large tests; together they are the 1023 the suite
held before. The six large tests are `Large_RuntimeContextNvidia` and `Large_AsyncExecutionTest`,
which need a GPU.

## Evidence

- Local, Windows: `sr test --distributed` reported "100% tests passed, 0 tests failed out of 1023",
  with the three usual skips. `--label small` ran 234, `--label medium` 783, `--label large` 6, and
  `--label quarantined` found none.
- Quarantine, probed with `Small_Bit.CountrZeroFindsTheLowestSetBit` in `SR_QUARANTINED_TESTS`: the
  test carried the labels `small`, `quarantined` and `core`; `--label small` ran 233 and
  `--label quarantined` ran 1. The entry was removed afterwards.
- Size check, probed with `TEST(Unsized, Probe)` added to `test_bit.cpp`: the build failed with
  "the suite Unsized has no size prefix". The line was removed afterwards.
- CLI: `python -m pytest tests` in `cli/` passed 147.
- `python tools/check_all.py .` reports only `docs/api-site`, the generated folder git ignores,
  which a runner does not have. The tool tests pass.
- CI run 38817409771 on the branch: `gate` passed, with the checkers on both systems, the CLI,
  Linux, Windows, ASan and UBSan, the docs, and ThreadSanitizer. The SushiBLAS job reached step 700
  of 767 when its 45 minute limit cancelled it; it has a ccache and an hour since.
- CI run 38825715740 on `main` at `d1d1567`: `gate` passed, and so did ThreadSanitizer. The
  SushiBLAS job built and ran 2694 SushiBLAS tests; 2693 passed and one failed, a SushiBLAS test
  that compares a cube root by `==`. The job ended cancelled at its 60 minute limit, with its
  ccache cold. `docs/reference/KNOWN_ISSUES.md` records the failing test.

## What the branch runs found

| Failure | Cause | Fix |
| --- | --- | --- |
| Checkers | `check_docs_truth.py` had not run in CI before; the manual cites `cli/config.local.toml`, which git ignores | an entry in `docs/.docs_truth_allow` |
| Linux tests | `sr test` restores the shell's `LD_LIBRARY_PATH` for a oneAPI build, and the shell on a runner has no path to `libsycl` | `setup-oneapi` writes the oneAPI path into the job |
| ASan | `sr` replaced icpx with the first `clang++` on `PATH`, a system clang with no SYCL; then icpx passed `-fsanitize` to the device pass | `sr` takes acpp or the intel/llvm bundle, else icpx with `-Xarch_host` on each sanitizer flag |
| ThreadSanitizer | a benchmark replaces `operator new`, which the TSan runtime defines too | `sr` leaves the benchmarks out of a TSan build |
| Windows | `sr setup` took 15 minutes and the cold build passed the 45 minute limit | ccache and the `sr setup` tree are cached, saved even when a step fails; 90 minutes |
| SushiBLAS | no ccache on the job; `sb` used icpx for the C compiler | ccache installed and cached; oneAPI's `clang++`, which has a C sibling |

## Open

- Windows takes 90 minutes while its caches are cold, past the 45 minute ceiling the rule sets for
  the pre-merge stage. The warm time is measured on the next runs.
- `gate` is not a required check, and `main` takes changes directly; both are the owner's choice.
- SushiEngine is private and is not built downstream until a token exists as a secret.
- `nightly.yml` has not run; its first scheduled run is the first evidence.
- The SushiBLAS test that compares a cube root by `==` needs a tolerance, in SushiBLAS.
