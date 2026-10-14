# Test coverage: report

**Status:** Phases 1 to 3 and task 1.10 landed on 2026-10-14. The break check covered five
headers on 2026-10-15; the other new tests are not checked that way.

## What landed

| Commit | Change |
| --- | --- |
| `8fd007b` | `refactor(distributed)!`: the shared vocabulary moves down into the public modules `transport` and `cluster_load`. Nine internal modules take the private parts. Tests stop reaching `source/`. |
| `819915e` | `refactor(engine)`: `dependency_tracking`, `work_stealing`, `region_boundary`, `numa_lookup` and `engine_handles` become modules, each with its own tests. |
| `33c8e15` | `test`: one test file for each of the 105 public headers that had none, the example tests and their README sections. |
| `d449cba` | `docs(agent)`: plan status. |

The include paths of `transport` and `cluster_load` changed, so the first commit breaks
consumers.

## Evidence

- Build: `CMAKE_BUILD_PARALLEL_LEVEL=4 sr build --distributed --jobs 4` reported "Build completed
  successfully".
- Tests: `sr test --distributed` reported "100% tests passed, 0 tests failed out of 1022". Three
  tests were skipped for the reasons the suite already gives:
  `Integration_DevicePipelineTest.DiamondAllKernelNodes`,
  `Integration_InteropHostExport.ImportOnAQueueWithoutTheExtensionReturnsUnsupported` and
  `Integration_SteadyStateAllocation.ProfiledReuseTicksDoNotAllocatePerTick`.
- The layering, naming, source comment, docs truth, changelog and test mirror checkers reported
  no findings.

## Departures from the plan

- `numa_lookup` became a platform module. The plan had it tested through the mapper instead.
- The emit_state tests run what the description does, with no private hook.
- `tools/layering/check_layering.py` keeps its own copy of the same-tier edges, so it gained the
  edges of the new modules. The SushiSkills copy of the checker was not touched.

## Task 1.10

`check_test_mirror.py` gained `rule_private_include`, written in SushiSkills as `1a1e278` and
copied here with this repository's license header. It reports an include that names a header
under a `source/` folder, by a path through `source/` or by its path below `source/`. A quoted
include that resolves beside the test file is a local helper and is not reported. The copy also
brings two rules SushiSkills already held: a unit's folder of aspect files, and the tier in the
include path.

On this tree the rule reports nothing. On `7f4bc7c`, the commit before this work, it reports 54
includes of 31 distinct private headers, the count the plan gives.

## Break check

On 2026-10-15 five headers were broken by hand and the suite was run. `countr_zero_u64(0)`
returned 63, `Extent::count()` added one, the deque's `size()` added one, `late_count_source`
kept an over-capacity host count, and the NUMA lookup added one to the node index. 11 tests
failed, and each break failed the new test written for its header:
`Unit_Bit.CountrZeroFindsTheLowestSetBit`, `Unit_Extent.CountIsTheProductOfTheSizes`, two
`Unit_WorkStealingDeque` tests, `Unit_LateCountResolution.AHostCountAboveTheCapacityResolvesToZero`
and two `Unit_NumaLookup` tests. The breaks were reverted and the suite passed 1022 of 1022.

## Open

- **Break check**: only the five headers above were checked.
- **Commit `fa75278`** (an over-capacity host LateCount resolves to 0) came from another
  session. The tests here follow its behaviour. It is also on local `main` as `51af947`, and
  that commit is not pushed.
- The three split candidates and the three test-folder helpers listed in
  `docs/design/REMAINING_WORK.md` are not part of this work.
