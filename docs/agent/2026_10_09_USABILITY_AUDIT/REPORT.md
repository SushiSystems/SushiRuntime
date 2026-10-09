# Usability and operability audit: sushiruntime

**Status:** Complete. Read-only audit of 2026-10-09; no auditor changed the repository.

Nine auditing agents read the tree at `bfbc137`, one per dimension, each with a disjoint scope.
No second agent re-checked their findings, so every finding below is one reader's claim with its
file and line. Nobody ran a build, a test, a benchmark or `sr`. Three auditors ran
`clang++ -fsyntax-only` on scratch probes (struct sizes, header parse times, type traits) and two
ran the Python checkers under `tools/`. A cost claim without a measurement is marked as a
hypothesis in its finding and names the benchmark that would settle it.

Findings already in `docs/design/REMAINING_WORK.md` or in the audit of 2026-10-05 are cited as
"already known" and not counted.

| Dimension | Prefix | High | Medium | Low |
| --- | --- | --- | --- | --- |
| Graph-building API | GRAPHAPI | 4 | 13 | 6 |
| Public API outside the graph folder | API | 4 | 12 | 6 |
| Scheduler, graph, memory, core | CORE | 5 | 13 | 3 |
| Execution, topology, interop | EXEC | 6 | 12 | 7 |
| Distributed layer | DIST | 5 | 14 | 6 |
| Documentation | DOCS | 5 | 12 | 5 |
| Layout, build, packaging | LAYOUT | 6 | 14 | 8 |
| Command line, CI, tools | OPS | 6 | 14 | 7 |
| Tests and benchmarks | TEST | 7 | 11 | 3 |
| Total | | 48 | 115 | 51 |

The sections after "Corrections" hold the nine reports as the auditors returned them. Their
headings were demoted by one level and the em dashes in the layout report's finding titles were
replaced by commas; nothing else was edited.

## What more than one auditor found

Two auditors reaching the same place from different scopes is the nearest thing this audit has
to a second check.

- The layering in `ARCHITECTURE.md` section 2 does not match the includes, and
  `tools/layering/check_layering.py` passes with an empty tier list (LAYOUT-01, OPS-19). Five
  two-module include cycles exist, six with the distributed layer on.
- `SR_THROW_IF` throws `std::runtime_error` where the headers document `Core::SushiException`,
  and the message carries no values (API-02, EXEC-07, CORE-17).
- No page lists the CMake options, CLI config keys and environment variables an operator sets
  (DOCS-09, EXEC-23, LAYOUT-04). Three options are documented nowhere.
- The log level is a compile-time constant and INFO lines go to the host program's stdout
  (API-06, CORE-09).
- CI builds and tests with raw `cmake` and `ctest` on a toolchain that is not the default, has no
  Windows build job and no ASan job (OPS-02, OPS-03, LAYOUT prior-audit row L4, TEST-15).
- The four numeric primitives (`add_reduce`, `add_segmented_reduce`, `add_prefix_scan`,
  `add_counting_sort`) and `Sum`, `Minimum`, `Maximum` sit in the public API (API-21, TEST-18,
  GRAPHAPI "already known"). The graph auditor adds that they cannot be built on the public API
  today, because they use four private members of `Graph`.
- The umbrella header reaches 73 of 85 headers and is the only header named `.h` (API-10,
  LAYOUT-12). `<sycl/sycl.hpp>` alone is about 4.0 s of a 5.6 s parse of `graph.hpp`
  (GRAPHAPI facts), so trimming SushiRuntime's own headers can save at most about 1.6 s per
  translation unit.
- An `SR_DISTRIBUTED` mismatch between consumer and library is not diagnosed, and the nine
  distributed headers install when the layer is off (API-09, DIST-23, LAYOUT-20).
- `RuntimeContext` carries nine groups of members (EXEC-03) and 25 test files include it
  directly (TEST-05), so splitting it moves tests as well as callers.
- `REMAINING_WORK.md` and `KNOWN_ISSUES.md` hold rows that the commits of 5 to 7 October made
  false (DOCS-08, OPS prior-audit table, EXEC-25).

## What the suites can and cannot hold

TEST-01 and TEST-02 bound every optimisation finding in this report. The one benchmark shaped
like the per-node round trip runs once per case, prints to stdout and has no stored baseline.
The two scheduler benchmarks allocate a task inside the timed region. Until those are repaired,
a change to the hot path (CORE-03, CORE-04, CORE-05, CORE-06, CORE-07) cannot be shown to be
faster. TEST-07 names four shapes with no bit-equality guard: `DynamicGraph` against `Graph`,
`add_host` mixed with kernels, `run_async` against `run`, and `Extent` or `NdRange` launches.

## Defects that are small and do not wait for a design decision

Each was read from the code and not reproduced.

| Finding | What |
| --- | --- |
| EXEC-01 | A GPU without a resolved PCI address is mapped to NUMA from uninitialised fields |
| CORE-01 | A failing node is always logged as `unnamed_task` |
| DIST-01 | A lost peer can kill the surviving process with SIGPIPE on Linux |
| DIST-02 | A failed output allocation terminates the worker process |
| DIST-16 | Master reader threads have no exception guard |
| DIST-20 | The inflight count is decremented twice for a late result |
| TEST-03 | The cancellation tests can lose the cancel and hang for 1500 s |
| TEST-04 | Seventeen tests turn any constructor failure into a skip |
| OPS-01 | `sr` imports `sushicore.docs_bundle`, which the `v0.8.0` tag of sushicore lacks |
| GRAPHAPI-17 | `unwrap()` copies the `RunReport` on every throwing run |
| LAYOUT-14 | `src/CMakeLists.txt` lists `memory/usm_allocator.cpp`, which does not exist |

## Decisions that belong to the owner

Module boundaries, public interface shape, data ownership and deletions are not the auditors'
to settle. The findings that wait on a ruling:

1. Whether the four numeric primitives and the combine vocabulary stay in the runtime
   (API-21, TEST-18).
2. The decomposition of `RuntimeContext` and whether it keeps deprecated forwarders
   (EXEC-03), and the single allocation entry point that would replace eight (EXEC-04).
3. Whether `TaskGraph` is a single-thread builder, which decides the fate of the 64 KiB
   tracker and of epoch reclamation (CORE-10, CORE-04).
4. Which headers are public: eight installed headers have no includer outside their own module
   (CORE-12), and `core/` holds six headers that belong to higher layers (LAYOUT-10).
5. The name of the builder type, which today cannot be written as `SushiRuntime::Graph`
   (GRAPHAPI-03), and the split of launch shape from data naming in `add` (GRAPHAPI-01).
6. One configuration struct for `Runtime::create` in place of two positional ones (API-01).
7. Whether `size()` counts `add` calls or internal nodes (GRAPHAPI-07), and whether range
   calls throw where they clamp today (API-05, GRAPHAPI-22). Both change behaviour.
8. Presets as the single statement of a configuration, or no presets (LAYOUT-06), and an
   explicit sanitizer option in place of the build-type switch (LAYOUT-05).
9. Whether module READMEs keep per-symbol rationale beside the code or move it to
   `docs/design/` (DOCS-05).
10. The default bind address of the master and a registered-peer gate (DIST-03). The first
    changes behaviour for anyone running two machines today.
11. Oxford comma: the humanizer's English rules ask for it, the repository's style guide and
    the delegation block write without it (documentation auditor's evidence section).

## Corrections

The dispatch gave the auditors four facts that the tree contradicts.

- The CMake option is `SR_ENABLE_DISTRIBUTED`, not `SUSHI_ENABLE_DISTRIBUTED`
  (`cmake/SushiRuntimeProjectOptions.cmake:35`).
- `include/SushiRuntime/distributed/` holds nine headers and a README, not ten headers.
- `tests/functional/regression/` holds one source file and a README, not two source files.
- `sr` imports `sushicore` and nothing named `sushicli`. How the two relate was not
  established.

Two reports were amended after they came back. The documentation auditor's report arrived
without evidence for two delegation items and was sent back; its reply is appended to its
section and narrows DOCS-05 from 32 READMEs to the 14 it opened. The execution auditor did
not report a scratchpad size; the orchestrator measured it as empty after the last auditor
finished.

The working tree changed during the audit and no auditor caused it. At the start it held two
uncommitted edits: `AdvancedControls::device_is_gpu` in
`include/SushiRuntime/api/runtime/runtime.hpp` and its line in `docs/reference/CHANGELOG.md`.
Seven auditors saw both in `git status` when they finished. Both files were rewritten at
20:04:30 local time and `git status` has been clean since. `HEAD` is unchanged and no stash was
added. The transcripts of this session and of all nine auditors contain no `Write`, no `Edit`
and no `git checkout`, `restore`, `stash` or `reset`. The API and execution reports treat
`device_is_gpu` as part of the surface; where they cite it, they describe the tree as it was
before 20:04:30.

## Graph-building API (GRAPHAPI)

Dispatch block evidence:
- Item 1 (SOLID): no code written. Every proposal below is shaped as one brick with one responsibility; where a proposal changes a module boundary or a public interface shape it says "owner decides".
- Item 2 (humanizer): skill loaded and applied to this report. Also loaded: `api-stability`, `cpp-code-style`, `performance`.
- Item 6: No source written; nothing to syntax-check.
- Item 7: scratch was five one-line `.cpp` probes under `scratchpad/graphapi/` and `scratchpad/m.cpp`, all deleted. `du -sh` on the scratchpad afterwards printed `0`. No binaries, no copies.
- Nothing was written in the repository. `sr`, `cmake`, `ninja`, `ctest` were not run. The only compiler use was `clang++ -fsyntax-only` and `-E`.

### Facts

Sizes (`wc -l`): graph.hpp 3052, dynamic_graph.hpp 1136, README.md 899, run_handle.hpp 256, region.hpp 67.

Counts in graph.hpp (`grep -c`):
```
task_count++: 23
dirty=true: 25
observe(reads|writes) calls: 52
make_reduce_scratch: 14
public add overloads: 16
add_untracked: 5 add_host: 2 add_reduce: 4 seg: 4
@see README lines: 53
throw sites: 34
static_assert: 0 (graph.hpp), 0 (dynamic_graph.hpp)
```
Task-recording entry points on `Graph`: 16 `add` + 5 `add_untracked` + 1 `add_emitter` + 2 `add_host` + 4 `add_reduce` + 4 `add_segmented_reduce` + `add_prefix_scan` + `add_counting_sort` = 34.

Front-end cost, measured. Command: `clang++.exe -DSR_DETERMINISTIC_FP=1 -ID:/Projects/sushiruntime/include -fsycl -O3 -DNDEBUG -std=gnu++17 -D_DLL -D_MT -Wall -Wextra -pedantic -fsyntax-only <one-include TU>` (flags from `build/compile_commands.json`, llvm-sycl toolchain, 5 runs each, warm cache):
```
a_sycl  (<sycl/sycl.hpp> only)        4304 3945 3968 4055 3750 ms
b_graph (api/graph/graph.hpp)          5621 5579 5528 5526 5656 ms
c_dyn   (api/graph/dynamic_graph.hpp)  6534 6278 5963 6060 5849 ms
```
Preprocessed non-directive lines (`-E | grep -vc '^#'`): sycl 354 860; graph.hpp 379 475; dynamic_graph.hpp 388 141; run_handle.hpp 365 268. So `<sycl/sycl.hpp>` is about 4.0 s of a 5.6 to 6.1 s parse and 91% of the lines. Everything SushiRuntime adds on top is about 1.6 s (graph.hpp) to 2.1 s (dynamic_graph.hpp) per translation unit. That is the ceiling for any header split or move to `src/`.

Type properties, verified by a syntax-only TU of `static_assert`s (rc=0):
```
static_assert(std::is_move_constructible<Region>::value)
static_assert(!std::is_copy_constructible<Region>::value)
static_assert(has_nullary_try_run<Graph>::value)
static_assert(!has_nullary_try_run<DynamicGraph>::value)
static_assert(std::is_nothrow_invocable<decltype(&Graph::cancel), Graph&>::value == false)
```

What a first-time user writes: `examples/quickstart.cpp:40-58` is four calls (`Runtime::create()`, `rt.state<float>(N)`, `rt.graph()`, `sim.add(heat, fn)`, `sim.run(STEPS)`). That path has no friction. Friction starts with the second example: once the data is a `Buffer`, the caller picks among 34 entry points and must usually name each buffer twice (GRAPHAPI-01).

`src/api/` does not exist (`ls src`: core, distributed, execution, graph, interop, memory, scheduler, topology). The whole api layer is header-only. `docs/architecture/ARCHITECTURE.md` "Compile policy" says kernels are header templates on purpose, so the consumer's TU compiles the arithmetic under the exported FP flags.

Already known, not repeated as findings:
- Graph is one class holding registration, primitives, drivers and device plumbing: prior audit C4 (REPORT.md:984-990).
- Graph and DynamicGraph duplicate the run surface and the catch ladder: prior audit C6 (REPORT.md:1000-1006).
- `sycl` types in the public surface: C10 (REPORT.md:1032). Test-only friends in public headers: C26 (REPORT.md:1160). Stacked `//` rationale and long Doxygen blocks: C15. Constant and enum naming (`REDUCE_TILE`, unsized `StepSync`): C16. 39 + 19 undocumented data members: REPORT.md:1170.
- Doxygen conformance sweep: REMAINING_WORK.md "Before the v1.0.0 freeze".
- A `Buffer`/`State` moved after `Graph::add` dangles: REMAINING_WORK.md "Correctness".
- Offload ops recorded on a Region run their local fallback: REMAINING_WORK.md "Distributed layer".
- `add_reduce`, `add_segmented_reduce`, `add_prefix_scan`, `add_counting_sort` sit in tension with the "runtime owns no math" rule; ARCHITECTURE.md §3 documents them as part of `API::Graph`. C4 already proposes moving them out of the class. Whether they leave the repository is the owner's decision. One new fact for that decision: they cannot be built on the public API today, because they use the private `emit_device_sized`, `emit_late_count_resolver`, `reduce_scratch_` and `reduce_registration_tokens_` (graph.hpp:2645, 2704, 3015, 3018).

### Findings

Ranked by how much each helps a user or an operator.

#### GRAPHAPI-01 [high, api] Launch shape and data naming are one flat overload set, so the caller names data twice and the matrix has holes
Evidence: with `add(Reads, Writes, n, fn)` the kernel still captures the raw pointer by hand: `g.add(Reads(buf), Writes(buf), N, [p](std::size_t i) {...})` (test_fluent_api.cpp:231-232). README.md:296-301 admits the two "can get out of step". The form that names data once, `add(Extent, In/Out/InOut..., fn)` (graph.hpp:514-524), exists only for `Extent`, so 1-D code writes `Extent{N}` to get it (game_engine_demo.cpp:76-100, vecadd_demo.cpp:48-57) although README.md:265 says a 1-D Extent "is only needed for an origin offset". Coverage by launch shape: captures only Extent; `vector<void*>` only per-element n; coupled States only implicit 1-D (graph.hpp:604-614, no Extent form, though README.md:333 names reaction-diffusion as the use); `Dynamic` only per-element and 1-D State; offload only per-element and 1-D State; `NdRange` has no State or capture form.
Change: split the two axes into two descriptor bricks. A launch descriptor (count, `Extent<N>`, `NdRange<N>`, late-bound window) and an access descriptor (Reads/Writes, captures, State, coupled States), with one internal `emit(launch, access, fn)`. Each existing overload forwards to it. New combinations then cost nothing. This is a public interface shape, so the owner decides before any code.
Breaks API: source-compatible; existing overloads forward. Effort: L.

#### GRAPHAPI-02 [high, api] A wrong callable signature gives an overload-set dump or an error inside a SYCL kernel; the class comment promises the opposite
Evidence: graph.hpp:244-245 "The concept-constrained overloads give a readable compiler error when a callable has the wrong signature". The code is C++17 `enable_if` (graph.hpp:273, 392, 533...) and holds zero `static_assert`s. A mistyped `add(state, fn)` therefore reports "no matching function" with 16 candidates. The two variadic forms do not constrain the trailing callable at all: `add(State<T>& out, State<A>& in0, Rest&&...)` requires only `sizeof...(Rest) >= 1` (graph.hpp:604-606) and `add(Extent<N>, Cap0, Args&&...)` only `is_capture_v<Cap0>` (graph.hpp:514-517). Their errors surface in `detail::invoke_state` / `invoke_with_pointers` (callable_traits.hpp:76, 100), inside a `parallel_for` lambda.
Change: one diagnostics brick, `api/detail/callable_checks.hpp`, with a `static_assert` per family stating the expected signature in words (for example "add(State<T>&, fn): fn must be callable as fn(std::size_t, const T*, T*)"). Each family gets a lowest-priority catch-all overload that fires it. Constrain the non-callable members of both packs (`State<...>&` or capture wrappers). Add `static_assert(std::is_trivially_copyable_v<Combine>)` where the doc already requires it (graph.hpp:733). Correct the class comment.
Breaks API: no. Effort: M.

#### GRAPHAPI-03 [high, api] The type a user builds with cannot be named as `SushiRuntime::Graph`
Evidence: `SushiRuntime::Graph` is the namespace of the lower layer (`::SushiRuntime::Graph::TaskGraph`, graph.hpp:79), so the builder is `SushiRuntime::API::Graph` and is not re-exported. `api/fluent.hpp:51-59` exports `Runtime`, `RunHandle`, `Reads`, `Writes`, `In`, `Out`, `InOut`, `LateCount`; `DynamicGraph` is exported from dynamic_graph.hpp:1134; `Graph` and `Region` are not. Tests and examples spell `API::Graph` 11 times (`grep -rn "API::Graph\b" tests examples`) and use `auto` elsewhere. The header itself has to write `::SushiRuntime::Graph::OpID`, `::SushiRuntime::Graph::LateBindings` fully qualified (graph.hpp:1712, 2491, 2558). A user who writes `using namespace SushiRuntime; using namespace SushiRuntime::API;` gets an ambiguous `Graph`.
Change: owner decides the naming. The two options I can see: rename the lower namespace, or give the builder an exported name that does not collide and keep `API::Graph` as an alias. Either way, export every user-facing type from one place (`fluent.hpp`), and move the `using API::DynamicGraph` out of dynamic_graph.hpp.
Breaks API: source-compatible with an alias and a deprecation. Effort: M.

#### GRAPHAPI-04 [high, layout] There is no recorder brick: Region inherits all of Graph, and DynamicGraph reaches into its private base
Evidence: `class Region : private Graph` (region.hpp:42) re-exposes ten names by `using`. Each Region therefore carries a `CompiledGraph plan_`, `dirty_`, `compiles_`, `async_step_pending_`, `profiling_requested_` and the whole run driver (graph.hpp:3029-3048), none of which a region uses. DynamicGraph reads `specs_`, `steps_`, `task_count_` and `has_dangling_resource()` through `static_cast<const Graph&>(*kv.second)` (dynamic_graph.hpp:173, 315, 432, 674, 972, 1073) and three `friend` declarations (graph.hpp:2045-2048). The emit epilogue `++task_count_; dirty_ = true; return *this;` is written 23 times.
Change: extract `TaskRecorder` (specs, liveness observers, scratch ownership, registration tokens, StepMachinery, one `record(NodeSpec)` that does the epilogue). `Graph` = recorder + plan cache + driver. `Region` = recorder only. `DynamicGraph` composes recorders through the recorder's public const interface, with no friend and no cast. This is the narrow interface C4 asks the primitives to emit through.
Breaks API: no (Region's public surface is unchanged). Effort: L. Module boundary: owner approves.

#### GRAPHAPI-05 [medium, layout] Proposed file map; `graph.hpp` and `dynamic_graph.hpp` stay the includes a user writes
All new files under `include/SushiRuntime/api/graph/` unless noted. Line ranges are today's.

| New brick | From | Lines | Holds |
| --- | --- | --- | --- |
| `api/vocabulary/late_count.hpp` | graph.hpp 97-118 | ~25 | `LateCount` (it is vocabulary, like `Dynamic`) |
| `api/detail/late_count_resolution.hpp` | graph.hpp 120-238 | ~120 | resolve / validate / source / read; non-template |
| `task_recorder.hpp` | graph.hpp 2303-2350, 2992-3027 | ~150 | GRAPHAPI-04 |
| `detail/emit_untracked.hpp` | graph.hpp 267-385 | ~120 | five untracked shapes + emitter |
| `detail/emit_tracked.hpp` | graph.hpp 387-505, 616-632, 2434-2471 | ~180 | Reads/Writes and raw-pointer shapes |
| `detail/emit_captured.hpp` | graph.hpp 507-524, 2397-2432 | ~60 | In/Out/InOut form |
| `detail/emit_state.hpp` | graph.hpp 526-614, 2352-2395 | ~135 | State and coupled State rules |
| `detail/late_window.hpp` | graph.hpp 2473-2600 | ~130 | `LiveRange`, `DeviceWindow`, `submit_late_window` |
| `detail/emit_dynamic.hpp` | graph.hpp 634-678, 2754-2918 | ~210 | Dynamic shapes, host-slot checks |
| `detail/device_sized.hpp` | graph.hpp 2602-2752 | ~150 | fault records, resolver node, `emit_device_sized` |
| `detail/emit_offload.hpp` | graph.hpp 1705-1804, 2920-2955 | ~135 | three offload shapes |
| `detail/emit_host.hpp` | graph.hpp 680-716 | ~40 | `add_host` |
| `primitives/reduce.hpp`, `segmented_reduce.hpp`, `prefix_scan.hpp`, `counting_sort.hpp` | graph.hpp 731-1703 | 230 / 160 / 245 / 336 | already known C4 |
| `step_driver.hpp` | graph.hpp 1854-2035, 2105-2301; dynamic_graph.hpp 187-287, 935-1063 | ~250 once | already known C6 |
| `plan_cache.hpp` | graph.hpp 1806-1852, 2957-2990 | ~80 | build, dirty flag, compile count, registration lookup |
| `graph.hpp` (kept) | | ~350 | class declaration, the public overloads as one-line forwards |
| `boundary_access.hpp` | dynamic_graph.hpp 434-481, 559-600, 702-785 | ~180 | `Pin`, `BaseAccess`, `compute_access`, interval tests |
| `region_composer.hpp` | dynamic_graph.hpp 483-557, 602-700, 787-933 | ~340 | cross edges, `rewire_base`, `build_region`, `commit`, `rearm` |
| `dynamic_graph.hpp` (kept) | | ~300 | regions map, dirty sets, public surface |

The overload bodies become free functions over `TaskRecorder`; the class keeps declarations only. `region_composer`, `boundary_access`, `late_count_resolution`, `plan_cache` and the non-template halves of `step_driver` are candidates for `src/` (GRAPHAPI-14).
Breaks API: no. Effort: L. Depends on GRAPHAPI-04.

#### GRAPHAPI-06 [medium, api] The same failure reports a different `Errc` through `try_run` and `try_wait`, and a run-time failure is labelled `compile_failed`
Evidence: `Graph::drive` maps `std::exception` and `...` to `Errc::compile_failed` (graph.hpp:2174-2181), although the `try` block covers the whole run (graph.hpp:2163). DynamicGraph does the same (dynamic_graph.hpp:988-995). `RunHandle::try_wait` maps the same two cases to `Errc::kernel_failed` (run_handle.hpp:214-221), and its comment says "the same failures, reported as a value" (run_handle.hpp:177-178). An operator who sees `compile_failed` for a throwing host task looks in the wrong place.
Change: one `error_from_current_exception(phase)` function in the C6 driver brick, with the phase (compile or step) passed by the caller, used by all three ladders.
Breaks API: yes in behaviour (the `Errc` a caller may branch on changes); changelog entry needed. Effort: S.

#### GRAPHAPI-07 [medium, api] `size()` and `rule_registration()` count recorded specs, not `add()` calls, and the docs call both "rules"
Evidence: `size()` is documented "Number of update rules (tasks) added so far" (graph.hpp:1808) and `rule_registration(rule)` takes "Position in add() order, below size()" (graph.hpp:1817). Both index `specs_` / `task_count_`, which a primitive bumps once per internal node (for example graph.hpp:813, 933, 950). README.md:251 says an emitter "is one rule, so size() counts it once however many nodes it adds", while one `add_prefix_scan` is several. The test has to save `graph.size()` before the call to find where its rules start (test_rule_registration.cpp:81, 118-122). `RunReport` task totals are `task_count_ * executed` (graph.hpp:2230), so they inherit the same unit. DynamicGraph has no counterpart at all, although `commit()` renumbers every index (dynamic_graph.hpp:898-914).
Change: make one `add*()` call one rule everywhere. The recorder keeps a per-call entry {first spec, spec count}; `size()` counts calls; `rule_registration(i)` takes a call index; a `last_rule()` query returns the index of the call just made so nobody computes positions. Give DynamicGraph `rule_registration(RegionKey, rule)`.
Breaks API: yes for `size()` on graphs that use primitives (value changes). Effort: M.

#### GRAPHAPI-08 [medium, api] `LateCount` and `Dynamic` solve the same problem with different shapes, and `LateCount` needs three statements for the plain case
Evidence: README.md:208 prescribes `LateCount lc; lc.capacity = n;`. Tests wrote their own factories twice (test_counting_sort.cpp:83 `LateCount fixed(std::size_t)`, test_device_sized_primitives.cpp:170 `device_count(...)`). Differences from `Dynamic`: `LateCount::host` is `std::function` (graph.hpp:106), `Dynamic` uses `Core::Delegate<..., 48>` (dynamic.hpp:79-85); `LateCount::device` is a borrowed `const Buffer<uint32_t>*` (graph.hpp:114), `Dynamic` stores `const uint32_t*` filled by a checked factory (dynamic.hpp:286); a `LateCount` device index is range-checked only at run (graph.hpp:145, 212) because `validate_late_count` (graph.hpp:168-182) skips it, while `Dynamic` checks at construction (dynamic.hpp:60). "host takes precedence ... which callers should not do" (README.md:205) is a rule the type could enforce. The header says the count is "resolved once per run" (graph.hpp:98, 105) but the resolver node runs every step (graph.hpp:2676-2687); `Dynamic` says "once per step".
Change: static factories `LateCount::fixed(n)`, `::from_host(capacity, provider)`, `::from_device(capacity, buffer, index)` that validate at construction and make "exactly one source" unrepresentable; same provider type as `Dynamic`; move to `api/vocabulary/`. The aggregate fields stay for one MINOR release.
Breaks API: source-compatible with a deprecation. Effort: S.

#### GRAPHAPI-09 [medium, api] Parameter order differs between sibling calls
Evidence:
- `add(reads, writes, n, fn)` (graph.hpp:393, 623) against `add(op, n, reads, writes, local, params)` (graph.hpp:1712, 1729): `n` moves in front of the access lists.
- `add(Dynamic, reads, writes, capacity, fn)` (graph.hpp:641) puts the modifier first; `add(Extent, captures..., fn)` (517) puts the launch first; `add(State, Extent, fn)` (570) puts it second; `add(reads, writes, Extent, fn)` (409) third.
- `add_reduce(input, output, n, op, identity)` (740) against `add_prefix_scan(counts, offsets, total, n)` (1130) and `add_counting_sort(keys, indices, offsets, total, n, key_range, counts*)` (1376), whose last parameter is an optional output passed as a raw pointer defaulting to null.
Change: fix one order (what GRAPHAPI-01 settles: modifier, launch, access, callable, options) and forward the old spellings with `[[deprecated]]`. Replace the nullable `const Buffer*` with an overload.
Breaks API: source-compatible with a deprecation. Effort: S to M.

#### GRAPHAPI-10 [medium, api] The run surface is uneven between the two graphs and within each
Evidence: `Graph` has `try_run()` (graph.hpp:1996); `DynamicGraph` does not (verified by `static_assert`, see Facts). `run_for` and `run_until` have no `try_` form and no caller-owned-report form on either class (graph.hpp:1905-1926, dynamic_graph.hpp:233-253), so a frame loop with a time budget must use exceptions and allocate a report. `run_async()` throws; there is no non-throwing start (graph.hpp:1936). `Graph::set_profiling_requested` is `noexcept` (graph.hpp:1841), DynamicGraph's is not (dynamic_graph.hpp:148). `cancel()` is documented "without throwing" and is not `noexcept` on either (graph.hpp:1990, dynamic_graph.hpp:295). No run entry is `[[nodiscard]]` except the `try_` ones.
Change: when C6's single driver lands, generate the full grid once (throwing / `try_`, owned / reused report, steps / budget / predicate) so both graphs get the same set by construction.
Breaks API: no (additions only). Effort: S after C6.

#### GRAPHAPI-11 [medium, api] Three move hazards the documentation denies
Evidence:
- `Graph(Graph&&) = default` is documented "@param other Graph to move from; left with no tasks" (graph.hpp:253-255). A defaulted move leaves `task_count_`, `dirty_` and `compiles_` as they were (graph.hpp:3029, 3041, 3045), so the moved-from graph reports `size() > 0`, passes the "no tasks" guard (graph.hpp:2150) and runs a moved-from plan. Read from the code, not executed.
- README.md:809 says a Region "is not copyable or movable". It is move-constructible (verified by `static_assert`); `Region stolen = std::move(world.region(k));` compiles and empties the region the DynamicGraph still owns.
- `RunHandle` holds a raw `Owner*` (run_handle.hpp:251) and `Graph` and `DynamicGraph` are movable, so moving a graph while its handle is pending leaves the handle pointing at the old object. The precondition (graph.hpp:1930) mentions outliving, not moving.
Change: user-declared moves on the recorder that reset the scalars; delete Region's move operations; make the graph move check `async_step_pending_` (assert, per cpp-code-style "the caller broke a precondition") and document it.
Breaks API: deleting Region's move is formally breaking and matches the documented contract. Effort: S.

#### GRAPHAPI-12 [medium, api] `name_last()` stores the caller's `const char*` and names by position
Evidence: graph.hpp:720-728 "Persistent label, stored not copied"; `NodeSpec::name` is `const char*` (graph.hpp:3006) and is handed to `TaskMetadata` at every build (graph.hpp:2972, task_graph.cpp:472-479). `g.add(...).name_last(label.c_str())` with a temporary `std::string` dangles with no diagnostic, and the name surfaces only in a profile. For a multi-node call it names "the last one, the final fold" (README.md:404), so the other levels of a reduction stay "unnamed_task".
Change: the recorder owns names (a stable store, for example `std::deque<std::string>`), `name_last` takes `std::string_view`, and with GRAPHAPI-07's per-call entry the name applies to every node of the call.
Breaks API: no (`const char*` converts). Effort: S.

#### GRAPHAPI-13 [medium, api] `DynamicGraph::region(key)` marks the region for rebuild on every access
Evidence: dynamic_graph.hpp:94-102 inserts into `dirty_regions_` unconditionally. `world.region(k).size()` or holding the returned `Region&` to pass around forces a teardown and rebuild of that region at the next run. `find_region` (dynamic_graph.hpp:119) is the only non-dirtying accessor and returns `const Region*`. A region can only be appended to; replacing its tasks means `drop(key)` then `region(key)`, which the header does not say.
Change: the recorder carries a generation counter bumped by `record()`; `commit()` compares it with the generation it last built. `region()` stops dirtying. Add `Region::clear()` so replacing a region's work is one call.
Cost claim (hypothesis): a spurious rebuild costs one `build_region` plus boundary rewiring; `tests/benchmark/performance/test_recompose_churn_perf.cpp` would settle how much.
Breaks API: no. Effort: S to M.

#### GRAPHAPI-14 [medium, performance] About 1 100 lines of non-template code are parsed by every including TU; the measured ceiling of the gain is 1.6 to 2.1 s of a 5.6 to 6.1 s parse
Evidence, non-template bodies in headers: `add_prefix_scan` (graph.hpp:1130-1366, 237 lines), `add_counting_sort` (1376-1703, 328), late-count helpers (130-237), `run_async` and `finish_async_step` (1936-1983, 2074-2098), `resolve_live_range`, fault records, host-slot checks, `build_plan` (2490-2513, 2607-2634, 2796-2881, 2962-2981), and the whole composer in dynamic_graph.hpp:503-933 (about 430 lines, only `for_each_intra_successor` is a template). Parse times are in Facts.
Change: after GRAPHAPI-05, move `region_composer`, `boundary_access`, `late_count_resolution`, `plan_cache` and the non-template driver half to a new `src/api/`. The two uint32 primitives could follow; they hold no floating point, so the FP-flag reason for header kernels (ARCHITECTURE.md "Compile policy") does not apply to them, but their kernels would then be compiled into the library, which I did not check the build supports.
How to settle it: `-ftime-trace` on the three probe TUs before and after; there is no compile-time benchmark under `tests/benchmark`. Since sycl.hpp is 4.0 s of the total, the larger lever is C10 (already known).
Breaks API: no. Needs a new source folder and a build file change (owner; outside this dispatch). Effort: L.

#### GRAPHAPI-15 [low, layout] Include hygiene in the four headers
Evidence: graph.hpp includes `<cassert>` (line 32, no `assert(` in the file) and `topology/hw_discovery.hpp` (line 72, no `Topology::` use). run_handle.hpp catches `sycl::exception` (line 210) and includes no SYCL header; it compiles through `execution_engine.hpp`. dynamic_graph.hpp includes `api/runtime/runtime.hpp` (line 46) only to define `Runtime::dynamic_graph()` at its own bottom (lines 1127-1130), so a member of `Runtime` is defined in another class's header, and a TU that includes only runtime.hpp and calls `rt.dynamic_graph()` has a declared but undefined inline function. DynamicGraph itself needs only `friend class Runtime`.
Change: drop the two unused includes; include what run_handle.hpp uses; let runtime.hpp include dynamic_graph.hpp and define both factories there.
Breaks API: no. Effort: S.

#### GRAPHAPI-16 [medium, performance, hypothesis] Liveness observers are never de-duplicated, and every `run()` walks all of them
Evidence: `observe()` appends each handle's observer on every `add` (graph.hpp:2321-2341); `has_dangling_resource()` loops over all of them on each run entry (graph.hpp:2344-2350, called at 1945 and 2158). A graph of 10 000 tasks over three buffers holds about 30 000 observers and checks all of them every tick of `while (running) sim.run(report)`. DynamicGraph does it per region per run (dynamic_graph.hpp:314-318, 971-974). `add_counting_sort` alone calls `observe` about 20 times per call.
Change: the recorder keeps one observer per distinct allocation.
Measurement that would settle it: `tests/benchmark/performance/test_scheduler_dispatch_perf.cpp` with a many-node, few-buffer graph and one step per `run()`; `test_dynamic_graph_perf.cpp` for the composed case. Unmeasured.
Breaks API: no. Effort: S.

#### GRAPHAPI-17 [low, performance, hypothesis] `unwrap()` copies the `RunReport` on every throwing run
Evidence: `return *r;` on a by-value `Result` parameter (graph.hpp:2106-2111, dynamic_graph.hpp:422-427) copies the report, including its per-node vectors when profiling is on. `drive()` two functions below does `return std::move(report)` (graph.hpp:2129).
Change: `return std::move(*r);`.
Measurement: any benchmark that calls `run(steps)` with profiling on, for example `test_profiling_queue_perf.cpp`. Unmeasured.
Breaks API: no. Effort: S.

#### GRAPHAPI-18 [low, performance, hypothesis] Each recorded task is a `std::function` holding copies that are copied again at every build
Evidence: `NodeSpec::emit` is `std::function<void(BackingGraph&)>` (graph.hpp:3005). The tracked overloads copy the key vectors into `const` locals, capture them by copy, then pass them by copy to `add_node` at each build (graph.hpp:414-423, 444-452, 471-478, 496-500); the callable is copied into the node lambda at each build as well (`[fn, n]`, graph.hpp:278, 298, 418...). `std::function` also requires a copyable callable, so an `add_host` task that owns a `std::unique_ptr` is rejected with an error from inside `<functional>`. All of this is on the build path, not the per-step path; it matters for DynamicGraph, where a region rebuild replays every spec (dynamic_graph.hpp:674-678).
Change: move the vectors into the spec, keep them in the spec and hand `add_node` a view; consider a move-only erased callable for host tasks.
Measurement: `test_graph_compile_perf.cpp` and `test_recompose_churn_perf.cpp`. Unmeasured.
Breaks API: no. Effort: M.

#### GRAPHAPI-19 [low, performance, hypothesis] The composer uses node-based containers on the commit path and takes root order from pointer hashes
Evidence: `commit()` builds two `std::set`s per call (dynamic_graph.hpp:829, 832); `compute_access` builds a `std::map<Base, std::unordered_map<Node*, Acc>>` plus an `unordered_set` and an `unordered_map` per base (714, 731-732); the persistent state is four `std::map`s and an `unordered_set<Node*>` (1083-1099). `plan_.roots.assign(roots_.begin(), roots_.end())` (880) takes its order from an `unordered_set<Node*>`, so root submission order depends on node addresses and can differ between two runs of the same program. Ordering correctness does not depend on it; a reproducible profile or schedule does.
Change: sorted vectors keyed by `plan_index` for roots and for the per-base tables.
Measurement: `test_recompose_churn_perf.cpp` for cost; a repeat-run comparison of `RunReport` node order for the reproducibility point. Unmeasured.
Breaks API: no. Effort: M.

#### GRAPHAPI-20 [medium, documentation] Statements that contradict the code
- graph.hpp:244-246: "concept-constrained ... readable compiler error" (see GRAPHAPI-02).
- graph.hpp:253, 259 and dynamic_graph.hpp:74, 80: moved-from graph "left with no tasks / no regions" (see GRAPHAPI-11).
- graph.hpp:1808, 1817 and README.md:251, 584-587: the unit of `size()` and `rule_registration` (see GRAPHAPI-07).
- README.md:176-180 and 671-676: "drive() settles a pending asynchronous step". The code rejects the run with `invalid_graph` (graph.hpp:2154-2157, dynamic_graph.hpp:967-970); nothing is settled.
- dynamic_graph.hpp:916-918 and README.md:172-173: `rearm()` "refreshes the per-step updaters". It only re-arms in-degrees and resets offload attempts (921-933); `refresh_updaters()` is separate and runs once per run (1022).
- README.md:156-158: `teardown_region()` "marking each dirty". It marks nothing (793-814); `commit()` collects the dirty bases beforehand (832-839).
- README.md:809: Region "not copyable or movable" (see GRAPHAPI-11).
- graph.hpp:98, 105 and README.md:443-444, 480-481: LateCount "once per run"; the code resolves per step.
- run_handle.hpp:177-178: `try_wait` "the same failures" as `try_run` (see GRAPHAPI-06).
- graph.hpp:81: the brief of the boolean trait `is_graph_emitter_v` describes an emitter, not a predicate.
- graph.hpp:2436-2438: a `@param` broken across three lines mid-sentence.
Change: correct each in the pass that touches its code. Breaks API: no. Effort: S.

#### GRAPHAPI-21 [low, documentation] Public overloads document one parameter or none, and 53 `@see` lines point at an 899-line README without a section
Evidence: `add(const Reads&, const Writes&, std::size_t n, F&& fn)` documents `n` only (graph.hpp:387-394); the five tracked overloads have no `@return`, and none says what `add` throws although `add(Dynamic, ...)` throws `invalid_access` from `require_host_slot` (graph.hpp:2808-2827). `add_untracked(n, fn)` (267-274) documents `fn` and not `n`. Three of the four `observe` helpers, both `drive_impl`s and DynamicGraph's first `drive` have no comment (graph.hpp:2325-2341, 2185; dynamic_graph.hpp:935, 999). Every `@see include/SushiRuntime/api/graph/README.md` names the file and no heading. The README holds rationale for private helpers (`covered_by_any`, `retire_covered`, `NodeSpec converting constructor`) next to the public contract, and has no table that tells a user which `add` to call.
Change: belongs to the backlog's Doxygen sweep (already known); the part that is new is a "which add do I call" table at the top of the README, keyed by launch shape and data naming, and `@see` targets that name the heading.
Breaks API: no. Effort: M.

#### GRAPHAPI-22 [medium, api] Registration accepts launch shapes the runtime could reject from what it already knows
Evidence: `add(State<T>& s, Extent<N> e, fn)` (graph.hpp:570-594) never compares the extent with `s.size()`. `add(reads, writes, Extent, fn)` and the capture form (409, 517) do not compare the extent with the named buffers. The coupled form iterates `out.size()` (2375) and hands the kernel the input pointers without checking that the inputs are at least that long. `add(State, fn)` and `add(Dynamic, State, fn)` read `sp->size()` at build time through a raw `State*` (544, 2902). A mismatch is an out-of-bounds device write, which on the Windows CUDA lane the backlog already describes as a 0xC0000006 fault with no call site.
Change: a registration-time check where both sizes are known (extent volume plus offset against State size; coupled input sizes against the output), throwing `invalid_graph` with the rule's name, in the same style as the 34 existing checks. It validates shapes and computes nothing.
Breaks API: yes in behaviour for graphs that are already wrong. Effort: S.

#### GRAPHAPI-23 [low, api] Names in the public surface that do not say what they are
Evidence: `BackingGraph` (graph.hpp:79) is a public alias for the lower-layer `TaskGraph`, exposed so `add_emitter` has a parameter type; `no_registration` (1813) is a constant in lower case next to `NO_REGISTRATION`; `set_profiling_requested` / `profiling_requested` (1841, 1852) are the only setter/getter pair on a class whose other verbs are fluent and return `Graph&`; `compile_count()` on DynamicGraph counts compositions (dynamic_graph.hpp:178-185); `Dynamic`'s builders are `when`, `sized`, `based_at` as free functions and `and_when`, `and_sized` as members, so the first call and the chained calls have different names for the same control (dynamic.hpp:119-268; vocabulary is another auditor's scope, noted because every `add(Dynamic, ...)` call site shows it).
Change: fold into the naming decision of GRAPHAPI-03; constant naming is already known (C16).
Breaks API: source-compatible with deprecations. Effort: S.

### Not checked

- I read graph.hpp, dynamic_graph.hpp, region.hpp, run_handle.hpp, the folder README, REMAINING_WORK.md, quickstart.cpp, `api/vocabulary/dynamic.hpp`, `dependencies.hpp`, `captures.hpp` and `api/detail/callable_traits.hpp` in full.
- docs/CONTRIBUTING.md: headings only. docs/architecture/ARCHITECTURE.md: headings, the first 45 lines of §3 and the "Compile policy" subsection; the other ~1 190 lines were not read. The prior audit report was grepped for the graph headers and its findings C1, C4, C6, C10, C15, C16, C26 were read; the rest was not.
- Tests and examples: `grep` of call sites in `examples/*.cpp`, test_fluent_api.cpp and test_dynamic_graph.cpp, and lines 40-125 of test_rule_registration.cpp. No test file was read in full.
- Not read: `api/detail/step_machinery.hpp`, `fixed_order_reduce.hpp`, `fixed_order_scan.hpp`, `device_fault.hpp`, `run_report_builder.hpp`, `api/runtime/runtime.hpp` beyond its include list and factory lines, `graph/task_graph.hpp` beyond a grep, anything under `src/` except `TaskGraph::name_last_node`.
- No benchmark was run and nothing was built. Findings 13, 16, 17, 18 and 19 carry cost claims that are hypotheses; each names the benchmark that would settle it. The parse-time numbers are syntax-only front-end times on one machine, not full compile times; code generation for instantiated kernels is not in them.
- The moved-from `Graph` behaviour (GRAPHAPI-11, first bullet) and the dangling `name_last` pointer (GRAPHAPI-12) are read from the code, not reproduced.
- README.md:850 says "Exactly one step may be in flight per runtime"; the guard I read is per graph (`async_step_pending_`). Whether the engine enforces the per-runtime rule lives in `execution_engine`, which I did not read.
- Whether a mistyped callable really produces the diagnostics described in GRAPHAPI-02 was not compiled; the claim rests on the absence of any `static_assert` and on the constraints as written.
- Whether the library target is built with SYCL device targets, which decides if kernels can move to `src/` (GRAPHAPI-14), was not checked.
- Whether `docs/getting_started` or `docs/guides` already hold a "which add" table (GRAPHAPI-21) was not checked; those folders are outside this scope.
- `Core::LivenessObserver` equality or identity, needed for the de-duplication in GRAPHAPI-16, was not read.

## Public API outside the graph folder (API)

Read-only. Nothing was written in the repository or the scratchpad. All paths are under `D:\Projects\sushiruntime`.

Dispatch block evidence:
- Item 1 (SOLID): judged per finding; each proposal names one seam and keeps siblings the same shape.
- Item 2 (humanizer): skill loaded and applied to this report.
- Item 6: No source written; nothing to syntax-check.
- Item 7: I created no scratch files. `du -sh` on the scratchpad reports 9.0K, which was there before I started.
- `git status --short` at the end: ` M docs/reference/CHANGELOG.md`, ` M include/SushiRuntime/api/runtime/runtime.hpp`, the same two files as at the start.

### Facts

#### Entry-point map (from `#include` to a finished run)

| Step | Type the user names | Declared at | Owns / borrows |
| --- | --- | --- | --- |
| include | `SushiRuntime/SushiRuntime.h` (every example) or `api/fluent.hpp` | `SushiRuntime.h:30-64`, `api/fluent.hpp:31-46` | n/a |
| create | `Runtime::create(const Topology::HardwareFilterConfig& = {}, const RuntimeConfig& = {})` | `api/runtime/runtime.hpp:151-152` | `Runtime` holds `std::shared_ptr<Execution::RuntimeContext>` (`:248`) |
| data | `rt.buffer<T>(count, DeviceIndex{0}, Residency::Shared)`, `rt.state<T>(...)` | `runtime.hpp:175-192` | `Buffer` holds its own `shared_ptr` to the context (`api/data/buffer.hpp:426`); `State` is two Buffers (`state.hpp:207-208`) |
| graph | `rt.graph()`, `rt.dynamic_graph()` | `runtime.hpp:162,168` | `Graph` and `DynamicGraph` take the `shared_ptr` (`graph.hpp:2100`, `dynamic_graph.hpp:414`) |
| declare access | `In/Out/InOut`, `Reads/Writes`, `Extent`, `NdRange`, `Dynamic` + `when/sized/...` | `api/vocabulary/*.hpp`, `api/launch/*.hpp` | captures copy the raw pointer and a `Core::LivenessObserver` |
| run | `run()`, `try_run()` → `Core::RunReport`, `Core::Result`, `Core::SushiException` | graph scope | n/a |
| inspect | `rt.advanced()` → `AdvancedControls` | `runtime.hpp:60-133,209` | holds `Execution::RuntimeContext&` (`:132`), the only handle that really borrows |
| escape | `rt.unsafe_context()`, `rt.interop()`, `rt.cluster(...)` | `runtime.hpp:215,222,232` | `Interop` and `Cluster` take the `shared_ptr` |

Lifetime rules as coded: every handle except `AdvancedControls` shares ownership of the context. `Graph::add` records a `Buffer`/`State` by address, so a recorded handle must not move.

#### Configuration seams

- Constructor arguments: two positional structs on `Runtime::create` (`runtime.hpp:151-152`): `Topology::HardwareFilterConfig` (`topology/hw_discovery.hpp:72-83`, four bools) and `RuntimeConfig`, an alias of `Execution::RuntimeSettings` (`runtime.hpp:50`; fields at `execution/runtime_settings.hpp:51-93`: `profiling`, `worker_count`, `first_core`, `pinning`, `rebalancer`, `force_scheduler`).
- Post-construction setters: `Runtime::rebalancer(bool)` (`runtime.hpp:199`), `Cluster::liveness_timeout`, `Cluster::task_deadline` (`cluster.hpp:72,79`), `Graph::set_profiling_requested` (graph scope).
- Environment variables: none. `grep -rnE "getenv|_dupenv_s|GetEnvironmentVariable" include src` returns no match.
- Compile definitions read by public headers (`grep` of `#if/#ifdef` under `include/`):
  - `SR_DISTRIBUTED`, 20 sites; in my scope: `api/data/buffer.hpp:382`, `api/data/state.hpp:184`, `api/detail/run_report_builder.hpp:80`, `api/runtime/cluster.hpp:44`, `api/runtime/runtime.hpp:224` (plus `api/graph/dynamic_graph.hpp:927`). Set PUBLIC at `src/CMakeLists.txt:109`.
  - `SR_ENABLE_LOGGING`: `core/logger.hpp:44,64,299,316`; defaults to 1 at `:45`; no CMake file or doc under `cmake/`, `cli/`, `docs/` mentions it.
  - `NDEBUG`: picks the log level at `core/logger.hpp:65-69` (INFO in release, DEBUG otherwise).
  - `SUSHIRUNTIME_EXPORTS`: `core/export.hpp:31`, PRIVATE at `src/CMakeLists.txt:78`.
  - `SYCL_EXT_ONEAPI_BINDLESS_IMAGES`: `interop/detail/bindless_detection.hpp:38`.
  - `SR_DETERMINISTIC_FP=1` is exported (`cmake/Determinism.cmake:104`) but no public header tests it.

#### Umbrella header (transitive closure, measured with an inline Python include walker; it ignores `#ifdef`, so the six `distributed/` headers count even when `SR_DISTRIBUTED` is off)

| Start header | Headers reached | Lines | Pulls `<sycl/sycl.hpp>` |
| --- | --- | --- | --- |
| `SushiRuntime.h` | 73 of 85 | 16,161 | yes |
| `api/fluent.hpp` | 63 | 14,203 | yes |
| `api/runtime/runtime.hpp` | 59 | 12,826 | yes |
| `api/data/buffer.hpp` | 34 | 6,155 | yes |
| `api/launch/extent.hpp` | 1 | 116 | yes |
| `version.hpp` | 2 | 131 | no |

Headers the umbrella adds over `fluent.hpp`: `version.hpp`, `core/event_timestamps.hpp`, `graph/dependency_tracker.hpp`, `memory/epoch_reclamation.hpp`, `scheduler/event_polling.hpp`, `scheduler/rebalancer_interfaces.hpp`, `scheduler/task_scheduler.hpp`, `topology/affinity.hpp`, `topology/numa_mapper.hpp`. `fluent.hpp` does not include `version.hpp`.

#### Examples (checked by reading against the headers, not built)

- `quickstart.cpp`, `vecadd_demo.cpp`, `stencil_demo.cpp`, `coupled_fields_demo.cpp`, `game_engine_demo.cpp`, `streaming_world_demo.cpp`: every call matches a current declaration (`Runtime::create`, `state<T>`/`buffer<T>`, `Graph::add(State&, F)` at `graph.hpp:536`, `add(State&, Extent<N>, F)` at `:570`, coupled `add` at `:606`, `add(Extent, Cap0, ...)` at `:517`, `Reads()`/`Writes(tile[k])`, `run(n)` returning `Core::RunReport` with `total_duration_ms`, `compile_count()`, `region_count()`, `drop`). I found no mismatch. I did not trace `Region::add` overload resolution to the end.
- `distributed_demo.cpp` compiles against the lower layers, not the fluent API (see API-13).
- `examples/README.md:3` says seven programs; seven `.cpp` files exist.
- `grep` of `examples/` for `try_run`, `version_matches`, `RuntimeConfig`, `Residency::Device`, `advanced()` returns nothing.

#### Other measured facts

- `[[nodiscard]]` count in every header of my scope: 0 (`grep -rc nodiscard`).
- `SR_THROW_IF` throws `std::runtime_error`, not `SushiException` (`core/logger.hpp:352-358`).

#### Already known, not repeated as findings

- No non-throwing `Runtime::try_create`: backlog `docs/design/REMAINING_WORK.md`, "Backbone, BB-5 to BB-8".
- A `Buffer`/`State` moved after `Graph::add` dangles silently: backlog, "Correctness".
- Host read of `Residency::Shared` during a run on Windows CUDA: backlog, "Correctness".
- Doxygen conformance sweep over `include/SushiRuntime/`: backlog, "Before the v1.0.0 freeze" (API-18 lists only the entries on this surface).
- `sycl/sycl.hpp` in 19 public headers: prior audit `REPORT.md:1034-1038`.
- `malloc_residency_on` returning `nullptr` as a sentinel: prior audit `REPORT.md:1106-1108`.
- Umbrella header name versus the sibling layout: backlog "Repository hygiene" and prior audit `REPORT.md:841-845` (the rename only; its contents are API-10 below).
- `SR_DISTRIBUTED` must stay PUBLIC: reasoned in `src/CMakeLists.txt:83-107` (the missing mismatch check is API-09).
- The backlog item "No area README" is stale for my scope: `examples/README.md` and every `include/SushiRuntime/api/*/README.md` exist.

### Findings

Ranked by how much each helps a user or an operator.

#### API-01. Two positional config structs, both from internal layers
- Severity: high. Category: api.
- Evidence: `runtime.hpp:151-152` `create(const Topology::HardwareFilterConfig& config = {}, const RuntimeConfig& runtime_config = {})`; `runtime.hpp:50` `using RuntimeConfig = Execution::RuntimeSettings;`. Setting a worker count means writing `Runtime::create({}, config)`. The parameter named `config` is the device filter, and `RuntimeConfig` holds no device choice. `docs/getting_started/INTRODUCTION.md:351` spells it `API::RuntimeConfig` although `fluent.hpp:52` promotes it.
- Change: one options struct owned by `api/runtime` with two named members, for example `RuntimeConfig{ devices, workers/profiling/... }`, and `create(const RuntimeConfig& = {})`. The API struct converts to the two lower-layer structs in one private function, so the lower layers keep their own types. Keep the two-argument overload as `[[deprecated]]`, forwarding.
- Breaks API: source-compatible with a deprecation. Effort: M.

#### API-02. Misuse of a device index surfaces as `std::runtime_error` with no index in the message
- Severity: high. Category: api.
- Evidence: `rt.buffer<float>(n, DeviceIndex{7})` reaches `RuntimeContext::get_queue` (`src/execution/runtime_context.cpp:387`) and `rt.advanced().device_is_gpu(7)` reaches `:489`; both are `SR_THROW_IF(..., "Queue index out of bounds.")`, which throws `std::runtime_error` (`core/logger.hpp:357`). Every other misuse on this surface throws `SushiException` with an `Errc` (`buffer.hpp:161,187,410`, `dynamic.hpp:57,63`). `try_run`-style code that inspects `Errc` cannot classify this one, and `runtime.hpp:79-84` documents no throw on `device_is_gpu`.
- Change: validate `DeviceIndex` once in the API layer (one helper used by `buffer`, `state` and `device_is_gpu`) and throw `SushiException{Errc::invalid_access, "DeviceIndex 7 is out of range: this runtime drives 2 devices; see advanced().device_count()"}`. Add the `@throws`.
- Breaks API: no (`SushiException` is a `std::runtime_error`). Effort: S.

#### API-03. Lifetime rules in the docs contradict the ownership in the code
- Severity: high. Category: documentation (and api for `AdvancedControls`).
- Evidence: `runtime.hpp:139-140` "graphs borrow it, so the Runtime must outlive every graph it produces"; `:172` "@pre The buffer does not outlive the Runtime"; `:184` the same for State; `api/runtime/README.md:52-53`. In the code `Buffer`, `Graph`, `DynamicGraph`, `Interop` and `Cluster` each hold a `shared_ptr` to the context (`buffer.hpp:426`, `graph.hpp:2100`, `dynamic_graph.hpp:414`, `interop/interop.hpp:100`, `cluster.hpp:98`). Only `AdvancedControls` holds a bare reference (`runtime.hpp:132`).
- Change: pick one rule and make all siblings follow it. The code already implements "handles keep the context alive", so the cheaper path is to give `AdvancedControls` the `shared_ptr` like its siblings and rewrite the four doc passages to state the one remaining rule (a recorded handle must not move, and must outlive the graph that recorded it). I did not verify that tearing the context down after the `Runtime` object is gone is safe in every path; that check belongs to the execution auditor before the docs are changed.
- Breaks API: no. Effort: S.

#### API-04. Internal-layer types the user must name on the ordinary path
- Severity: high. Category: api.
- Evidence: `Topology::HardwareFilterConfig` (`runtime.hpp:151`); `Execution::WorkerPoolInfo` (`runtime.hpp:106`); `Execution::RuntimeContext` and `Memory::Allocation` in the public `Buffer` constructors (`buffer.hpp:147,176`) and `State` constructor (`state.hpp:77`); `Core::ResourceRegion` from `Buffer::region` (`buffer.hpp:371`); `Core::LivenessObserver` from `liveness()` (`buffer.hpp:363`, `state.hpp:126`); `sycl::range`/`sycl::id` as public fields of `Extent` and `NdRange` (`extent.hpp:50-51`, `nd_range.hpp:50-51`); `Distributed::ITransport`, `KernelRegistry`, `IOffloadPolicy` and `sushi_ptr` in `Runtime::cluster` (`runtime.hpp:232-234`). `fluent.hpp:60-63` promotes four `Core` names; the rest are reached by their layer namespace.
- Change: alias each of these into `API` next to its user (`API::DeviceFilter`, `API::WorkerPool`, `API::BufferRegion`), so user code spells one namespace; make the context-taking `Buffer`/`State` constructors private with `Runtime` as friend, since `Runtime::buffer()` is the documented factory. SYCL types in `Extent` are a deliberate dependency (prior audit 1034); I propose no change there.
- Breaks API: source-compatible with a deprecation for the constructors; aliases are additive. Effort: M.

#### API-05. Range calls clamp and no-op silently
- Severity: medium. Category: api.
- Evidence: `buffer.hpp:315-316` (`read_range`), `:339-342` (`write_range`), `:375-376` (`region`): an offset past the end yields an empty vector, a zero-byte write or a zero-length region with no report. `write_range(ElementRange, const T* src)` takes a pointer with no length (`:335`) and a null `src` is skipped (`:342`). A zero-length `ResourceRegion` passed to `Writes(...)` orders nothing, so a wrong offset becomes a missing dependency.
- Change: throw `SushiException{invalid_access}` naming offset, count and size when the window does not fit; add `write_range(ElementRange, span<const T>)` and deprecate the pointer form; keep `State` forwarding identical. Document the new `@throws`.
- Breaks API: yes for callers that rely on clamping (behaviour change); the pointer overload is source-compatible with a deprecation. Effort: S.

#### API-06. No runtime control over logging; `Runtime::create()` prints to stdout
- Severity: medium. Category: api (operability).
- Evidence: the level is a compile-time constant (`core/logger.hpp:64-72`), INFO under `NDEBUG`; context construction logs one INFO line per device (`src/execution/runtime_context.cpp:342`) to stdout (`logger.hpp:109`). `SR_ENABLE_LOGGING` appears in no CMake file or doc. `ACTIVE_LOG_LEVEL` is `inline constexpr` with a value that depends on the including TU's `NDEBUG` (`:65-69`), so a debug consumer and the release library define the same inline variable differently.
- Change: a runtime level and sink owned by the logger and set through `RuntimeConfig` (default WARN), with the compile-time switch kept only as an upper bound. This crosses into the `core` auditor's scope; I report it because it is what an operator sees first.
- Breaks API: no. Effort: M.

#### API-07. `version_matches()` exists and nothing on the entry path calls it
- Severity: medium. Category: api (operability).
- Evidence: `version.hpp:86-91`; `include/SushiRuntime/README.md:41` "the check a consumer should make once at startup". No example calls it; `fluent.hpp` does not include `version.hpp`; `Runtime::create` (`runtime.hpp:151-156`) does not check.
- Change: `Runtime::create` is header-inline, so it can compare the consumer's header macros with `runtime_version()` and throw `Errc::unsupported` naming both versions and the usual cause (an older shared library earlier on the loader path). Include `version.hpp` from `runtime.hpp`.
- Breaks API: no. Effort: S.

#### API-08. `Buffer` and `State` are siblings with different shapes, and both expose tracker plumbing
- Severity: medium. Category: api.
- Evidence: `Buffer` has `data()`, `device_index()`, `region()`, `dependency_key()` (`buffer.hpp:247,265,371,356`); `State` has `current()`/`next()`, `read_key()`/`write_key()`, no `device_index()` and no `region()` (`state.hpp:89-119`). `State::advance()` is public with the note "The runtime calls this once per step" (`state.hpp:174-178`); a user call desynchronises the pair. `dependency_key`, `read_key`, `write_key` and `liveness` exist for `Graph`, not for the user.
- Change: add `State::device_index()`; move `advance`, the key getters and `liveness` behind one access struct in `api/detail` that `Graph`, `Reads`, `Writes` and the captures use, and deprecate the public spellings.
- Breaks API: source-compatible with a deprecation. Effort: M.

#### API-09. An `SR_DISTRIBUTED` mismatch between consumer and library is not diagnosed
- Severity: medium. Category: api (operability).
- Evidence: the macro adds members to `Buffer`, `State`, `Runtime` and the whole `Cluster` class in consumer-compiled headers (sites listed under Facts). `src/CMakeLists.txt:83-107` explains why it is PUBLIC and that a mismatch size-deletes a differently defined object. A consumer who does not use the exported CMake target gets no error.
- Change: export a function such as `runtime_built_distributed()` beside `runtime_version()` and fold the comparison into the startup check of API-07; or reference a flag-specific exported symbol from the headers so a mismatch fails at link time.
- Breaks API: no. Effort: S.

#### API-10. The umbrella header includes the scheduler, EBR and tracker internals
- Severity: medium. Category: layout.
- Evidence: `SushiRuntime.h:40-61` includes `topology/numa_mapper.hpp`, `topology/affinity.hpp`, `memory/epoch_reclamation.hpp`, `graph/dependency_tracker.hpp`, `scheduler/task_scheduler.hpp`, `scheduler/event_polling.hpp`, `scheduler/rebalancer_interfaces.hpp`; `:63` calls the fluent API "the surface most users program against". Every example, `README.md:256`, `INTRODUCTION.md:85` and `tests/package/main.cpp:32` include the umbrella. Counts are in Facts.
- Change: `SushiRuntime.h` = `version.hpp` + `api/fluent.hpp`. Users of lower layers include those headers by path. A finer set for callers who want it: `api/runtime/runtime.hpp` (already enough for Runtime, Graph, Buffer, State), plus one small aggregate each for `api/vocabulary/` and `api/launch/`. `buffer.hpp` reaching 34 headers comes from including `execution/runtime_context.hpp` for a `shared_ptr` member; whether a forward declaration plus out-of-line transfer calls can cut that is worth a compile-time measurement (`-ftime-trace`, before and after) before any change. I measured header counts only, not compile time.
- Breaks API: yes for code that relies on the umbrella for internals (pre-1.0, changelog entry with `!`). Effort: S for the umbrella, M for the buffer include diet.

#### API-11. Name promotion into `SushiRuntime::` follows three different rules
- Severity: medium. Category: api.
- Evidence: promoted in the leaf header: `Buffer`, `Residency`, `DeviceIndex`, `ElementRange` (`buffer.hpp:436-439`), `State` (`state.hpp:213`), `Extent`, `NdRange`, `Sum`, `Minimum`, `Maximum`. Promoted only in `fluent.hpp:51-59`: `Runtime`, `RuntimeConfig`, `RunHandle`, `Reads`, `Writes`, `In`, `Out`, `InOut`, `LateCount`. Never promoted: `Dynamic`, `when`, `sized`, `sized_from_device`, `based_at`, `based_at_device`, `PinPolicy`, `AdvancedControls`, `Cluster`, `DynamicGraph`, `Region`, `residency_name`. Including `api/runtime/runtime.hpp` alone gives `SushiRuntime::Buffer` and `API::Runtime`.
- Change: one rule: each leaf header promotes the public names it declares, and `fluent.hpp` promotes none. `when` and `sized` are short enough that the owner may prefer to keep them in `API`; that is a naming choice to ask about.
- Breaks API: no (additive). Effort: S.

#### API-12. `Runtime` constness and copy semantics are accidental
- Severity: medium. Category: api.
- Evidence: `interop()` is `const` and returns a facade that can mutate (`runtime.hpp:215`); `graph()`, `buffer()`, `state()`, `advanced()`, `unsafe_context()` are non-const (`:162,176,188,209,222`), so nothing can be allocated through a `const Runtime&`. No special members are declared, so `auto rt2 = rt;` compiles and silently shares one context. `Buffer::operator[]` and `host()` are `const` and return mutable access (`buffer.hpp:286,299`) while `write_range` is non-const (`:335`).
- Change: state handle (shallow-const) semantics once, in `api/README.md`, and apply them: the factories and `write_range` become `const`; `Runtime` either deletes copy or documents that a copy is a second handle to the same runtime.
- Breaks API: no for added `const`; deleting copy is breaking. Effort: S.

#### API-13. `distributed_demo.cpp` does not use the fluent distributed surface
- Severity: medium. Category: documentation.
- Evidence: `examples/distributed_demo.cpp:100,122` construct `RuntimeContext` directly; `:143-145` call `sycl::malloc_shared`; `:157-175` use `TaskGraph` and `ctx.get_execution_engine().execute(...)`; `:74,167` call `SushiRuntime::detail::enqueue_host_task`; `:132-133` hand-roll the wait that `Cluster::wait_for_workers` provides (`cluster.hpp:56`). `Runtime::cluster`, `Cluster`, `Buffer::distributed()` and `State::distributed()` have no example.
- Change: write the master side on `Runtime::cluster(...)`, `rt.buffer<float>(n).distributed()` and the `Graph::add(OpID, ...)` overloads; keep the worker side, which has no fluent equivalent, and say so in `examples/README.md`.
- Breaks API: no. Effort: M.

#### API-14. What a newcomer does not find in `examples/`
- Severity: medium. Category: documentation.
- Evidence: no example handles an error (`try_run` or `catch (const SushiException&)`), passes a `RuntimeConfig`, uses `Residency::Device` with `read_range`/`write_range`, reads `advanced()`, or uses `Dynamic` (grep in Facts). `examples/CMakeLists.txt:46,51` use the in-tree helpers `sushiruntime_apply_build_policy` and `add_sycl_to_target`; the only `find_package(SushiRuntime)` consumer is `tests/package/CMakeLists.txt`. The kernel index type changes with the overload and no example says so: `std::size_t i` on the State path (`quickstart.cpp:50`), `sycl::id<1>` on the captures path even in 1-D (`vecadd_demo.cpp:48`), `std::size_t` on the region path (`streaming_world_demo.cpp:63`).
- `quickstart.cpp` itself is honest and short: one include, 15 statements, real output. Its only excess is the 73-header umbrella (API-10).
- Change: one more example that shows the operating path (config, `try_run`, the error message, a device-resident field read back with `read_range`, `advanced().worker_pool()`), and a "build your own program" section in `examples/README.md` that points at `tests/package/`. The index-type split is the graph auditor's to judge; the README should at least state it.
- Breaks API: no. Effort: M.

#### API-15. `Dynamic` exposes its storage, and its member setters are weaker than the free functions
- Severity: medium. Category: api.
- Evidence: public fields `when_pred`, `count_fn`, `device_count`, `device_count_index`, `base_fn`, `device_base`, `device_base_index` (`dynamic.hpp:89-115`). `sized_from_device(const Buffer<std::uint32_t>&, index = 0)` checks residency and index (`:285-292`, `:53-68`); `and_sized_from_device(const std::uint32_t* counter, std::size_t index)` takes a raw pointer, has no default index and checks nothing (`:170,186`); the same for `and_based_at_device` (`:229,245`). `Graph::require_host_slot` (`graph.hpp:2808`) re-checks residency at `add()`, not the index range. Lvalue-overload Doxygen at `:163-169` and `:222-228` omits `@param index` and `@return`.
- Change: the member setters take the `Buffer` and share `checked_device_slot` with the free functions; the fields become private with `Graph` as the reader.
- Breaks API: source-compatible with a deprecation. Effort: S.

#### API-16. `Buffer` allocation: unchecked size overflow and a message that guesses the cause
- Severity: medium. Category: api.
- Evidence: `buffer.hpp:156` `const std::size_t bytes = count * sizeof(T);` with no overflow test; the adopting constructor has one (`:185`). The failure message (`:163-164`) says "(out of USM memory)" without naming the device index or residency, and the same code path also fails for a device that does not support the requested USM kind.
- Change: share the overflow check between both constructors; message of the form "Buffer<T> of N elements (B bytes), Residency::Device on device 1: allocation failed" with the fix that applies (smaller count, another residency, another device).
- Breaks API: no. Effort: S.

#### API-17. Reference documentation that no longer matches the headers
- Severity: low. Category: documentation.
- Evidence and change (edit each passage):
  - `docs/architecture/ARCHITECTURE.md:237-238` and `:538-539` list six `Errc` values; `core/error.hpp:47-57` has nine (`invalid_access`, `dangling_resource`, `unsupported` missing).
  - `ARCHITECTURE.md:254-256` lists five promoted names; `fluent.hpp:51-63` promotes thirteen.
  - `ARCHITECTURE.md:91` shows `Runtime::create(config = {})`; the function takes two structs.
  - `api/data/README.md:28` "The memory is shared USM" contradicts the Residency section above it (`:12-19`).
  - `api/data/README.md:37-38` "A Buffer must not be constructed over foreign USM" while `buffer.hpp:176` is a public adopting constructor.
  - `api/runtime/README.md:32-33` lists what `AdvancedControls` reads and omits `device_is_gpu` (the uncommitted addition) and `worker_pool`.
- Breaks API: no. Effort: S.

#### API-18. Doxygen gaps on this surface (CONTRIBUTING §4: every parameter and the return)
- Severity: low. Category: documentation. The general sweep is already in the backlog; these are the entries here.
- Evidence:
  - `runtime.hpp:225-234` `cluster`: no `@param kernels`, no `@return`.
  - `runtime.hpp:145-152` `create`: no `@return`. `:170-180`, `:182-192` `buffer`/`state`: no `@tparam`, `@param`, `@return`, `@throws` (they throw `allocation_failed`).
  - `runtime.hpp:194-199` `rebalancer`: no `@param`, no `@return`.
  - `buffer.hpp:142-149` allocating constructor: no `@param`, no `@throws` although `:161` throws; `:169-177` adopting constructor documents only `allocation`.
  - `buffer.hpp:397` rvalue `distributed()`: no comment. `buffer.hpp:305-311` `read_range`: no `@return`.
  - `state.hpp:73-79` constructor: no `@param`, no `@throws`.
  - `cluster.hpp:52-56` `wait_for_workers`: no `@param`; `:96` constructor undocumented.
  - `version.hpp:70-74,82-86`: no `@return`. The usage example for `SUSHIRUNTIME_VERSION` sits on `SUSHIRUNTIME_VERSION_ENCODE` (`:43-50`).
  - `dependencies.hpp:45-62` and `captures.hpp:108-114`: `detail` helpers under plain `//` comments or one shared brief.
  - `extent.hpp:102-110`, `nd_range.hpp:77-78`, `captures.hpp:101-104`: deduction guides without Doxygen (a plain comment explains them; acceptable if the checker allows it).
- Breaks API: no. Effort: S.

#### API-19. Naming and type inconsistencies between siblings
- Severity: low. Category: api.
- Evidence:
  - Setters: `Runtime::rebalancer(bool)` (`runtime.hpp:199`) and `Cluster::liveness_timeout(t)` (`cluster.hpp:72`) are noun-named and chain; `Graph::set_profiling_requested(bool)` (`graph.hpp:1841`) is `set_`-named and returns `void`. Getter for the first is `AdvancedControls::rebalancer_enabled()` on a different object.
  - Counts: `Cluster::alive_workers()` returns `int` and `wait_for_workers(int n, ...)` takes `int` (`cluster.hpp:56,69`); every other count on the surface is `std::size_t`.
  - Enumerators: `Errc::invalid_access` snake_case (`error.hpp:47-57`), `Residency::Shared` PascalCase (`buffer.hpp:80-85`), `LogType::ERR` upper-case (`logger.hpp:57-63`).
  - Namespace casing: `API::detail` (`buffer.hpp:103`) and `Core::Detail` (`logger.hpp:355`).
  - Abbreviations in public fields against the "no abbreviated names" rule: `StateView::cur` (`state.hpp:53`), `Dynamic::when_pred`, `count_fn`, `base_fn` (`dynamic.hpp:89-104`), parameter `ctx` throughout.
  - Boolean queries without `is_`/`has_`: `host_accessible()`, `empty()`, `profiling_enabled()`, `rebalancer_enabled()` beside `device_is_gpu()`.
- Change: one convention per row, applied before the 1.0 freeze. `Errc` can carry both spellings for one release (two enumerators with the same value).
- Breaks API: source-compatible with a deprecation. Effort: M.

#### API-20. No `[[nodiscard]]` anywhere on this surface
- Severity: low. Category: api.
- Evidence: count 0 in all 18 headers of scope. Results that are easy to drop: `Runtime::create()`, `Runtime::buffer()`/`state()` (dropping frees the allocation at once), `Cluster::wait_for_workers()` (a `bool` status, `cluster.hpp:56`), `Buffer::read_range()`, `version_matches()`, `Buffer::region()`.
- Change: add the attribute to factories, status returns and queries.
- Breaks API: no (new warnings; errors under `-Werror` in consumer code that drops a result). Effort: S.

#### API-21. Compute vocabulary sits in the orchestration API
- Severity: low. Category: layout. Raised for the owner's decision; I propose no feature.
- Evidence: `api/vocabulary/combine.hpp:41-101` (`Sum`, `Minimum`, `Maximum`), `api/detail/fixed_order_reduce.hpp` (151 lines), `api/detail/fixed_order_scan.hpp` (183 lines), documented as API at `ARCHITECTURE.md:119-132`. The project rule for this audit says the runtime owns no reductions.
- Change: either record the exception in `ARCHITECTURE.md` (a fixed-order fold as an ordering primitive the tracker needs to own), or move these three headers and the `add_reduce`/`add_prefix_scan`/`add_counting_sort` builders to the layer above. The graph auditor owns the builder side.
- Breaks API: yes if moved. Effort: L.

#### API-22. `Cluster::wait_for_workers` polls on a fixed 5 ms sleep
- Severity: low. Category: performance.
- Evidence: `cluster.hpp:58-64`, a `sleep_for(5ms)` loop over `distributed_alive_workers()`. It runs once at startup, so the cost is latency of at most 5 ms, not steady-state load. Not measured.
- Change: none needed for speed; if the distributed layer gains a registration condition variable, wait on it. Listed so nobody "optimises" it without a number.
- Breaks API: no. Effort: S.

### Not checked

- Nothing was built or run; "compiles against the current API" is by reading declarations, and I did not follow SFINAE resolution inside `graph.hpp`/`region.hpp` beyond the overload signatures.
- `api/detail/step_machinery.hpp`, `run_report_builder.hpp`, `callable_traits.hpp`, `fixed_order_reduce.hpp`, `fixed_order_scan.hpp`: I read their README sections and grepped them, but did not read the bodies line by line. `device_fault.hpp` was read.
- `docs/agent/2026_10_05_ESTATE_AUDIT/REPORT.md` was grepped for 24 terms, not read whole. `docs/architecture/ARCHITECTURE.md` was read at §3, §5 (logger and errors) and §14; `docs/CONTRIBUTING.md` at §4. The project's instruction to read every `.md` under `docs/` was not followed in full.
- `docs/getting_started/` and `docs/guides/` were only grepped for include lines and a few API names; whether their samples compile is the backlog item "Unverified manual content".
- Whether destroying a `Runtime` while buffers or graphs still hold the context is safe at teardown (API-03) was inferred from the `shared_ptr` members, not traced through `RuntimeContext::~RuntimeContext`.
- Whether a `sycl::exception` from `q.memcpy(...).wait()` in `read_range`/`write_range` is translated anywhere before it reaches the user.
- The Python `cli/` and any environment variables it reads are outside my scope; the "no environment variables" fact covers `include/` and `src/` only.
- No compile-time or run-time measurement was taken; API-10 and API-22 say so.
- An automated scan for undocumented functions failed on shell quoting and was not rerun; API-18 is from reading.

## Scheduler, graph, memory, core (CORE)

Read-only. Nothing in the repository was written. Every performance finding is a hypothesis until the named benchmark is run; I ran no build, no test and no benchmark.

### Facts

#### What was read

Read in full: `src/scheduler/scheduler.cpp`, `include/SushiRuntime/scheduler/{task_scheduler,event_polling,task,work_stealing_deque,ready_sink,workload_coordinator,thermal_proxy_strategy}.hpp`, `include/SushiRuntime/graph/{node,dependency_tracker,late_bindings,task_types}.hpp`, `src/graph/{task_graph,dependency_tracker}.cpp`, `include/SushiRuntime/memory/epoch_reclamation.hpp`, `include/SushiRuntime/core/{logger,ref_counted,common}.hpp`, the seven module READMEs in scope, `docs/design/REMAINING_WORK.md`, and the "Code shape" findings C1, C2, C11, C12, C13, C17, C18, C24, C26 of the prior audit.

#### Build the numbers refer to

`build/compile_commands.json`, entry for `src/scheduler/scheduler.cpp`: `clang++.exe ... -fsycl -O3 -DNDEBUG -std=gnu++17 ... -ftls-model=initial-exec`. `SR_DISTRIBUTED` is not defined in this build. With `NDEBUG`, `ACTIVE_LOG_LEVEL` is `INFO` (`core/logger.hpp:65-66`), so `SR_LOG_DEBUG` compiles out.

#### Measured sizes and offsets

Method: a scratch file with `template <int Tag, std::size_t N> struct Show;` instantiated on `sizeof`/`offsetof`, compiled with the flags above and `-fsyntax-only`; the sizes are read from the "undefined template" errors. MSVC STL ABI, standalone build.

```
clang++.exe -fsyntax-only -DNOMINMAX -DSR_DETERMINISTIC_FP=1 -DWIN32_LEAN_AND_MEAN -ID:/Projects/sushiruntime/include
  -isystem D:/Projects/sushistack/dependencies/vcpkg/installed/x64-windows/include -fsycl -O3 -DNDEBUG -std=gnu++17
  -D_DLL -D_MT -Wno-invalid-offsetof -ferror-limit=0 sizes.cpp 2>&1 | grep -o "Show<[0-9]*, [0-9]*>"
sizeof:  Node 800 | Task 24 | TaskMetadata 88 | SmallEventVector 160 | RegionLink 40 | TaskWork(std::function) 64
         PolledEvent 40 | ActiveEventList 10288 | TaskScheduler 45824 | Deque 192 | EpochManager 18560
         DependencyTracker 65536 | sycl::event 16
Node offsets: metadata 24 | work 112 | (host_work 176) | native_work 240 | mode 304 | in_degree 312 | region 320
         successor_list 360 | inline_successor_count 368 | inline_successors 376 | sycl_deps 504 | is_sentinel 664
         io_reads_ 672 | io_writes_ 696 | locality_checked_ 720 | cached_queue_index_ 724 | bindings_ 728
         baked_width 752 | registration_index 760 | profiling 768 | ext_ 792
TaskScheduler offsets: worker_queues 24 | drain_scratch_ 2072 | pending_submissions 8216 | pending_mutex 8280
         pending_cv 8360 | pending_count_ 8432 | priority_count_ 8440 | running 8448 | shutdown_requested 8449
         run_failed_ 8451 | run_cancelled_ 8552 | event_poller 8560 | worker_completed_tasks 12736
         profiling_active_ 45504 | active_ledger_ 45512 | current_dispatch_step_ 45524 | inflight_ 45528
         quiesce_mutex_ 45536 | worker_numa_ 45688 | victim_order_ 45712 | local_tier_len_ 45736
         local_steals_ 45760 | remote_steals_ 45768
```

What the layout says:

- `Node` is 800 bytes, 12.5 cache lines. A native node's dispatch and completion read bytes at 0-48 (vptr, refcount, `numa_affinity`), 240 (`native_work`), 304-316 (`mode`, `submits_device_work_`, `in_degree`), 336-376 (boundary successors, spill head, successor count), 376+ (inline successors) and 728 (`bindings_`): six or seven lines. A device node adds 112 (`work`), 504 (`sycl_deps.static_count`), 696 (`io_writes_`) and 720-724.
- Three `std::function` members take 192 bytes; a node uses one. `metadata.params` takes 64 bytes and nothing in scope reads it on dispatch. `NodeProfiling` (24 bytes, three atomics) has no reader and no writer anywhere in `include/`, `src/` or `tests/` (`grep -rn "NodeProfiling\|node->profiling\|n->profiling"` returns only its declaration and its README section).
- In `TaskScheduler`, bytes 45504-45567 are one cache line that holds `profiling_active_`, `active_ledger_`, both dispatch-step counters, `inflight_` and the head of `quiesce_mutex_`. Every worker reads the first three per dispatch and writes `inflight_` at least twice per node.
- `local_steals_` and `remote_steals_` share the line at 45760; every successful steal by any worker writes it.
- `drain_scratch_` is 256 vector headers of 24 bytes, packed 2.67 to a line, each written by a different worker.
- `TaskScheduler` is 45,824 bytes because every per-worker array is sized for `MAX_WORKER_THREADS = 256` (`core/common.hpp:60`).
- A `Deque` is three lines (`items`+refcount, `top`, `bottom`+`overflow_`), plus a ring of 262,144 atomic pointers, 2 MiB per worker, faulted lazily (`work_stealing_deque.hpp:50, 71`).
- A `DependencyTracker` is 64 KiB (1024 cache-line-aligned shards, `dependency_tracker.hpp:85, 92`) and one lives in every `TaskGraph::Impl` (`task_graph.cpp:84`).

#### The per-node path, submit to completion

Per step, fixed cost (engine side is outside my scope; listed because it calls into the scheduler):

| Step | Where | Cost |
| --- | --- | --- |
| arm | `scheduler.cpp:224-243` | `run_error_mutex_` lock, `pending_mutex` lock, `notify_all` |
| submit roots | `scheduler.cpp:921-955` | one `pending_mutex` lock; per root one `add_ref` (atomic RMW) and one virtual `get_priority()`; `notify_all` |
| sentinel re-arm | `src/execution/execution_engine.cpp:453` | a second full `arm_for_execution()` on a worker: both locks and another `notify_all`, while `running` is already true |
| wait | `scheduler.cpp:964-971` | cv wait, no timeout |
| disarm | `scheduler.cpp:253-259` | `pending_mutex` lock; every worker then parks on `pending_cv` |

Per node, on the worker:

| Stage | file:line | What it costs |
| --- | --- | --- |
| loop top | `scheduler.cpp:412-414` | three acquire loads (free on x86, real on ARM) |
| injector check | `scheduler.cpp:342-347` | two relaxed loads on the own deque, one on `pending_count_` |
| drain (roots only) | `scheduler.cpp:355-371` | `pending_mutex`; the first worker takes up to 4096 tasks |
| epoch guard | `scheduler.cpp:440`, `epoch_reclamation.hpp:272-285, 360-361` | two `instance()` static-guard checks, two TLS reads, a load of `global_epoch_`, a release store, a `seq_cst` fence, a release store on exit; paid on every loop iteration, idle ones included |
| poll | `event_polling.hpp:286-325` | per in-flight event one `get_info` driver call; on completion one `event.wait()` |
| priority check | `scheduler.cpp:461` | one relaxed load; `pending_mutex` only if non-zero |
| pop | `work_stealing_deque.hpp:124-154` | `seq_cst` fence, CAS on the last element (inherent to Chase-Lev) |
| steal (if empty) | `scheduler.cpp:836-872` | N-1 victims, two relaxed loads each; on a hit a fence, a `seq_cst` CAS and a `fetch_add` on the shared steal counter |
| split check | `scheduler.cpp:490` | virtual `is_splittable()` |
| execute_task entry | `scheduler.cpp:559-585` | two acquire loads, virtual `is_graph_node()` at 572 and again at 595, `fetch_add` on the worker's own counter |
| native node | `scheduler.cpp:604-643` | virtual `get_execution_mode()`, virtual `submits_device_work()`, acquire load of `profiling_active_`, virtual `execute_native()`, one `std::function` call (`node.hpp:530`), a default-constructed `sycl::event` and a `PolledEvent` copy at 643 |
| device node | `scheduler.cpp:648-762` | first dispatch only: `context_index_for_ptr` per region, which takes the registry lock; then `dispatch_queue()` (acquire load plus a call into `RuntimeContext`), a default `sycl::event` at 705, `q.submit`, `depends_on` per dependency, virtual `execute()`, one `std::function` call (`node.hpp:533`), a second default `sycl::event` for the comparison at 756, acquire loads at 723, 758, 762, a `PolledEvent` copy at 761 |
| HOST-mode device node | `scheduler.cpp:709-716` | one `std::vector<sycl::event>` allocation per dispatch (the README states it) |
| uncached queue | `scheduler.cpp:693-695` | `get_queue_for_ptr`, a process-global mutex per dispatch (`graph/README.md:149-151`); only for pointers the context did not allocate |
| reap | `event_polling.hpp:296-319` | `accumulate_profiling` if armed; TLS `ReadySink`; virtual `on_complete()`; per successor an `acq_rel` `fetch_sub` on `in_degree`; per freed successor `add_ref`, a `fetch_add` on the shared `inflight_`, a deque push; then `release()` (atomic RMW), a `fetch_sub` on `inflight_`, a `PolledEvent` move in `remove_at` |

Steady state takes no lock for a non-root, non-priority node with a cached queue index. There is no hashed container lookup on that path. `shared_ptr` copies: each `PolledEvent` copy or move copies or moves the event handle (`event_polling.hpp:130-136, 152-153`); the push at `scheduler.cpp:761` makes two copies of a live handle.

#### Where threads wait, sleep or spin

| Thread | file:line | Behaviour |
| --- | --- | --- |
| worker, pool disarmed | `scheduler.cpp:417-433` | parked on `pending_cv`, no timeout |
| worker, armed, no task, no in-flight event | `scheduler.cpp:535-541` | 100 `yield()` iterations, then `sleep_for(100 µs)` in a loop; each iteration runs the epoch guard and a full victim sweep |
| worker, armed, holding an in-flight event | `scheduler.cpp:538-539`, `event_polling.hpp:274-281` | never sleeps: yields until the event completes; polls are skipped for up to 64 iterations between checks |
| worker, 32 polls with no progress | `event_polling.hpp:338-349` | blocks in `event.wait()` on its oldest event, silently |
| caller | `scheduler.cpp:964-971` | cv wait, no timeout |
| rebalancer, only when `settings.rebalancer` | `workload_coordinator.hpp:70` | `sleep_for(5 ms)` loop; off by default (`execution_engine.cpp:150`) |
| logger | `logger.hpp:96-100` | cv wait |

A push to a worker's own deque notifies nobody (`event_polling.hpp:416`); a sleeping peer finds the work at its next 100 µs wake-up.

#### Encapsulation

Includes of scope headers from outside their module (`grep -rn "#include <SushiRuntime/scheduler/\|.../graph/\|.../memory/epoch"`):

- `graph/node.hpp`: `api/graph/graph.hpp:69`, `api/graph/dynamic_graph.hpp:49`, `api/detail/step_machinery.hpp:41`, `execution/execution_engine.hpp:39`, `execution/chain_plan.hpp:36` and five sources.
- `scheduler/task_scheduler.hpp`, `workload_coordinator.hpp`, `thermal_proxy_strategy.hpp`: only `src/execution/execution_engine.cpp:39-41`, `src/execution/runtime_context.cpp:44` and `src/graph/task_graph.cpp:41` (which uses nothing from it).
- `scheduler/event_polling.hpp`, `work_stealing_deque.hpp`, `rebalancer_interfaces.hpp`, `graph/dependency_tracker.hpp`, `memory/epoch_reclamation.hpp`: no includer outside `scheduler/`, `graph/` and tests.
- The only scheduler header a public header reaches is `scheduler/task.hpp`, through `graph/node.hpp:41`.

#### Logging the operator gets from these layers

Levels `ERR`, `WARN`, `INFO`, `DEBUG`, fixed at compile time. Call sites in scope: scheduler 2 INFO, 3 WARN, 3 ERROR, 1 DEBUG; graph 1 INFO, 1 WARN, 2 ERROR, 6 DEBUG; poller 1 WARN; thermal 1 DEBUG. A disabled level costs nothing: `if constexpr` discards the call and its arguments (`logger.hpp:317-334`). An enabled message costs one `ostringstream` per argument, one for the line, a `localtime` call, a mutex, a cv notify and an `fflush` (`logger.hpp:150-158, 238-308`). `INFO` and `WARN` go to stdout, `ERR` to stderr (`logger.hpp:109`).

### Findings

Ranked. "Known" lines cite the backlog or the prior audit and are not counted.

#### CORE-01 [high, operability] A failing node is always logged as 'unnamed_task'

Evidence: `scheduler.cpp:766` logs `task->get_name()`. `Task::get_name()` returns `name_`, default `"unnamed_task"` (`task.hpp:63, 145`). `Node` does not override it and stores its label in `metadata.name` (`task_types.hpp:114`; set at `task_graph.cpp:478`). `set_name` has no caller in `include/` or `src/`. The co-location error at `scheduler.cpp:676-680` names no node at all.
Change: `Node::get_name()` returns `metadata.name`; delete the second name field on `Task` or make it the only one. Put the name and `registration_index` in the failure log and in the `SushiException` message at 676.
Breaks API: no. Effort: S.

#### CORE-02 [high, operability] A run that never finishes produces no output

Evidence: `wait_quiescent()` waits with no timeout (`scheduler.cpp:964-971`). The stall drive blocks a worker in `event.wait()` and logs only if that throws (`event_polling.hpp:338-349`). Nothing reports `inflight_`, the deque depths, the in-flight event count per worker or the node a worker waits on. `ActiveEventList` is private to the worker's stack (`scheduler.cpp:390`), so no other thread can see it.
Change: (1) a `WARN` in the stall drive naming the task, the worker and the poll count. (2) `TaskScheduler::snapshot()` returning a value struct: `inflight`, armed, failed and cancelled flags, and per worker the deque size, the overflow size and an in-flight count the worker publishes when it changes. (3) the caller-side wait uses `wait_for` with a report interval and logs the snapshot once per interval; the interval is a `RuntimeSettings` field, which is the owner's call. Workers keep their timeout-free park.
Breaks API: no, additive. Effort: M.

#### CORE-03 [high, performance] One shared counter is written at least twice per node, on a line every worker reads

Evidence: `add_inflight()` per freed successor and `drop_inflight()` per completion (`event_polling.hpp:214-217, 224-235, 415, 317`). `inflight_` sits at offset 45528, on the same line as `profiling_active_` (45504) and `current_dispatch_step_` (45524), which `execute_task` loads at `scheduler.cpp:608, 611, 698, 723, 758, 762`.
Change: apply the net delta once per completion: `freed - 1`. A chain node frees one successor, so it touches the counter zero times. Give `inflight_` its own aligned line and group the read-mostly flags on another.
Settles it: `test_chain_dispatch_perf`, `test_wide_fanout_perf`, `test_scheduler_dispatch_perf`.
Breaks API: no. Effort: S.

#### CORE-04 [high, performance] The dispatch loop enters an EBR critical section that protects nothing a worker reads

Evidence: `scheduler.cpp:440` opens an `EpochGuard` on every loop iteration. The only `retire()` call in the library is `dependency_tracker.cpp:53`, and the only structure reclaimed through EBR is the tracker's `AccessNode` chain, which `resolve()` walks under its own guard (`dependency_tracker.cpp:67`). No scheduler code dereferences an `AccessNode`. The guard costs a full fence per iteration (`epoch_reclamation.hpp:284`) and holds the worker's slot active, which only delays reclamation.
Change: remove the guard from `dispatch_loop`. A task that builds a graph on a worker still gets the guard inside `resolve()`. The nesting note at `memory/README.md:41-43` goes with it.
Settles it: `test_scheduler_dispatch_perf`, `test_chain_dispatch_perf`; safety through `tests/benchmark/stress/test_epoch_stress.cpp` and the TSan lane.
Breaks API: no. Effort: S.

#### CORE-05 [high, performance and operability] An armed pool spins on every core for the whole step

Evidence: `submit_batch` wakes all workers whatever the root count (`scheduler.cpp:954`). A worker with no task yields 100 times, then sleeps 100 µs in a loop, sweeping every victim each time (`scheduler.cpp:535-541, 836-872`). A worker holding a device event never sleeps (`scheduler.cpp:538`). A local push wakes no one (`event_polling.hpp:416`). Each step ends with every worker parking and the next begins by waking them all through one mutex. On Windows `sleep_for(100 µs)` is bound by the timer resolution, which I did not measure. Every idle sweep also reads the busy worker's `bottom` line, so that worker's next push or pop pays a cache miss.
Change: measure first. Then, as one brick (`IdlePolicy`, owned by the scheduler): wake `min(roots, workers)` threads; keep a count of sleepers and `notify_one` from a push when the count is non-zero (one relaxed load per push); sleepers wait on a cv instead of `sleep_for`; a bounded spin before parking at disarm so a frame loop does not pay a futex round trip per step. The spin budget is a setting, which is the owner's call.
Settles it: `test_wide_fanout_perf`, `test_chain_dispatch_perf`, `test_root_fanout_perf`; CPU time per step is measured by none of them, so that needs a new benchmark.
Breaks API: no, additive setting. Effort: L.

#### CORE-06 [medium, performance] Each dispatch default-constructs one or two `sycl::event` objects

Evidence: `scheduler.cpp:566, 578, 643, 772` (ready path), 705 and 756 (device path). `include/SushiRuntime/scheduler/README.md:19-21` says that construction "can heap-allocate in DPC++" and that `has_event` exists to avoid it in the poll loop. The push at 761 copies a live handle twice.
Change: `PolledEvent` holds `std::optional<sycl::event>`; a `push_ready(task)` overload constructs no event; the KERNEL path sets `has_event` without the comparison; the event is moved in. `remove_at` then resets the vacated slot, which today keeps the last completed event alive until the slot is reused (`event_polling.hpp:146-159`).
Settles it: nothing existing. `test_steady_state_allocation.cpp:67-74` counts only the driving thread, by design (`tests/functional/integration/README.md:1474-1476`). A worker-thread allocation count per node is the missing benchmark.
Breaks API: no (`EventPolling` is internal in practice, see CORE-12). Effort: S.

#### CORE-07 [medium, performance and layout] Node is 800 bytes with its hot fields spread over seven cache lines

Evidence: offsets above. 192 bytes of `std::function` for one used slot (`node.hpp:302-308`), 64 bytes of `params`, 24 dead bytes of `NodeProfiling` (`node.hpp:232-237, 394`), 128 bytes of inline successors between the successor count and the dispatch flags.
Change: order the struct hot first: vptr, refcount, `in_degree`, successor count, `mode`, the three bool flags, `cached_queue_index_`, `numa_affinity`, a raw `LateBindings*`, the spill head and the first six successors in two lines; everything else behind. Delete `NodeProfiling`. Collapsing the three work slots into one is a public-type decision (`TaskWork`, `HostWork` are public aliases) and is the owner's call.
Settles it: `test_chain_dispatch_perf`, `test_wide_fanout_perf`.
Breaks API: ABI yes (API templates create nodes inline); source-compatible if field names stay. Effort: M.

#### CORE-08 [medium, performance] Seven virtual calls per native node, two of them for a type nothing uses

Evidence: `is_splittable()` (`scheduler.cpp:490`), `is_graph_node()` twice (572, 595), `get_execution_mode()` (604), `submits_device_work()` (605), `execute_native()` (641), `on_complete()` (`event_polling.hpp:410`), plus `get_priority()` per root (`scheduler.cpp:934`) and per heap comparison (`task_scheduler.hpp:82`). `Node` is not `final` (`node.hpp:296`), so calls through `Node*` stay virtual. `RangeTask`, the only splittable task, is constructed only in `tests/functional/unit/test_range_task.cpp`.
Change: mark `Node` `final`; resolve the downcast once and pass `Node*` down; decide whether `RangeTask`, `split()` and `is_splittable()` stay (removing a public type is the owner's call).
Already known: prior audit C2 (remove the downcast and the `is_graph_node` tag).
Settles it: `test_scheduler_dispatch_perf`.
Breaks API: `final` no; removing `RangeTask` yes. Effort: S.

#### CORE-09 [medium, operability] The logger cannot be turned down, redirected or correlated at run time

Evidence: level is a compile-time constant (`logger.hpp:64-72`); no setting and no environment variable (`grep -rn "getenv\|SR_LOG_LEVEL" include src` is empty; `runtime_settings.hpp` has no log field). `INFO` and `WARN` go to the host program's stdout (`logger.hpp:109`). A line carries no thread or worker id and the build machine's absolute `__FILE__` (`logger.hpp:302-305`). `ERR` is queued like the rest, so the last lines before a crash are the ones lost. The logger state is function-local statics in inline header functions with no export macro (`logger.hpp:196-218`); on Windows the DLL and each executable therefore hold separate copies, so `SR_LOGGER_SET_SYNC_MODE(true)` in a test does not reach the library's own messages. I read that from the code and did not run it. The macros expand to a bare `if constexpr`, so `if (x) SR_LOG_WARN(...); else ...` binds the `else` to the macro (`logger.hpp:317-334`).
Change: one exported logger in `src/core/logger.cpp` behind three functions (`log_write`, `set_log_level`, `set_log_sink`); a relaxed level load ahead of formatting; `SR_LOG_LEVEL` read once at start; default stream stderr; `ERR` drains the queue and writes synchronously; thread id in the line; macros wrapped in `do { } while (0)`.
Already known: no categories, no TRACE or FATAL (C13); logger body in a header (C18).
Breaks API: no, macros unchanged. Effort: M.

#### CORE-10 [medium, performance and layout] Graph construction pays for concurrency no caller uses

Evidence: every caller builds on one thread: the API replays `specs_` in order (`api/graph/graph.hpp:276-446`), the dynamic graph builds one `BackingGraph` per region (`api/graph/dynamic_graph.hpp:673`), the worker agent one per remote task (`src/distributed/worker_agent.cpp:397`). The five `*_last_node` accessors are correct only on one thread (`src/graph/README.md`, "The last node added"). No test adds to one graph from two threads. Cost of the lock-free design per node: `NodeEntry` plus two `RawNodeEntry` allocations (`task_graph.cpp:155, 166, 177`), an `AccessNode` per access (`dependency_tracker.cpp:72`), an `unordered_map` entry in the cycle check (`task_graph.cpp:665-669`). Per graph: a 64 KiB tracker and 1024 atomic exchanges per `reset()` (`dependency_tracker.cpp:47-55`), run by `compile()` or `take_nodes()` and again by the destructor.
Change: the owner decides whether `TaskGraph` is a single-thread builder. If yes: a `std::vector<sushi_ptr<Node>>`, a small map from allocation base to its access history, roots and leaves found in the pass that already visits every node, and no EBR. `EpochManager` then has no production user (see CORE-04) and `memory/epoch_reclamation.hpp` can go. If no: track the touched shards so `reset()` visits those only.
Settles it: `test_graph_compile_perf`, `test_recompose_churn_perf`, `test_dynamic_graph_perf`, `test_distributed_offload_perf`.
Breaks API: the documented thread-safety contract changes; signatures do not. Effort: M to L.

#### CORE-11 [medium, layout] scheduler.cpp holds seven responsibilities; concrete split

Evidence: 1125 lines; `execute_task` alone is 220 (`scheduler.cpp:554-774`) and mixes abort handling, late-binding skip, host timing records, co-location validation, queue routing, the clock pairing and event hand-off.
Change, all under `src/scheduler/`:
- `task_scheduler.cpp`: constructor, `start`, `arm_for_execution`, `disarm`, `stop`, `submit`, `submit_batch`, `wait_quiescent`, run error and cancellation.
- `worker_loop.cpp`: `dispatch_loop`, `drain_pending_submissions`, `find_task_to_steal`.
- `steal_topology.{hpp,cpp}`: a `StealTopology` value built from the context (`build_steal_topology`, `worker_numa_`, `victim_order_`, `local_tier_len_`).
- `queue_router.{hpp,cpp}`: the co-location check and queue choice (`scheduler.cpp:648-703`, `dispatch_queue`, `queue_device_index`).
- `task_dispatch.cpp`: what is left of `execute_task`.
- `scheduler_telemetry.cpp`: worker times, counters, `migrate_tasks`.
- `offload_completion.cpp`: `set_coordinator`, `complete_offloaded_node`.
The successor walk is written three times: `Node::for_each_successor` (`node.hpp:502-521`), by hand at `scheduler.cpp:1095-1118` and at `task_graph.cpp:688-699`. The two copies call the first.
Already known: file name does not match its header (C17).
Breaks API: no. Effort: M.

#### CORE-12 [medium, layout and api] Eight installed headers have no includer outside their own module

Evidence: the Encapsulation facts above. `scheduler/{task_scheduler,event_polling,work_stealing_deque,workload_coordinator,thermal_proxy_strategy,rebalancer_interfaces}.hpp`, `graph/dependency_tracker.hpp` and `memory/epoch_reclamation.hpp` are reached only from `src/` and tests. `EpochManager::instance()` and its thread-local state are inline header statics (`epoch_reclamation.hpp:129-133, 259-263`), so on Windows a test executable exercises its own manager, not the library's (same mechanism as CORE-09; not run).
Change: move the eight to `src/<module>/` as private headers and give tests a private include path. `poll_events` and `trigger_successors` are templates over a queue type with one instantiation (`event_polling.hpp:264, 400`) and become plain functions in a `.cpp`. This is a public-surface decision and the owner's call. I did not verify which headers the install rule copies.
Already known: bodies in headers (C18); singletons (C24).
Breaks API: yes, headers leave the installed set; pre-1.0. Effort: M.

#### CORE-13 [medium, api] TaskGraph hands back nothing from add and offers five "last node" accessors instead

Evidence: `name_last_node`, `width_last_node`, `last_node_width`, `last_node_added`, `last_registration_index` (`task_graph.cpp:472-510, 557-561`); every `add_*` returns `void`. The four `add_*` bodies repeat the same block: resolve reads, resolve writes, store regions, fill offload lists (`task_graph.cpp:316-330, 381-395, 448-463, 579-592`).
Change: `add_*` return a `Node&`; the five accessors forward to it and are deprecated. One private `Impl::track_accesses(node, reads, writes)` replaces the four copies.
Breaks API: source-compatible with a deprecation. Effort: M.

#### CORE-14 [medium, api] TaskScheduler's public surface exposes internals and misnames two operations

Evidence: public mutable `num_workers` beside `get_worker_count()` (`task_scheduler.hpp:96, 433`); public `dispatch_loop` (322); `friend class TaskGraph` (92) that `task_graph.cpp` never uses; `get_completion_rate()` returns a running total, as its own README says (`include/SushiRuntime/scheduler/README.md:130-133`); `migrate_tasks` ignores `to_worker_id` (`scheduler.cpp:1011`); the test hook `steal_victim_order` is public (444); the header includes `execution/runtime_context.hpp` for one reference member (48, 199).
Change: make `num_workers` and `dispatch_loop` private; drop the friend and the unused include at `task_graph.cpp:41`; rename to `completed_task_count` and drop the unused parameter, each with a deprecated forwarder; forward-declare `RuntimeContext`.
Already known: upward include (C1).
Breaks API: source-compatible with deprecations. Effort: S.

#### CORE-15 [medium, performance and layout] Per-worker state lives in five parallel arrays sized for 256 workers

Evidence: `worker_queues`, `drain_scratch_`, `workers`, `worker_completed_tasks`, `worker_time_` (`task_scheduler.hpp:113, 118, 219, 228, 245`); `sizeof(TaskScheduler)` is 45,824. `drain_scratch_` headers false-share (2.67 per line). The two steal counters share one line across all thieves (`scheduler.cpp:855, 866`). Counters with a single writer use `fetch_add`, a locked instruction (`scheduler.cpp:405, 585`).
Change: one cache-line-aligned `WorkerState` per worker (deque pointer, scratch, completion counter, time buckets, own steal counters, thread handle), in a vector of `num_workers`. Single-writer counters become a relaxed load and store; readers sum the steal counters.
Settles it: `test_steal_locality_perf`, `test_scheduler_dispatch_perf`.
Breaks API: no. Effort: M.

#### CORE-16 [medium, performance] The first worker to reach the injector takes every root

Evidence: `grab_count = min(4096, size)` (`scheduler.cpp:350, 358`). The other workers, woken by the same `notify_all`, find it empty and steal one task at a time, each steal a fence and a CAS (`work_stealing_deque.hpp:167-178`).
Change: take `ceil(size / num_workers)`, at least one.
Settles it: `test_root_fanout_perf`.
Breaks API: no. Effort: S.

#### CORE-17 [medium, api and operability] Graph errors use two exception types and name no node

Evidence: `compile()` throws `std::runtime_error` for "no root" and "cycle" (`task_graph.cpp:660, 709`); the scheduler throws `SushiException{Errc::invalid_graph}` for a graph error of the same kind (`scheduler.cpp:676`). The cycle message gives a count only (`task_graph.cpp:704-707`). `epoch_reclamation.hpp:183` and `SR_THROW_IF` (`logger.hpp:357`) also throw bare `std::runtime_error`.
Change: throw `SushiException` with `Errc::invalid_graph`; put the name and registration index of one undrained node in the cycle message.
Already known: exceptions as the public error mechanism (C5).
Breaks API: no; `SushiException` derives from `std::runtime_error`. Effort: S.

#### CORE-18 [medium, documentation] README statements the code no longer supports

1. `include/SushiRuntime/graph/README.md:78-84`: NodeProfiling fields "are written only when the runtime was built with profiling on". Nothing writes them.
2. `include/SushiRuntime/scheduler/README.md:76-78`: the invocation count and device time "are added here". `accumulate_profiling` writes a ledger record and touches no counter (`event_polling.hpp:358-393`).
3. Same file, 40-42: the flag comes "from the context's profiling choice". It is the plan's flag (`scheduler.cpp:224-235`), as `src/scheduler/README.md:184` says.
4. Same file, 164-165, and `graph/README.md:124-127`: "completion promise". The engine uses a `CompletionLatch` (`execution_engine.cpp:114`).
5. Same file, 154-157: "The graph layer uses [RangeTask]". No library code constructs one.
6. `src/scheduler/README.md:171-172`: `completed_tasks_for` does not exist; the function is `get_completion_rate`.
7. `src/scheduler/README.md:72-73`: "an armed, idle pool polls often between runs". The pool is disarmed between runs (`scheduler.cpp:253-259`).
8. `task_scheduler.hpp:169-174`: "Workers exit their loop when this is false". They park (`scheduler.cpp:417-433`).
9. `include/SushiRuntime/core/README.md:135-136`: release() "issues a release fence". The code issues an acquire fence (`ref_counted.hpp:84`).
10. `src/graph/README.md`, "compile": the first paragraph says compile "does not traverse the whole graph"; the third describes the Kahn pass that visits every node (`task_graph.cpp:664-712`).
11. `src/scheduler/README.md:183-184, 206`: "the comment this page replaces cites ..." is history, which the documentation rules keep out of a module README.
Breaks API: no. Effort: S.

#### CORE-19 [low, layout] core/ref_counted.hpp includes the logger and uses none of it

Evidence: `ref_counted.hpp:33` includes `core/logger.hpp`; the file has zero `SR_` uses. Through `scheduler/task.hpp:38` and `graph/node.hpp:42` every API consumer compiles `<thread>`, `<queue>`, `<sstream>`, `<condition_variable>` and the two logger classes. `task_graph.cpp:29` includes `<future>` and uses nothing from it.
Change: delete both includes; add the logger include to any file that relied on it transitively.
Breaks API: source-compatible, except for code leaning on the transitive include. Effort: S.

#### CORE-20 [low, operability] The rebalancer's one log line prints a meaningless number

Evidence: `thermal_proxy_strategy.hpp:97-98` prints `max_queue - last_completed_counts_[bottleneck_worker]` as "Rate": a queue depth minus a lifetime completion total, unsigned. It also calls a worker a "Core".
Change: keep `current_rate` of the chosen worker and print that; say "worker".
Breaks API: no. Effort: S.

#### CORE-21 [low, performance] arm_for_execution locks the error mutex on every step to clear an error that is almost never set

Evidence: `scheduler.cpp:227-230`, called twice per step because the sentinel arms again (`src/execution/execution_engine.cpp:453`, outside my scope; the second call finds `running` already true).
Change: test `run_failed_` first and lock only when it is set. Whether the sentinel's second arm can go belongs to the execution auditor.
Settles it: `test_wide_fanout_perf` (many small steps).
Breaks API: no. Effort: S.

#### Already known and still open, not counted above

- Scheduler, graph, execution include cycle: prior audit C1.
- Scheduler downcast to `Graph::Node`: C2.
- `catch (...)` logs without the cause at `scheduler.cpp:454-458, 764-769` and `event_polling.hpp:344`: C12.
- Per-node DEBUG lines (`scheduler.cpp:589`, `task_graph.cpp:279, 332, 397, 594`): C13.
- `node.hpp` holds ten types: C17.
- Sentinel injection in `compile()`: `REMAINING_WORK.md`, Structural debt.
- R9, one scheduler round trip per colour: `REMAINING_WORK.md`, Physics substrate.
- Dynamic-graph offload skips `stamp_offloadable`: `REMAINING_WORK.md`, Distributed layer.

### Not checked

- No build, test or benchmark was run. No number in this report is a timing.
- Sizes and offsets are for the standalone Windows build with the MSVC STL. `SR_DISTRIBUTED`, libstdc++ and AdaptiveCpp layouts were not measured.
- Whether a default `sycl::event` allocates in the installed DPC++ was not verified; the claim rests on the repository's own README.
- The Windows per-module duplication of the logger and of `EpochManager` was read from the code, not observed.
- `docs/architecture/ARCHITECTURE.md` was grepped, not read; `docs/CONTRIBUTING.md` was read by headings only.
- Not read, or read only by grep: `core/{bit,delegate,result,span,strong_id,sushi_ptr,error,run_report,profiling_ledger,event_timestamps,resource_region,resource_liveness,sycl_compat,dispatch_width,export}.hpp`, `src/core/version.cpp`, `memory/{allocation,usm_registry,usm_allocation_owner}.hpp`, the body of `graph/task_graph.hpp`, `scheduler/rebalancer_interfaces.hpp` beyond its three declarations. `src/memory/` was not opened.
- The benchmark files were read by their headers only; I did not confirm what each prints.
- The install rule's header set was not confirmed.
- Memory-ordering correctness of the deque, the EBR and the successor list was not audited; I listed costs, not proofs.

### Delegation block evidence

1. SOLID: each proposal names one unit with one job (`StealTopology`, `QueueRouter`, `IdlePolicy`, `WorkerState`, the logger's three functions) and the split in CORE-11 keeps siblings shaped alike. Choices about module boundaries, public types and settings are marked as the owner's call.
2. Humanizer: skill loaded; `generic_english.md` read to line 80 of 178; the report was written against it.
3. No process running `sr`, `se`, `cmake`, `ninja` or `ctest` was started. No build file was touched. The one compiler run was `clang++ -fsyntax-only` on a scratch probe.
4. Nothing was written in the repository.
5. Done: the seven questions, with evidence. Not done: any measurement, and the files listed under Not checked.
6. No source written; nothing to syntax-check. The probe command and its output are pasted under Facts.
7. Scratch: `sizes.cpp` and `cc.py` in the scratchpad, both deleted. `du -sh` of the scratchpad reports 4.0K; it holds one folder, `graphapi`, which another agent made.

## Execution, topology, interop (EXEC)

Dispatch block: (1) no code written; every proposal below is shaped as a single-responsibility brick with a file name. (2) `humanizer` loaded and applied to this report; `performance`, `cpp-code-style`, `platform-portability`, `logging`, `api-stability` loaded. (3) No `sr`, `se`, `cmake`, `ninja` or `ctest` process started. (4) Nothing written in the repository. (6) No source written; nothing to syntax-check. (7) Nothing written to the scratchpad; it holds `cc.py` (250 bytes) and a `graphapi/` folder from other agents, which I did not touch or size.

All paths are under `D:\Projects\sushiruntime`.

### Facts

**Sizes** (`wc -l`): `src/execution/runtime_context.cpp` 1050, `execution_engine.cpp` 600, `chain_run.cpp` 234, `chain_plan.cpp` 170; `include/SushiRuntime/execution/runtime_context.hpp` 443, `execution_engine.hpp` 410; `src/topology/*` 679 in four files; `src/interop/interop.cpp` 299. READMEs: `include/SushiRuntime/execution/README.md` 326, `src/execution/README.md` 320, `include/SushiRuntime/topology/README.md` 56, `src/topology/README.md` 65, `src/interop/README.md` 12. `include/SushiRuntime/interop/` has no README.

**RuntimeContext public surface** (`runtime_context.hpp:75-418`): 39 member functions without the distributed layer, 52 with it. Responsibilities, by member:

| Responsibility | Members | State in `Impl` (`runtime_context.cpp`) |
| --- | --- | --- |
| Hardware probe | `probe_hardware` | `provider`, `nodes` (62, 68) |
| Queues, four families | `get_queue`, `profiling_queue`, `has_profiling_queue`, `chain_queue`, `device_count`, `device_is_gpu`, `get_numa_local_queue`, `wait_all` | `all_queues`, `numa_aware_queues`, `profiling_queues` + `profiling_once` + `profiling_built`, `chain_queues`, `chain_profiling_queues`, `chain_profiling_built` (74-112) |
| USM allocation | `malloc_shared`, `malloc_device`, `malloc_{shared,device,host}_on`, `aligned_malloc_{host,shared,device}_on`, `free_usm` | none of its own |
| USM ownership registry | `adopt_usm`, `forget_usm`, `containing_usm_base`, `live_usm_count`, `context_index_for_ptr`, `get_queue_for_ptr` | `usm_registry` (128) |
| Topology view | `numa_distance`, `cpu_core_to_numa` | `distance_model`, `core_to_numa` (65, 71) |
| Settings | `settings`, `profiling_enabled` | `settings` (117) |
| Transfer counters | `add_*_bytes`, `*_bytes` (four) | two atomics (122-123) |
| Engine lifetime | `get_execution_engine`, `shutdown` | `engine` (138) |
| Distributed session | 13 `distributed_*` / `*_distributed_*` members | `role`, `buffers`, `coordinator` (142-146) |

**What `include/SushiRuntime/api` calls on RuntimeContext** (`grep -rhoE "(->|\.)(<member>)\(" include/SushiRuntime/api | sort | uniq -c`): `get_execution_engine` 13, `device_to_host_bytes` 9, `host_to_device_bytes` 9, `get_queue_for_ptr` 3, `settings` 3, `containing_usm_base` 2, `device_count` 2, `device_is_gpu` 2, `add_device_to_host_bytes` 1, `add_host_to_device_bytes` 1, `context_index_for_ptr` 1, `profiling_enabled` 1, `malloc_shared_on` / `malloc_device_on` / `malloc_host_on` 1 each (all at `api/data/buffer.hpp:118-120`), `free_usm` 1 (`api/detail/fixed_order_reduce.hpp:131`), and the distributed members from `cluster.hpp`, `run_report_builder.hpp`, `buffer.hpp`, `runtime.hpp`.
Not called from `api/` at all: `get_queue`, `profiling_queue`, `has_profiling_queue`, `chain_queue`, `get_numa_local_queue`, `malloc_shared`, `malloc_device`, all three `aligned_malloc_*_on`, `adopt_usm`, `forget_usm`, `live_usm_count`, `numa_distance`, `cpu_core_to_numa`, `wait_all`, `shutdown`, `probe_hardware`. Their callers are the scheduler (`src/scheduler/scheduler.cpp:195, 658, 667, 694, 695`), interop (`src/interop/interop.cpp`), `memory/usm_allocation_owner.hpp` and tests. `API::Runtime::unsafe_context()` (`api/runtime/runtime.hpp:222`) hands the whole class to users, so any member can have callers in the repositories above.

**Environment variables.** `grep -rnE "getenv|_dupenv|GetEnvironmentVariable" include src` returns nothing. No C++ in this repository reads an environment variable. The variables that change what these layers see are read by SYCL and hwloc. Docs coverage, counted with `grep -rl <name> docs` minus `docs/agent` and `docs/archive`: `ONEAPI_DEVICE_SELECTOR` 0 files, `SYCL_DEVICE_FILTER` 1, `CUDA_LAUNCH_BLOCKING` 0, `HWLOC_*` 0, `ACPP_VISIBILITY_MASK` 0.

**Compile definitions read in scope:**

| Definition | Where | Set by | In docs |
| --- | --- | --- | --- |
| `SR_DISTRIBUTED` | `runtime_context.hpp:43, 353`; `execution_engine.hpp:50, 399`; `runtime_context.cpp:43, 140, 153, 749, 867`; `execution_engine.cpp:386, 592` | `src/CMakeLists.txt:109`, PUBLIC, from option `SR_ENABLE_DISTRIBUTED` | yes (3 files) |
| `SYCL_EXT_INTEL_DEVICE_INFO >= 5` | `src/topology/hw_discovery.cpp:128, 135` | SYCL toolchain | only `src/topology/README.md` |
| `SYCL_EXT_ONEAPI_BINDLESS_IMAGES` | `interop/detail/bindless_detection.hpp:38`; `src/interop/interop.cpp:44, 54, 90, 244` | SYCL toolchain | no |
| `_WIN32` | `include/SushiRuntime/topology/affinity.hpp:35, 55` | compiler | no |
| `NOMINMAX` | `affinity.hpp`, `hw_discovery.cpp:28`, `hwloc_topology_provider.cpp:28`, `numa_mapper.cpp:28` | defined in place, four times | no |

**Platform forks.** `_WIN32` appears once in scope, in `affinity.hpp`. Interop branches on the handle kind at run time (`interop.cpp:259`), not by `#ifdef`. Toolchain forks are three macros above plus two unguarded extension uses (EXEC-14).

**Start-up path of one `RuntimeContext`** (`runtime_context.cpp:198-222`): `HwlocDistanceModel()` loads an hwloc topology (`hwloc_topology_provider.cpp:114`); `cpu_core_to_numa()` loads a second (`:72`); `discover_sycl_devices` loads a third with all I/O types kept (`hw_discovery.cpp:66-69`); `NUMAMapper::map_distances` loads a fourth, also with I/O (`numa_mapper.cpp:60-61`). Then one `sycl::queue` per device, the profiling queues if `settings.profiling`, then the scheduler. Cached for the context's life: `nodes`, `core_to_numa`, the SLIT matrix. Lazy: profiling and chain queues. Nothing is shared between two contexts in one process. No benchmark under `tests/benchmark/performance` measures construction.

**Benchmarks present:** `test_chain_dispatch_perf`, `test_scheduler_dispatch_perf`, `test_root_fanout_perf`, `test_wide_fanout_perf`, `test_profiling_queue_perf`, `test_graph_compile_perf`, `test_recompose_churn_perf`, `test_async_overlap_perf`, `test_dynamic_graph_perf`, two steal benches, `test_distributed_offload_perf`. Only `test_profiling_queue_perf.cpp` mentions profiling, and it measures a raw queue, not a profiled chain run.

**Already known, not repeated below:** RuntimeContext carries seven responsibilities (prior audit C3, `REPORT.md:976`); memory and interop include upward (C8, `:1016`); swallowed `catch (...)` at `hw_discovery.cpp:149` and `interop.cpp:112, 151` (`:1042`); logger has no categories (`:1058`); `execution_engine.hpp` holds interface and implementation (`:1090`); `-1` sentinels in `context_index_for_ptr` and `chain_plan.cpp` (`:1106`); `Runtime::try_create` is BB-5b in `docs/design/REMAINING_WORK.md:27`.

### Findings

#### EXEC-01. GPU without a resolved PCI address is mapped from uninitialised memory
- Severity high. Category operability.
- Evidence: `HardwareNode` and `PCIDomainAddress` in `include/SushiRuntime/topology/hw_discovery.hpp` have no member initialisers. `hw_discovery.cpp:92` declares `HardwareNode node;`. When all three lookups fail, line 207 only warns; `pci_address` is zeroed only in the `catch (...)` at 213. `numa_mapper.cpp:73-77` then passes the four fields to `hwloc_get_pcidev_by_busid`. If the garbage or zero address matches a real device (0000:00:00.0 is normally the host bridge), line 92 logs `Device [..] (PCI ..) is hard-bound to NUMA node N`, which is false.
- Change: give both structs default initialisers; carry the address as `std::optional<PCIDomainAddress>`; in the mapper, skip the lookup when it is empty and log one line.
- Breaks API: source-compatible if the field stays and a `has_pci_address` flag is added; yes if the field becomes optional. Effort S.

#### EXEC-02. Discovery failures reach the operator without cause or next step
- Severity high. Category operability.
- Evidence: `hw_discovery.cpp:220-224` logs the SYCL text and throws `std::runtime_error("hardware discovery failed.")`, so the exception a caller catches has no cause, and it is not a `Core::SushiException`. `runtime_context.cpp:373` logs it again as `Critical initialization failure: hardware discovery failed.` `hwloc_topology_init` and `hwloc_topology_load` return values are ignored at `hw_discovery.cpp:66-69`; `load` is ignored at `hwloc_topology_provider.cpp:72, 114` and `numa_mapper.cpp:61`. On WSL2, where hwloc sees no PCI devices, every start prints two WARN lines per GPU (`hw_discovery.cpp:207`, `numa_mapper.cpp:104`); neither says the result is harmless on a single-NUMA machine. The no-device message (`runtime_context.cpp:348-354`) is the good example: it names the filter and both causes.
- Not covered by any message: hwloc missing (a loader failure before `main`), and mixed SYCL stacks (a crash inside device enumeration, per the owner's notes; nothing in these layers can catch it).
- Change: throw `Core::SushiException` with the SYCL text in the message; check every hwloc return and say which fallback follows; collapse the WSL2 pair into one INFO line stating "no PCI information from hwloc (expected on WSL2); device placed on NUMA 0"; add a "start-up messages" section to `docs/guides/FAQ.md`, which today has no hit for "No SYCL device", "WSL" or "setvars".
- Breaks API: no (exception type narrows to the documented one). Effort S.

#### EXEC-03. RuntimeContext split, concretely
- Severity high. Category api, layout. The diagnosis is already known (C3); this is the decomposition asked for.
- Evidence: the responsibility table in Facts; nine groups behind one class and one 1050-line source.
- Change, one brick per file, RuntimeContext keeping construction order, `settings()`, the engine and `shutdown()`:
  1. `execution/device_queue_set.hpp` + `src/execution/device_queue_set.cpp`: owns `nodes`, all queue families, the NUMA map. Takes `get_queue`, `profiling_queue`, `has_profiling_queue`, `chain_queue`, `device_count`, `device_is_gpu`, `get_numa_local_queue`, `wait_all`, the destructor drain (`runtime_context.cpp:168-188`).
  2. `memory/usm_allocator.hpp` + `src/memory/usm_allocator.cpp`: owns `UsmRegistry`, references the queue set. Takes every `malloc_*`, `free_usm`, `adopt_usm`, `forget_usm`, `containing_usm_base`, `live_usm_count`, `context_index_for_ptr`, `get_queue_for_ptr`. `UsmAllocationOwner` can then hold the allocator, which removes the memory-to-execution include of C8.
  3. `execution/transfer_counters.hpp`: the two atomics and four accessors.
  4. `topology/topology_view.hpp`: `core_to_numa` and the distance model; takes `numa_distance`, `cpu_core_to_numa`.
  5. `execution/distributed_session.hpp` + `.cpp`, compiled only with `SR_DISTRIBUTED`: `role`, `buffers`, `coordinator` and the 13 members. This removes five of the `#ifdef` blocks from `runtime_context.cpp`.
- Blast radius: inside `api/`, 16 distinct non-distributed members across `buffer.hpp`, `graph.hpp`, `dynamic_graph.hpp`, `runtime.hpp`, `run_report_builder.hpp`, `fixed_order_reduce.hpp` (counts in Facts). The 16 members `api/` never calls can move without touching `api/`. Callers in SushiEngine through `unsafe_context()` were not checked.
- Breaks API: source-compatible with a deprecation, if RuntimeContext keeps forwarding members marked `[[deprecated]]` for one MINOR and gains `queues()`, `memory()`, `transfers()`, `topology()`, `distributed()`. Module boundaries and public shape need the owner's decision first. Effort L.

#### EXEC-04. Eight allocation entry points, two placement rules, one missing sibling
- Severity high. Category api.
- Evidence: `runtime_context.cpp:540-681`. The six `_on` functions are the same four lines with a different `sycl::` call. The two NUMA forms (`malloc_shared` 596, `malloc_device` 618) add a linear scan over `all_queues` to recover the index (603-606, 624-627). There is no `malloc_host(numa)` and no aligned NUMA form. The NUMA forms pick a queue round-robin per thread (`get_numa_local_queue`, 510-518), so two consecutive `malloc_shared(bytes)` calls on a machine with a CPU and a GPU on NUMA 0 return memory in two different SYCL contexts. `api/data/buffer.hpp:118-120` already switches on `Residency` to choose among three of them, so the parameterised seam exists one layer up. Header says "nullptr on failure" for all eight, yet a bad index throws (`get_queue`, 387).
- Change: one entry point, `void* allocate(const UsmRequest&)`, with `UsmRequest { UsmKind kind; std::size_t bytes; std::size_t alignment = 0; std::size_t device_index; }`, and one helper `device_for_numa(int)` for callers that start from a NUMA id. The `Residency` switch in `buffer.hpp` becomes a mapping to `UsmKind`. Keep the eight names as deprecated forwarders for one MINOR.
- Breaks API: source-compatible with a deprecation. Effort M.

#### EXEC-05. The runtime never tells a program what devices it found
- Severity high. Category api, operability.
- Evidence: the inventory `Impl::nodes` (`runtime_context.cpp:68`) has no accessor; the public questions are `device_count()` and `device_is_gpu(i)`. Name, NUMA node, memory size and backend exist only in INFO log lines (338-342). Device order is SYCL enumeration order (`hw_discovery.cpp:78`), so `DeviceIndex 0` is a CPU on one machine and a GPU on another. `HardwareFilterConfig` selects by class and fp64 only; choosing a backend or one of two GPUs needs `ONEAPI_DEVICE_SELECTOR`, which no page under `docs/` mentions.
- Change: add `const std::vector<DeviceInfo>& devices() const` (index, name, kind, backend, NUMA node, memory bytes) on the queue-set brick and on `API::Runtime`; document the ordering rule in `docs/guides/INTEGRATION.md`; add an optional backend list to `HardwareFilterConfig`. Whether to fix the order (GPUs first) is a behaviour change for the owner to decide.
- Breaks API: no (additions). Effort M.

#### EXEC-06. `probe_hardware()` is public and unsafe to call twice
- Severity high. Category api.
- Evidence: `runtime_context.hpp:87-95` invites a second call "when the hardware topology changes at runtime". The body (`runtime_context.cpp:295-304`) clears `all_queues` and replaces every lazy-queue vector while the scheduler holds `sycl::queue&` references, the registry holds old indices, and `chain_queue()` promises references valid "for the context's lifetime" (comment at 107-109). No caller exists outside the constructor (`grep -rn "probe_hardware(" include src tests examples`).
- Change: make it private and fold it into construction.
- Breaks API: yes (removal; pre-1.0, changelog entry and `!`). Effort S.

#### EXEC-07. Documented exception type is not the one thrown, and the message carries no values
- Severity medium. Category api, documentation.
- Evidence: `runtime_context.hpp:137, 152, 167` say `@throws Core::SushiException`. The code uses `SR_THROW_IF` (`runtime_context.cpp:387, 428, 476, 489`), which throws `std::runtime_error` (`core/logger.hpp:352-359`). The `.cpp` Doxygen blocks say `std::runtime_error` (383, 538), contradicting the header. The text is `"Queue index out of bounds."` with neither the index nor `device_count()`.
- Change: throw `Core::SushiException` with `Errc::invalid_access` and the two numbers; drop the duplicated Doxygen blocks from the `.cpp` so one statement exists.
- Breaks API: no. Effort S.

#### EXEC-08. A WARN per call on the dispatch path
- Severity medium. Category performance, operability. Hypothesis until run on a machine where it triggers.
- Evidence: `runtime_context.cpp:521-525` logs `No queue found for NUMA {}, falling back...` on every call whose NUMA id has no queue. The scheduler calls it per dispatch for pointer-less tasks (`src/scheduler/scheduler.cpp:695`). A two-socket machine with one device set on NUMA 0, or a `RemappingTopologyProvider` with more fake nodes than devices, sends every such task from a NUMA 1 worker through the WARN.
- Change: resolve the fallback once per NUMA id at probe time into a dense table, log it once there, and make the lookup a plain index.
- Benchmark: `test_steal_numa_effect_perf` uses a synthetic two-node topology and would show it; `test_scheduler_dispatch_perf` would not.
- Breaks API: no. Effort S.

#### EXEC-09. hwloc is loaded four times per context through five hand-written init/destroy sequences
- Severity medium. Category performance, layout. Start-up cost is a hypothesis; no benchmark exists.
- Evidence: load sites in Facts, plus a fifth in `NUMAMapper::get_distance` (`numa_mapper.cpp:146-148`). Two loads keep all I/O objects, the expensive kind. `hw_discovery.cpp:222-226` destroys the topology by hand on two paths and catches only `sycl::exception`.
- Change: one private RAII brick, `src/topology/hwloc_topology.hpp`, loaded once by `HwlocTopologyProvider` and passed to discovery, mapper, core map and distance model. Add `tests/benchmark/performance/benchmark_runtime_startup` that times `RuntimeContext` construction over five runs.
- Breaks API: yes for the exported `HardwareDiscovery` and `NUMAMapper` signatures if they take the topology; no if the provider owns it and the two classes become private to `src/topology`. Effort M.

#### EXEC-10. Four queue families built three different ways
- Severity medium. Category layout.
- Evidence: profiling queues use three parallel vectors (`runtime_context.cpp:82, 85, 92`); chain queues use `LazyQueue` (98-112) plus a fourth vector (95). The async handler lambda is written three times (320-323, 411-414, 461-464). The destructor and `wait_all` each walk the families separately; `wait_all` (834-840) waits only the plain queues, though its header says "all async operations".
- Change: one `QueueFlavour { bool in_order; bool profiling; }` and one table of `LazyQueue` per device and flavour, one builder, one `for_each_built_queue`. `has_profiling_queue` becomes a read of two slots.
- Breaks API: no (inside `Impl`). Effort M.

#### EXEC-11. Interop has no user document in this repository
- Severity medium. Category documentation.
- Evidence: `interop.hpp`, `external_memory.hpp`, `host_export.hpp` and `interop_capabilities.hpp` each point to "docs/design/SYCL_VULKAN_INTEROP.md §3 in the SushiEngine repository". `grep -rl export_host docs` and `import_device` return no file outside `docs/agent`. `ARCHITECTURE.md:1269-1270` is a two-line directory entry. `src/interop/README.md` is 12 lines about one ordering detail.
- Rules a user has to know and cannot read anywhere: who closes the Win32 handle or fd after `import_device`; that the exporting API's allocation must outlive the returned `Buffer`; that `HostExport::address` dangles once `HostExport::buffer` is destroyed; that an imported `Buffer` keeps the `RuntimeContext` alive (`DeviceImportOwner` holds a `shared_ptr`, `interop.cpp:120`); that there is no way to hand the runtime a foreign `sycl::queue` or context, and foreign USM is routed by introspection only (`runtime_context.cpp:776-802`).
- Change: add `include/SushiRuntime/interop/README.md` with an ownership table, and a section in `docs/guides/INTEGRATION.md`. The handle-ownership answer has to be read from the SYCL bindless-images specification first; I did not verify it.
- Breaks API: no. Effort S.

#### EXEC-12. `import_device` accepts descriptors it could reject, and one error names the wrong cause
- Severity medium. Category api.
- Evidence, all in `src/interop/interop.cpp`: `descriptor.kind` is not checked against the platform, and `ExternalMemoryDescriptor::kind` defaults to `OpaqueWin32` on Linux too (`external_memory.hpp`); `descriptor.offset + bytes <= allocation_bytes` is not checked before `map_external_linear_memory` (277); a device without the UUID aspect fails with "the descriptor names a different device" (251-254); `capabilities.host_export = true` is unconditional (77). `API::Interop::export_host` labels a host USM allocation `Residency::Shared` (`interop.hpp`), which may matter for the Windows CUDA host-read item in `REMAINING_WORK.md:9`; I did not trace how `Buffer` uses the label.
- Change: validate kind, range and UUID presence up front with one message each; derive `host_export` from `sycl::aspect::usm_host_allocations`; give `kind` no default.
- Breaks API: source-compatible, except removing the default of `kind`. Effort S.

#### EXEC-13. The one platform fork sits in a public header and is wrong above 63 cores
- Severity medium. Category operability (portability).
- Evidence: `include/SushiRuntime/topology/affinity.hpp:35-39` includes `<windows.h>` into every consumer of the umbrella (`SushiRuntime.h:42`). Line 58 computes `1ULL << core_id`, undefined for `core_id >= 64` and blind to processor groups; the POSIX branch calls `CPU_SET(core_id, ...)` with no bound. The Doxygen says an out-of-range value "is logged and ignored".
- Change: keep only the declaration in the header, returning a status; move the bodies to `src/topology/affinity_windows.cpp` (`SetThreadGroupAffinity`) and `src/topology/affinity_linux.cpp`; bound-check in both. Put the `NOMINMAX`-then-`hwloc.h` preamble, now copied in three sources, in the private header of EXEC-09.
- Breaks API: source-compatible (inline function becomes exported; return type changes from `void`). Build files change, which this dispatch could not touch. Effort S.

#### EXEC-14. Toolchain forks are not behind one seam
- Severity medium. Category operability (portability). Not compiled, so a hypothesis for AdaptiveCpp.
- Evidence: `core/sycl_compat.hpp` exists and covers only `host_task`. `hw_discovery.cpp:128-137` guards the Intel device-info extension inline. `interop.cpp:81-84` uses `sycl::aspect::ext_intel_device_info_uuid` and `sycl::ext::intel::info::device::uuid` with no guard. `chain_run.cpp:30, 167` includes and calls `sycl::ext::oneapi::experimental::submit` with no guard. `cmake/Sycl.cmake:12` still lists AdaptiveCpp as a supported toolchain.
- Change: add `device_pci_address(device)`, `device_uuid(device)` and `submit_without_event(queue, fn)` to `sycl_compat.hpp`, each returning an optional or falling back, and remove the three inline uses.
- Breaks API: no. Effort S.

#### EXEC-15. A profiled multi-step chain run keeps every event until the end
- Severity medium. Category performance. Hypothesis.
- Evidence: `chain_run.cpp:64-65` reserves `order.size()` entries, one step's worth. `submit_profiled` pushes one `PendingEvent` per node per step (195-196). `api/graph/graph.hpp:2283-2298` builds one `ChainRun` for the whole `run(n)` and calls `finish()` once, so the vector grows to `n * order.size()` live `sycl::event` objects and reallocates on the way. `src/execution/README.md:11-14` says later runs are "free of allocation".
- Change: record events into the ledger every K steps, or whenever `StepSync::EveryStep` already waits, and clear the buffer.
- Benchmark: none. `test_chain_dispatch_perf` runs unprofiled; add a profiled case there.
- Breaks API: no. Effort S.

#### EXEC-16. Chain HOST nodes build a `std::vector<sycl::event>` per node per step
- Severity medium. Category performance. Hypothesis; allocates only when `sycl_deps` is non-empty.
- Evidence: `chain_run.cpp:133-136` and `159-162`, written twice. The cause is the `HostWork` signature, `std::function<sycl::event(sycl::queue&, const std::vector<sycl::event>&)>` (`graph/node.hpp:58`), while the node stores a `SmallEventVector` (`node.hpp:340`).
- Change: one helper used by both paths with a scratch vector owned by `ChainPlan`, next to `pending_events`. Changing `HostWork` to take a span is the larger fix and belongs to the graph auditor.
- Benchmark: `test_chain_dispatch_perf` if it gains a HOST-node-that-submits case; today's chain is kernels only (not verified beyond its `@brief` and the `force_scheduler` loop).
- Breaks API: no for the helper. Effort S.

#### EXEC-17. `collect_profiling` looks up each node name by scanning all nodes
- Severity low. Category performance. Hypothesis.
- Evidence: `execution_engine.cpp:336-347`: for each registration index, a linear scan of `graph.all_nodes` (341-342). Quadratic in node count, once per profiled run; `report.node_timings.push_back` copies a name string per row.
- Change: build a `first_node_of_index` table in `capture_profiling_state()` (`execution_engine.cpp:558-575`), which already walks every node.
- Benchmark: none measures report collection; `test_graph_compile_perf` has the graph sizes to reuse.
- Breaks API: no. Effort S.

#### EXEC-18. Fixed per-step costs on the scheduler path, listed for measurement
- Severity low. Category performance. Each is a hypothesis.
- Evidence, `src/execution/execution_engine.cpp`: three mutex acquisitions and one `notify_one` per step in `CompletionLatch` (`reset` 61-67, `signal` 74-83, `wait` 86-90); the caller thread blocks at 491; the sentinel copies every predecessor event to call `wait()` on a worker thread (444-446); `wait_quiescent` then `disarm` at 494-497 and re-arm at 464 park and wake workers each step, which `src/execution/README.md:171-175` describes as "almost nothing" without a number; `set_profiling_ledger` each step (463). On the chain path, `chain_run.cpp:187` reads `steady_clock::now()` for every profiled node though only the pairing owner uses it.
- Benchmark: `test_wide_fanout_perf` (per-step drain latency) and `test_root_fanout_perf` (per-step cost) settle the first four; nothing isolates the clock read.
- Change: none proposed before numbers; move the `t0` read under `owns_pairing`.
- Breaks API: no. Effort S.

#### EXEC-19. Stale statements in the execution READMEs
- Severity medium. Category documentation.
- Evidence:
  - `include/.../execution/README.md:113-115`: execute "blocks on the sentinel's completion promise". It is a `CompletionLatch` (`execution_engine.cpp:52`).
  - `:126`: batch throughput "leaves it on (the default)"; `:316` and `runtime_settings.hpp` say the rebalancer is off by default.
  - `:131` and `:140`: profiling "zeroes every node's per-run timing" and "reads each node's accumulated device and host time". The code resets worker buckets and the ledger (`execution_engine.cpp:280-292`) and aggregates from ledger events (323-333).
  - `:12-14` and `chain_plan.hpp`: `classify_chain` refuses "a device value read at dispatch". `chain_plan.cpp:47-57, 134-167` has no such test.
  - `:198`: `device_count` reflects "GPUs once each". No GPU deduplication exists (EXEC-20).
  - `src/execution/README.md:201-202`: "The header only forward-declares `Node`". `execution_engine.hpp:39` includes `graph/node.hpp`, and the other README explains why.
  - `src/execution/README.md:190, 274`: "labelled F4 in the comments it replaces", "The comment this page replaces adds..." are history, which the documentation rule keeps out of a module page.
- Change: correct each line; if the device-read rule is enforced elsewhere, name the place.
- Breaks API: no. Effort S.

#### EXEC-20. Topology docs describe deduplication and behaviour the code does not have
- Severity medium. Category documentation, operability.
- Evidence: the `HardwareDiscovery` class comment in `hw_discovery.hpp` says duplicate devices "reported by multiple backends are deduplicated"; `hw_discovery.cpp:113-119` deduplicates CPUs only. A GPU exposed by two backends would become two devices with two contexts (not observable on the owner's GTX 1060; hypothesis). `ITopologyProvider::enumerate` documents `-1 if unknown`; the mapper always writes `>= 0`. `src/topology/README.md` calls `HwlocDistanceModel` "the single place that touches the hwloc SLIT table"; `numa_mapper.cpp:155` also reads it. `docs/architecture/ARCHITECTURE.md:586-587` names `get_distance()` on `HwlocDistanceModel`, whose method is `distance()`. Accelerators get a PCI lookup in discovery (`hw_discovery.cpp:121`) that the mapper ignores (`numa_mapper.cpp:70` tests `is_gpu` only).
- Change: fix the four statements; decide whether GPUs are deduplicated by PCI address or UUID and implement or document the choice.
- Breaks API: no. Effort S for the text, M for GPU deduplication.

#### EXEC-21. Two rules assign a CPU's NUMA node, and `NUMAMapper::get_distance` duplicates the distance model
- Severity low. Category api, layout.
- Evidence: `hw_discovery.cpp:106-110` gives every CPU device the NUMA node of PU 0; `numa_mapper.cpp:111-123` overwrites it with the node covering the process binding. The first value only feeds the "one CPU per NUMA node" filter, which therefore keeps whichever CPU device SYCL lists first. `NUMAMapper::get_distance` (`numa_mapper.cpp:140-180`) reloads hwloc per call and is used only by `tests/functional/unit/test_topology.cpp:94, 97`.
- Change: assign CPU NUMA in one place; name the filter for what it does (one CPU device); deprecate `get_distance` and forward to `HwlocDistanceModel`.
- Breaks API: source-compatible with a deprecation. Effort S.

#### EXEC-22. `free_usm` on an unrecorded pointer frees against queue 0 without a word
- Severity low. Category operability.
- Evidence: `runtime_context.cpp:754-759`: no record means `sycl::free(ptr, all_queues[0])`, commented "best effort". A pointer from another context, or a double free, takes this path silently. An adopted external mapping passed to `free_usm` would be handed to `sycl::free`.
- Change: log one WARN with the address and what was done, or refuse and return a status; record adopted entries as not freeable.
- Breaks API: no. Effort S.

#### EXEC-23. No page lists the variables and definitions that steer these layers
- Severity low. Category documentation, operability.
- Evidence: the two tables in Facts. An operator choosing a device, or diagnosing why a GPU is absent, has `ONEAPI_DEVICE_SELECTOR` and the hwloc variables as the only levers, and neither is in `docs/`.
- Change: one reference page, `docs/reference/CONFIGURATION.md`, with both tables and the `RuntimeSettings` and `HardwareFilterConfig` fields. The documentation skill decides the placement; I did not load it.
- Breaks API: no. Effort S.

#### EXEC-24. File layout inside execution
- Severity low. Category layout. Partly known (`REPORT.md:1090`).
- Evidence: `CompiledGraph`'s three member definitions live in `execution_engine.cpp:538-597`; `CompletionLatch` is defined in the same file (52-104). `runtime_context.cpp` repeats a Doxygen block for every function the header already documents, about 400 of its 1050 lines, and the two copies disagree (EXEC-07). `runtime_context.hpp:276, 285` use `std::optional` while `<optional>` is included only under `SR_DISTRIBUTED` (45); `hw_discovery.hpp` uses `uint32_t` and `size_t` without `<cstdint>` or `<cstddef>`.
- Change: `compiled_graph.hpp/.cpp`, `completion_latch.hpp` (private, under `src/execution`), interface and engine in separate headers; delete the `.cpp` duplicates; add the missing includes.
- Breaks API: source-compatible if `execution_engine.hpp` keeps including the new headers. Effort M.

#### EXEC-25. The backlog says the module folders have no README
- Severity low. Category documentation.
- Evidence: `docs/design/REMAINING_WORK.md:104-106` says none of the nine folders under `include/SushiRuntime/` carries a `README.md`. `include/SushiRuntime/execution/README.md`, `include/SushiRuntime/topology/README.md` and `include/SushiRuntime/api/README.md` exist. In my scope only `include/SushiRuntime/interop/` lacks one.
- Change: narrow the backlog line to the folders that still lack one.
- Breaks API: no. Effort S.

### Not checked

- Nothing was built, run or measured. Every performance item is a reading of the code.
- `docs/CONTRIBUTING.md` and `docs/architecture/ARCHITECTURE.md` were not read whole; I read ARCHITECTURE §6 (lines 566-590) and grepped both. `docs/design/REMAINING_WORK.md` was read whole. The prior audit was grepped, not read.
- Not opened: `include/SushiRuntime/topology/hwloc_topology_provider.hpp`, `remapping_topology_provider.hpp`, `matrix_distance_model.hpp`; the first 28 lines (licence and `@file`) of most files; `memory/usm_registry.hpp` and `graph/node.hpp` beyond grep hits; `core/sycl_compat.hpp` beyond three lines.
- `hw_discovery.hpp`, the interop headers and the topology headers were read with blank lines stripped, so findings on them cite symbols, not line numbers.
- The scheduler side of dispatch (`src/scheduler/scheduler.cpp`) is outside my scope; I read lines 684-696 only, to confirm the caller of `get_numa_local_queue`.
- Whether the SushiEngine repository calls any RuntimeContext member through `unsafe_context()`.
- Handle ownership after `import_external_memory` in the SYCL bindless-images specification.
- AdaptiveCpp compilation of `interop.cpp` and `chain_run.cpp` (EXEC-14), behaviour on a machine with an Intel GPU on two backends (EXEC-20), on a multi-socket machine (EXEC-08, EXEC-21) and on more than 64 logical cores (EXEC-13).
- The tests for these modules were listed, not read.

## Distributed layer (DIST)

Read-only. No file in the repository was written. No `sr`, `cmake`, `ninja` or `ctest` process was started.

### Facts

Measured with `wc -l`, `grep` and full reads; line numbers are from the working tree at `bfbc137` plus the two uncommitted files.

- The CMake option is `SR_ENABLE_DISTRIBUTED`, not `SUSHI_ENABLE_DISTRIBUTED` (`cmake/SushiRuntimeProjectOptions.cmake:35`). It defines `SR_DISTRIBUTED` as a PUBLIC compile definition (`src/CMakeLists.txt:109`).
- Sizes: `include/SushiRuntime/distributed/` 9 headers plus README, 1518 lines (the brief says 10 headers; there are 9). `src/distributed/` 7 sources, 2558 lines. `protocol.hpp` 419, coordinator 713, worker agent 641, TCP 499, in-process 392.
- OFF configuration: `src/CMakeLists.txt:19-26` removes `distributed/*.cpp` from the library. `install(DIRECTORY include/)` at `src/CMakeLists.txt:179` still installs all nine distributed headers. None of them contains an `SR_DISTRIBUTED` guard (grep count 0 in the headers).
- `SR_DISTRIBUTED` conditionals outside the layer: 13 public headers sites in `api/` (buffer, state, cluster, runtime, dynamic_graph, run_report_builder), `execution/`, `graph/node.hpp`, `scheduler/`; 16 sites in `src/` outside `src/distributed`.
- Tests: 9 E2E, 2 Jepsen, 16 unit. Exactly one test uses `TcpTransport` (`tests/functional/integration/test_distributed_e2e.cpp:546`, the loopback happy path). Every fault test runs on `InProcessTransport`.
- One benchmark exists: `tests/benchmark/performance/test_distributed_offload_perf.cpp:183`, in-process only, prints tasks/s and per-task latency. It does not vary payload size and does not count copies or allocations. No TCP benchmark exists.
- `grep -rn "SIGPIPE\|MSG_NOSIGNAL\|SO_NOSIGPIPE" src include tests examples` returns nothing.
- `PROTOCOL_VERSION` is 1 (`protocol.hpp:47`) and is read at one place, the HELLO handler (`distributed_coordinator.cpp:157`).
- `FrameHeader` (`protocol.hpp:105`) has no user anywhere in `src`, `include` or `tests`.
- The `kernels` argument of `DistributedCoordinator` is stored (`distributed_coordinator.cpp:301`) and never read.
- `WorkerAgent::run()` (`worker_agent.cpp:609`) has no caller in `tests/` or `examples/`.

#### Steps a user takes today to run one graph on two machines

Counted from `examples/distributed_demo.cpp` and the guide.

1. Build both sides with `sr build --distributed`.
2. Write the kernel as a `RemoteKernel` over untyped `void*` spans (`distributed_demo.cpp:64-80`).
3. Register it in a `KernelRegistry` on the worker (`:102-103`).
4. Write a worker `main`: `RuntimeContext`, `make_worker`, `WorkerAgent`, `start()`, a signal handler, a 200 ms sleep loop, `stop()` (`:98-118`).
5. Parse `--master=host:port`, `--port`, `--workers` by hand (`:84-96`, `:208-220`).
6. On the master, `TcpTransport::make_master(port, workers)` (`:124`).
7. Call `enable_distributed_master` with an empty `KernelRegistry` the master never reads (`:129`).
8. Pass `LeastLoadedOffloadPolicy(0)`, or nothing under 64 KiB offloads (`:128`).
9. Poll until the workers register, before `compile()`, because `stamp_offloadable` reads `distributed_supports` at compile time (`:132-139`, `src/graph/task_graph.cpp:195-202`).
10. Register each buffer with its byte size (`:153-155`).
11. Set `meta.op_id` and pack scalars into the eight `uint64_t` params (`:158-161`).
12. Write the local fallback lambda, which repeats the kernel body (`:163-173` repeats `:72-79`).

Steps 4, 5, 7, 9 and 12 can go, and 8 and 10 can shrink; DIST-08, 09, 10, 11 give the changes.

#### What an operator gets today

- Start: no worker executable ships. The only one is the demo. Stop: SIGINT or SIGTERM in the demo's own handler.
- Configuration: C++ setters only (`set_heartbeat_interval`, `set_pull_timeout`, `set_max_jobs`, `set_liveness_timeout`, `set_task_deadline`). No environment variable, no file. Fixed in source: 30 s master receive timeout (`tcp_transport.cpp:267`), 300 connect attempts at 200 ms (`:394`), `MAX_FRAME_BYTES` 256 MiB, `MAX_OFFLOAD_ATTEMPTS` 2.
- Health: five counters on the master, alive worker count. Nothing on the worker. No per-worker view (RTT, inflight, free memory are private to the coordinator).
- Logs: 14 log sites in the layer. Findings DIST-04, 05, 06, 12 list the failures that leave no line, or a line without the fix.

### Findings

Ranked by gain in usability or operability.

#### DIST-01. A lost peer can kill the surviving process with SIGPIPE on Linux
- Severity: high. Category: operability.
- Evidence: `tcp_transport.cpp:176` `auto r = ::send(s, p + sent, static_cast<int>(n - sent), 0);` with flags 0. Nothing in the repository ignores SIGPIPE or passes `MSG_NOSIGNAL`. `send_to` shuts the socket on failure (`:251`) and leaves the descriptor valid, so the next heartbeat (worker, every 1 s) or PING (master, every monitor pass) writes to it again. `src/distributed/README.md:158-159` describes exactly that second write as harmless.
- Status: hypothesis from reading. No test covers it; the only TCP test is the happy path. The settling test is a TCP loopback test that kills one side and asserts the other is still alive after two heartbeat periods.
- Change: `MSG_NOSIGNAL` on Linux, `SO_NOSIGPIPE` on macOS, in the socket adapter (DIST-21). Mark the peer closed after the first failed write so no later write happens.
- Breaks public API: no. Effort: S.

#### DIST-02. An oversized or failed output allocation terminates the worker process
- Severity: high. Category: operability.
- Evidence: `worker_agent.cpp:392` `for (const auto& h : msg.outputs) out_ptrs.push_back(resolve_output(h));` runs on the exec thread, two lines before `try` at `:394`. `alloc_cache_buffer` throws at `:232` (size above 256 MiB) and `:237` (USM allocation failed). `exec_loop` (`:482-496`) has no guard. `src/distributed/README.md:213-214` says "Both throws are contained by the pump-thread guard in `start()`", which holds only for the BUFFER_PUSH path.
- Status: read from code; no test.
- Change: move output resolution inside the `try`, convert both failures to `TASK_RESULT{false}` with a log line naming the buffer id, the requested size and the cap.
- Breaks public API: no. Effort: S.

#### DIST-03. Any host that reaches the port can read and overwrite master buffers
- Severity: high. Category: security.
- Evidence: the master binds every interface, `tcp_transport.cpp:316` `addr.sin_addr.s_addr = htonl(INADDR_ANY);`, with no bind-address parameter. `handle_message` serves `BUFFER_REQUEST` (`distributed_coordinator.cpp:185-197`) and applies `BUFFER_PUSH` (`:199-218`) without checking that the peer completed HELLO. Buffer ids are sequential from 1 (`buffer_registry.cpp`, `next_id_++`). The version fence compares against a wire value (`:206`), so a push with version `UINT32_MAX` always passes. A HELLO with the wrong version logs and breaks (`:160-162`); the connection stays open and every later message from it is still served.
- Already known: no encryption or authentication, trusted LAN only (`DISTRIBUTED_GUIDE.md:379`, `SECURITY.md:46`). Not known: the missing registered-peer gate, the open bind, and that a rejected HELLO does not close the link.
- Change: (a) `make_master` takes a bind address, default `127.0.0.1`; (b) the coordinator drops every non-HELLO message from a peer whose state is not Registered, and the transport closes a peer the coordinator rejects; (c) optional shared secret in HELLO, read from a file or environment variable, as the cheapest step above nothing.
- Breaks public API: (a) source-compatible with a deprecation of the two-argument overload; (b), (c) no. Effort: M.

#### DIST-04. A stray connection takes a worker slot for the life of the master
- Severity: high. Category: operability.
- Evidence: `tcp_transport.cpp:261` `for (int i = 0; i < worker_count && running.load(); ++i)` accepts exactly `worker_count` connections and returns. A port scan, a health probe or a worker of the wrong protocol version consumes slot `i`. When that reader exits (`:211`, `:222`, `:226`) the socket is not closed, the slot is not reopened and nothing is logged. `src/distributed/README.md:148-149` says "the connection is torn down and the slot is freed".
- Already known: reconnect of a dropped worker (`REMAINING_WORK.md:60`). This finding is the first-join case, which the backlog entry does not cover.
- Change: accept continuously; a connection becomes a worker slot only after a valid HELLO; a reader that exits closes its socket, frees the slot and logs peer address and reason. This also gives reconnect most of what it needs.
- Breaks public API: no. Effort: M.

#### DIST-05. A slow link turns large buffers into dead workers and silent local runs
- Severity: high. Category: operability.
- Evidence: liveness refreshes only when a whole frame has arrived (`distributed_coordinator.cpp:145-149`, called from `tcp_transport.cpp:228` after `read_n` of the full payload). The worker's heartbeat shares the per-socket send mutex with the result it is sending (`tcp_transport.cpp:245`), so no heartbeat leaves during a long write. Liveness timeout is 5 s (`:92`), pull timeout 5 s (`worker_agent.cpp:101`). At 100 Mbit/s a 64 MiB frame takes about 5.4 s. After the reap the worker stays `alive = false` on the master while `registered` stays true on the worker (`worker_agent.cpp:473`), so it never sends HELLO again and never rejoins, with a healthy connection.
- Status: hypothesis; arithmetic from constants. No test or benchmark throttles bytes (the fault harness holds or drops whole messages). Settling it needs a rate-limited transport decorator and a test at 100 Mbit/s with a 64 MiB buffer.
- Change: count received bytes as liveness in the transport; derive the pull timeout and the liveness allowance from payload size and measured RTT; on a reap, tell the worker (or have the worker re-HELLO when it sees an unknown-task drop) so it can rejoin.
- Breaks public API: no. Effort: M.

#### DIST-06. A version mismatch is invisible on the worker and repeats on the master every second
- Severity: medium. Category: operability.
- Evidence: `distributed_coordinator.cpp:160` `SR_LOG_ERROR("Worker {} HELLO rejected: protocol v{} != v{}.", ...)` then `break`; no reply. The worker re-sends HELLO each heartbeat until acknowledged (`worker_agent.cpp:473-474`), so the master logs the same ERROR once a second and the worker logs nothing. The message names neither the peer address nor the fix.
- Change: add `REGISTER_REJECT{reason, master_version}`; the worker logs "master speaks protocol v2, this binary v1; rebuild the worker from the master's commit" once and stops; the master logs once per connection and closes it.
- Breaks public API: no (wire protocol version bump). Effort: S.

#### DIST-07. The worker never learns the master is gone
- Severity: medium. Category: operability.
- Evidence: `ITransport` (`i_transport.hpp:41-96`) has no peer-state callback and `send` returns `void`. When the master dies, `reader_loop` returns without a log line (`tcp_transport.cpp:211`), `WorkerAgent` keeps heartbeating, and `send_to` logs `"TCP send to peer -1 failed; tearing down the connection."` (`:250`) on every heartbeat. `WorkerAgent::run()` never returns. `src/distributed/README.md:158` claims "the worker notices the master is gone".
- Change: add `on_peer_state(peer, Connected | Lost, reason)` to `ITransport`; `WorkerAgent` stops (or reconnects, once that exists) and logs once; the coordinator reaps at once instead of waiting 5 s. `send` returns a status.
- Breaks public API: yes for third-party `ITransport` implementations (a new pure virtual); none exist in the tree. Effort: M.

#### DIST-08. The master must pass a `KernelRegistry` it never reads, and the user writes every kernel twice
- Severity: medium. Category: api.
- Evidence: `distributed_coordinator.cpp:56` and `:301` are the only uses of `kernels`. The demo passes an empty one (`distributed_demo.cpp:129`) and repeats the kernel body as the fallback (`:72-79` against `:163-173`).
- Change: give the master registry a job. When the master holds the same registry as the workers, `add_task` with an `op_id` and no lambda takes the registered kernel as its local fallback. The parameter becomes optional; an op the master does not have keeps today's explicit lambda.
- Breaks public API: source-compatible with a deprecation of the three-argument form. Effort: M.

#### DIST-09. No worker entry point ships; each application writes its own `main`, argument parser and signal loop
- Severity: medium. Category: api / operability.
- Evidence: `distributed_demo.cpp:84-118` and `:199-228`. The demo's host parser uses `strtoul` with no error check and `substr(0, npos)`. `WorkerAgent::run()` exists for this and has no caller. Addresses as flags only is already known (`REMAINING_WORK.md:70`).
- Change: one free function `Distributed::run_worker(WorkerOptions, KernelRegistry&)` that parses an `Endpoint` (`host:port`), installs SIGINT and SIGTERM handling, starts, blocks and stops; `WorkerOptions` carries heartbeat, pull timeout, queue cap and reads `SR_MASTER`, `SR_WORKER_*` from the environment. The demo worker shrinks to registering kernels and one call.
- Breaks public API: no (additive). Effort: M.

#### DIST-10. Buffer registration and the wait for workers are manual, and forgetting either gives a silent local run
- Severity: medium. Category: api.
- Evidence: `register_distributed_buffer` requires pointer and size (`runtime_context.cpp`, `SR_THROW_IF(!impl_->buffers, ...)`); `free_usm` already unregisters automatically (`runtime_context.cpp:749`), so the runtime knows the pointer's lifetime but not its registration. An unregistered pointer makes `stamp_offloadable` skip the node with no log (`task_graph.cpp:209-215`). `compile()` before workers register stamps nothing (`:202`); the demo polls for 30 s by hand (`distributed_demo.cpp:132`), and `Cluster::wait_for_workers` polls at 5 ms (`cluster.hpp:57-65`).
- Change: in Master mode, register a runtime-allocated USM block at first use by an op-tagged task, using the size the USM registry already holds; log one WARN per graph naming the op and the reason when an op-tagged node is not stamped (no live worker, buffer unknown, below `min_offload_bytes`); replace the poll with a condition variable on registration.
- Breaks public API: no. Effort: M.

#### DIST-11. A remote kernel receives pointers without sizes or types
- Severity: medium. Category: api.
- Evidence: `kernel_registry.hpp` `RemoteKernel = std::function<sycl::event(sycl::queue&, span<void* const>, span<void* const>, const Graph::TaskMetadata&)>`. The demo casts and trusts a length from `meta.get_param<uint64_t>(0)` (`distributed_demo.cpp:68-71`). The worker knows each buffer's byte size (`worker_agent.cpp:59` `CacheEntry::size`) and drops it at `:371-381`.
- Change: pass `span<const BufferView>` with `{void* data, uint64_t size_bytes}` and a typed accessor `view.as<float>()` that checks divisibility; keep the old signature as a deprecated adapter that forwards.
- Breaks public API: source-compatible with a deprecation. Effort: M.

#### DIST-12. Buffers above 256 MiB cannot cross the wire, and the failure reads as a dead worker
- Severity: medium. Category: operability.
- Evidence: the sender never checks the cap: `tcp_transport.cpp:241` `uint32_t len = static_cast<uint32_t>(payload.size());` (also truncates above 4 GiB). The receiver drops the connection at `:218-223`. `try_offload` (`distributed_coordinator.cpp:667-674`) sums input sizes and never compares a handle to `MAX_FRAME_BYTES`. Sequence: dispatch, pull, oversized push, connection dropped, worker reaped after 5 s, re-dispatch, attempt cap, local run. The log shows "missed heartbeats".
- Change: decline in `try_offload` when any input or the sum of outputs exceeds the frame cap, with one WARN naming buffer id, size and cap. Longer term, chunk BUFFER_PUSH.
- Breaks public API: no. Effort: S for the decline, L for chunking.

#### DIST-13. Each offloaded buffer is copied four times and zero-filled twice on top of the socket copies
- Severity: medium. Category: performance.
- Evidence, input path: `distributed_coordinator.cpp:192` `push.data.resize(...)` (zero-fill), `:193` memcpy from USM, `:194` `serialize()` copies again through `put_blob` (`protocol.hpp:141`, growth by `insert`, no `reserve`); worker `tcp_transport.cpp:225` `std::vector<std::byte> payload(len)` (zero-fill), `protocol.hpp:332` `m.data.assign(...)`, `worker_agent.cpp:340` memcpy to USM. Output path is the mirror: `worker_agent.cpp:420`, `protocol.hpp:284`, `tcp_transport.cpp:225`, `protocol.hpp:302`, `distributed_coordinator.cpp:254`. `InProcessTransport::enqueue` adds one more copy.
- Status: hypothesis on cost. The existing benchmark runs one payload size in process and reports only tasks/s; none isolates copies. Settling it needs `benchmark_distributed_offload` over TCP loopback at 64 KiB, 4 MiB and 64 MiB, five runs each.
- Change: scatter send (header, then the USM span, with `writev`/`WSASend`) and receive the blob straight into the destination allocation; deserialisers return spans into the frame instead of owning vectors.
- Breaks public API: yes for `ITransport::send` if it gains a two-span form; source-compatible if added as an overload with a default that concatenates. Effort: L.

#### DIST-14. `try_offload` blocks a scheduler worker thread on a socket write and logs per task
- Severity: medium. Category: performance.
- Evidence: called from the scheduler loop (`src/scheduler/scheduler.cpp:505`). It ends in `impl_->transport->send(chosen, MsgType::TASK_DISPATCH, bytes);` (`distributed_coordinator.cpp:705`), a blocking `write_n` under the per-peer mutex, which a 256 MiB BUFFER_PUSH to the same worker holds for its whole duration (`:195` on the reader thread). The same call takes the coordinator mutex once, the registry mutex twice per input and three times per output (`:649-699`), copies two handle vectors (`:663-664`), allocates the frame, inserts into an `unordered_map`, and logs at INFO per task (`:707`), which the logging rule forbids on a per-element path.
- Status: hypothesis; no benchmark measures scheduler-thread stall. Same benchmark as DIST-13 with a second task dispatched to a worker during a large push.
- Change: a per-peer outbound queue drained by one sender thread, so `send` enqueues and returns; snapshot handles under one registry lock; replace the INFO with the counter that already exists and a TRACE.
- Breaks public API: no. Effort: M.

#### DIST-15. The wire format writes struct memory, has no magic, and carries its version in one message only
- Severity: medium. Category: security / api.
- Evidence: `protocol.hpp:235` `for (const auto& h : inputs) w.put(h);` writes `BufferHandle` (`u64, u32, u64`) raw, including four uninitialised padding bytes per handle, a small memory disclosure next to the README's care about `meta.name` (`include/.../README.md:40-43`). `w.put(meta.op_id)` writes a struct (`:230`). `r.get<Graph::TaskType>()` (`:248`) accepts any enum value. The frame is `[u32 length][u8 type]` (`tcp_transport.cpp:210-215`) with no magic. `PROTOCOL_VERSION` is a hand-edited constant with no test tying it to the layout. `FrameHeader` (`:105`) is unused and its `sizeof` is 8, not the 5 bytes on the wire.
- Already known: native-endian length (`REMAINING_WORK.md:67`).
- Change: field-by-field little-endian encoders per message, a four-byte magic and the version in the first frame of every connection, a table of protocol versions in the module README, range checks on enums, delete `FrameHeader`. One `static_assert` or golden-bytes unit test per message so a layout change fails a test until the version is bumped.
- Breaks public API: wire-breaking (version 2); C++ API no. Effort: M.

#### DIST-16. Master reader threads have no exception guard
- Severity: medium. Category: security / operability.
- Evidence: the worker wraps its handler (`worker_agent.cpp:577-592`); the coordinator does not (`distributed_coordinator.cpp:433-437`). `tcp_transport.cpp:225` allocates up to 256 MiB per frame on a wire length, and `:192` allocates a buffer-sized vector, both on a `std::thread` body, so `bad_alloc` calls `std::terminate`. A connected peer can trigger the first at will.
- Change: one guard in the transport's reader loop (so every transport user gets it), logging peer, type and length, then closing that peer.
- Breaks public API: no. Effort: S.

#### DIST-17. Transport errors do not name the endpoint or the cause, and hostnames are rejected
- Severity: medium. Category: operability.
- Evidence: `tcp_transport.cpp:322` `"TcpTransport: bind() failed for master."` (no port, no `errno`; the common cause is a port in use). `:411` `"TcpTransport: connect() to master failed (master not reachable)."` after a silent 60 s (`:394-408`, no progress line). `:390-391` accepts dotted IPv4 only through `inet_pton`, although `<netdb.h>` is included and the guide writes `--master=host:port` (`DISTRIBUTED_GUIDE.md:103`). `WSAStartup`'s result is discarded (`:73`). All three throw across the module surface; the throw rule is already known (prior audit, C-series "52 throw sites").
- Change: resolve with `getaddrinfo` (IPv4 and IPv6), put host, port and the OS error text in each message with the fix ("port 5555 is in use; pick another with --port or stop the other master"), log one line per ten connect attempts, return a status from `start()`.
- Breaks public API: `start()` returning a status is breaking for `ITransport`; messages alone are not. Effort: S for messages, M for the status.

#### DIST-18. The `ITransport` contract is unwritten, so a third transport copies behaviour from two sources
- Severity: medium. Category: api / layout.
- Evidence: both implementations override the same five methods and both use master = -1 as sender and ignore `peer_id` on a worker (`tcp_transport.cpp:60`, `:479`; `in_process_transport.cpp` `MASTER_ID`, worker branch of `send`). `WorkerAgent` addresses the master as `0` (`worker_agent.cpp:117`). `i_transport.hpp:51` says only "The ID of the sending node". Nothing states which thread runs the handler, whether `send` may block, whether the handler may call `send`, that `on_message` must precede `start`, or that `peer_count` is the configured count and not the connected one. Construction differs: static factories with a private constructor (`tcp_transport.hpp`) against a public constructor taking `(fabric, node_id)`. `InProcessFabric` exposes `impl_` as a public data member.
- Answer to the seam question: yes, a third transport is one header and one source with no registration anywhere, because the user constructs it. What is missing is the contract, not the seam.
- Change: write the contract into `i_transport.hpp` (peer ids as a named constant `K_MASTER_PEER`, threading, blocking, ordering); add one conformance test suite parameterised over transports; give both the same factory shape (`make_master`, `make_worker`); make `InProcessFabric::impl_` private with `InProcessTransport` as friend.
- Breaks public API: source-compatible with a deprecation for the in-process constructor. Effort: M.

#### DIST-19. Coordinator and worker agent each hold four responsibilities in one file
- Severity: medium. Category: layout.
- Evidence: `distributed_coordinator.cpp` holds membership and liveness (`:65-70`, `:471-512`), the pending-task table and deadline (`:72-82`, `:519-548`), message handling with buffer transfer (`:141-285`), and dispatch (`:619-710`). `worker_agent.cpp` holds the buffer cache (`:59-62`, `:195-259`, `:501-507`), duplicate suppression (`:75-98`), registration and heartbeat (`:110-118`, `:464-477`), and the job queue with kernel execution (`:356-435`, `:482-496`). The `switch (type)` shape is already known (prior audit, "switch (type) at distributed_coordinator.cpp:159 and worker_agent.cpp:284").
- Change, coordinator: `worker_directory` (stats, last seen, op support, reap), `pending_tasks` (table, deadline, inflight accounting), `buffer_exchange` (serve request, apply fenced writeback; shared by BUFFER_PUSH and TASK_RESULT, which today repeat the fence at `:206-216` and `:247-254`), `offload_dispatcher` (`try_offload`), with `DistributedCoordinator` composing them. Worker: `worker_buffer_cache`, `dispatch_deduplicator`, `registration_heartbeat`, `dispatch_executor`. One handler per `MsgType` in a table. Module boundaries are the owner's call.
- Breaks public API: no. Effort: L.

#### DIST-20. The inflight count drifts after a late or foreign result
- Severity: low. Category: operability.
- Evidence: `distributed_coordinator.cpp:231-233` decrements `workers[peer].inflight` for every TASK_RESULT, including one whose `task_id` is no longer pending. `reap_timed_out_tasks` already decremented for that task (`:540-541`). The second decrement removes a count that belongs to another task, and the policy ranks on `inflight`.
- Status: read from code; no test asserts inflight.
- Change: decrement only when the id was found in `pending`, using the worker recorded there, inside `pending_tasks` (DIST-19).
- Breaks public API: no. Effort: S.

#### DIST-21. Platform branches sit inside the transport source
- Severity: low. Category: layout.
- Evidence: seven `#if defined(_WIN32)` blocks in `tcp_transport.cpp` (`:38`, `:68`, `:86`, `:102`, `:128`, `:332`). The prior audit counted these and did not review them (REPORT.md:1214). On Windows `SO_REUSEADDR` (`:311`) lets a second process bind the same port; `SO_EXCLUSIVEADDRUSE` is the safe option there.
- Change: a `socket` adapter with `socket_posix.cpp` and `socket_windows.cpp` (open, close, shutdown, options, exact read and write, last error text). DIST-01 and DIST-17 land in it.
- Breaks public API: no. Effort: M.

#### DIST-22. The worker's buffer cache only grows
- Severity: low. Category: operability.
- Evidence: entries leave `cache` only in `free_all()` at `stop()` (`worker_agent.cpp:501-507`). No message tells a worker that the master unregistered a buffer (`MsgType`, `protocol.hpp:87-98`). A long run that allocates and frees buffers accumulates USM on the worker up to 256 MiB per id.
- Status: hypothesis for long runs; no soak measures worker memory.
- Change: `BUFFER_RELEASE{id}` sent on `unregister_buffer`, plus a byte budget with least-recently-used eviction and a worker-side counter.
- Breaks public API: no (wire version bump). Effort: M.

#### DIST-23. Distributed headers install in an OFF build, and the public API changes shape with the flag
- Severity: low. Category: api.
- Evidence: `src/CMakeLists.txt:179` installs `include/` whole; the nine headers have no `SR_DISTRIBUTED` guard, so an OFF consumer that includes `tcp_transport.hpp` compiles and fails at link with unresolved symbols. `Runtime::cluster`, `API::Cluster`, `Buffer::distributed`, `State::distributed` exist only under the macro (`runtime.hpp:224`, `cluster.hpp:44`, `buffer.hpp:382`, `state.hpp:184`), so an application that supports both builds needs its own `#ifdef`. `DistributedStatistics` in the run report exists in both and stays zero when OFF (`run_report_builder.hpp:80-85`).
- Already known: the preprocessor weave across five modules and the proposed offload seam (prior audit C9); the PUBLIC define rationale (`src/CMakeLists.txt:83-107`).
- Change: `#error` with the fix text at the top of each distributed header when `SR_DISTRIBUTED` is absent ("built without the distributed layer; reconfigure with sr build --distributed"). Keep `Buffer::distributed()` and `State::distributed()` in both configurations as no-ops, so only code that names a transport needs the flag.
- Breaks public API: no. Effort: S.

#### DIST-24. Stale statements in the guide and the two READMEs
- Severity: low. Category: documentation.
- `DISTRIBUTED_GUIDE.md:219-221` "Output bytes are **lazy**" contradicts `:346-348` and the code (`worker_agent.cpp:412-424`): outputs travel inline in TASK_RESULT, always, whole.
- `:189` and `protocol.hpp:90` "REGISTER_ACK: agreed common kernel set"; the payload is empty (`distributed_coordinator.cpp:181`).
- `:162` the worker sample uses `std::span`; the type is `SushiRuntime::span` and the project is C++17.
- `:103` `--master=host:port`; only dotted IPv4 parses (DIST-17).
- `:288-294` telemetry list omits `distributed_stale_writeback_count`; `ARCHITECTURE.md:913-916` omits it and `distributed_set_task_deadline`.
- `:329` "Five mechanisms compose" precedes a list of seven.
- `:310-312` "byte-for-byte identical above the transport" over TCP rests on one happy-path test.
- `src/distributed/README.md:148-149`, `:158`, `:213-214`: see DIST-04, DIST-07, DIST-02.
- `src/distributed/README.md:45-48` records that the old comment said "every live worker" while `supports()` returns true for any; the guide (`:173-174`) says "some live worker". The code and guide agree; the README paragraph should state the rule and drop the history.
- `include/.../README.md:27` "Both ends share the same binary" is an assumption nothing checks; HELLO carries no build id.
- Change: correct each line in the pass that moves the guide to the humanizer register (em-dash density already known, REPORT.md:658).
- Breaks public API: no. Effort: S.

#### DIST-25. `protocol.hpp` leans on transitive includes
- Severity: low. Category: layout.
- Evidence: uses `std::byte`, `std::is_trivially_copyable_v` and `std::move` (`:124-145`) with `<cstdint>`, `<vector>`, `<cstring>` only. Eleven types in one header is already known (REPORT.md:1090).
- Change: add `<cstddef>`, `<type_traits>`, `<utility>`; split by the known finding when DIST-15 rewrites the encoders.
- Breaks public API: no. Effort: S.

### Not checked

- Nothing was built or run. Every behavioural claim above is from reading; DIST-01, 05, 13, 14, 20, 22 are labelled hypotheses and name what would settle them.
- `tests/functional/integration/test_distributed_e2e.cpp`, `test_distributed_jepsen.cpp` and `tests/functional/unit/test_distributed.cpp`: test names and the transport used were grepped; the bodies were not read. A test I did not see may already cover part of DIST-20.
- `tests/benchmark/performance/test_distributed_offload_perf.cpp`: only the output lines and setup were read.
- `src/distributed/kernel_registry.cpp` was not opened (83 lines); its header was.
- `src/scheduler/scheduler.cpp` `complete_offloaded_node`, `src/execution/execution_engine.cpp:386` and `:592`, `include/SushiRuntime/scheduler/event_polling.hpp` distributed blocks, and `node.hpp` `OffloadState`: touching points seen through grep and short excerpts only. The reference-count handover between scheduler, coordinator and the resubmit hook was not verified.
- `api/graph/graph.hpp` offload emitters (`:1706-1780`, `:2921-2940`), `state.hpp:184-204` and `dynamic_graph.hpp:927`: located, not read.
- `docs/CONTRIBUTING.md` and `docs/architecture/ARCHITECTURE.md` were grepped for distributed content, not read whole; ARCHITECTURE section 12 (`:999-1063`) was not read line by line. `docs/reference/SECURITY.md` was grepped only.
- The prior audit report was grepped for overlaps on the terms listed in my commands; an overlap phrased in other words may exist.
- Data races between a master-side writeback `memcpy` and a concurrent `free_usm` or local writer (`distributed_coordinator.cpp:216`, `:254`) were noticed and not analysed.
- macOS behaviour, IPv6, and Windows socket semantics beyond the `SO_REUSEADDR` note.

### Dispatch block evidence

1. SOLID: findings DIST-18, 19, 21 judge brick shape and sibling symmetry explicitly; nothing was written.
2. Humanizer: skill loaded; report written against `generic_english` rules (facts with file:line, no closing summary).
3. No `sr`, `se`, `cmake`, `ninja` or `ctest` process started; no build configuration file written.
4. Files written in the repository: none. `git status --short` after the audit shows only the two files that were already modified at the start: `M docs/reference/CHANGELOG.md`, `M include/SushiRuntime/api/runtime/runtime.hpp`.
5. Done and not done are stated above under Findings and Not checked.
6. No source written; nothing to syntax-check.
7. Scratchpad: I created nothing in it. `du -sh` on the scratchpad path reports `8.0K`, which is not from this audit.

## Documentation (DOCS)

Read-only. Nothing was written in the repository. No source written; nothing to syntax-check. `sr`, `cmake`, `ninja` and `ctest` were not run.

### Facts

- 75 tracked `.md` files, 18 643 lines. 32 are module READMEs under `include/`, `src/`, `tests/`, `tools/`, `cli/`, `examples/`; none existed on 2026-10-05.
- `docs/` now has the skill's shape: `README.md` (56-line index), `CONTRIBUTING.md`, `DOCUMENTATION_STYLE_GUIDE.md`, `publish.toml`, `getting_started/`, `architecture/`, `guides/` (4 pages), `reference/` (6 pages), `design/` (map, backlog, 4 topics, each with a status on line 2), `agent/2026_10_05_ESTATE_AUDIT/REPORT.md`, `archive/`.
- `docs/archive/` holds (listed only, not read): `agent/reports/` (2), `changelog/v0.2.0.md`, `changelog/v0.3.0_LONG_FORM.md`, `slop/` (BACKBONE_WAVE_STATUS, CODE_AND_ARCHITECTURE_REVIEW, PHYSICS_SUBSTRATE_IMPLEMENTATION, TODO), `superpowers/specs/` (6).
- Checkers, system Python:

```
$ python tools/documentation/check_docs_layout.py
docs/api-site:1: rule_docs_entries: not part of the docs/ tree
$ python tools/documentation/check_changelog.py
(no output)
```

`docs/api-site/` is an ignored leftover on disk (one warnings log from 18 August and an `html/` folder); it is not tracked.

- Relative links, throwaway script over every tracked `.md` (inline links outside code fences, file and heading anchor):

```
BROKEN-FILE docs/agent/2026_10_05_ESTATE_AUDIT/REPORT.md:36 -> LICENSE
BROKEN-FILE docs/agent/2026_10_05_ESTATE_AUDIT/REPORT.md:72 -> LICENSE
BROKEN-FILE docs/agent/2026_10_05_ESTATE_AUDIT/REPORT.md:76 -> LICENSE
BROKEN-FILE docs/agent/2026_10_05_ESTATE_AUDIT/REPORT.md:453 -> README.md
BROKEN-FILE docs/agent/2026_10_05_ESTATE_AUDIT/REPORT.md:494 -> target
BROKEN-FILE docs/agent/2026_10_05_ESTATE_AUDIT/REPORT.md:514 -> LICENSE
BROKEN-FILE docs/agent/2026_10_05_ESTATE_AUDIT/REPORT.md:518 -> LICENSE
BROKEN-FILE docs/archive/slop/PHYSICS_SUBSTRATE_IMPLEMENTATION.md:4 -> PHYSICS_SUBSTRATE_REQUIREMENTS.md [archive]
BROKEN-FILE docs/archive/slop/TODO.md:224 -> SIMULATION_ENGINE_SUBSTRATE_PLAN.md [archive]
files 75 relative links 146 broken 9
```

All nine are in the audit report (link syntax quoted in prose) or in the frozen archive. The manual, the root README and the module READMEs have no broken relative link and no broken heading anchor.

- Identifier check, throwaway script: every backticked CamelCase, `SR_*` and snake_case token and every backticked path in eleven manual pages was searched in `include/`, `src/`, `cmake/`, `tests/`, `examples/`, `cli/sushiruntime/`. Unresolved after removing prose words and placeholders: `SR_NO_DEPRECATION_NOTICE` (CLI_GUIDE:84) and `SUSHISYSTEMS_HOME` (CLI_GUIDE:357), which have no hit in this repository and presumably live in sushicore; `docs/reference/DOCS_BUNDLE.md` (CLI_GUIDE:266), stated to be in the SushiCore repository.
- Em dashes per 300 words, same method as the prior audit: ARCHITECTURE 4.0 (142 in 10 615 words), README 3.8, CONTRIBUTING 3.5, DISTRIBUTED_GUIDE 3.5, INTRODUCTION 3.4, INTEGRATION 3.4, CLI_GUIDE 2.9, ENGINE_BACKBONE_REFACTOR 5.2, PHYSICS_SUBSTRATE_REQUIREMENTS 4.1. Every page written since the audit measures 0.0: FAQ, GLOSSARY, KNOWN_ISSUES, the style guide, `docs/README.md` and all 32 module READMEs.
- Doxygen: `Doxyfile` sits at the root. `INPUT` is `docs/reference/API_MAINPAGE.md`, `docs/getting_started/INTRODUCTION.md`, `docs/guides/INTEGRATION.md`, `docs/guides/CLI_GUIDE.md` and `include/SushiRuntime/api`, with `FILE_PATTERNS = *.hpp *.h *.md`, `RECURSIVE = YES`, `EXCLUDE_PATTERNS = */detail/*`, `EXTRACT_ALL = NO`, `WARN_AS_ERROR = NO`, XML on, output `build/docs/api-site`. The last local run's log (`build/docs/api-site/doxygen-warnings.log`, 7 October, 404 lines) has 217 "not documented" lines and seven unresolved references.
- Scratchpad: two scripts written and deleted. The folder measures 9.0K afterwards with three entries I did not create.

### Prior-audit status table

| Finding | Status | Evidence |
| --- | --- | --- |
| D1 no root README | Closed, with a remainder | `README.md` exists (593 lines). It is still the whole product README; see DOCS-03 |
| D2 `docs/README.md` not an index | Closed | 56 lines, one row per page; the layout checker's reachability rule is silent |
| D3 two live changelogs | Closed | Only `docs/reference/CHANGELOG.md`; `## Unreleased` at :3, `## v0.3.0 — 2026-09-15` at :63; v0.2.0 in the archive |
| D4 changelog entry shape | Closed | `check_changelog.py` prints nothing; the long form is `docs/archive/changelog/v0.3.0_LONG_FORM.md` |
| D5 CONTRIBUTING on versioning | Closed | `docs/CONTRIBUTING.md:229-235` states the one-line rule |
| D6 two licence texts | Closed | `docs/LICENSE` is gone; `README.md:593` names PolyForm Noncommercial and links the root `LICENSE` |
| D7 backlog in `docs/slop/TODO.md` | Closed, new defect | `docs/design/REMAINING_WORK.md` exists; four of its items are stale (DOCS-08) |
| D8 `docs/slop/`, `docs/superpowers/` | Closed in `docs/`, open in source | Both folders are under `docs/archive/` or `docs/design/`. Old paths remain at `cmake/SushiRuntimeInstall.cmake:21`, `cmake/SushiRuntimeConfig.cmake.in:16`, `tests/package/CMakeLists.txt:28`, `Doxyfile:4,24` |
| D9 changelog scope and order | Closed | Entries carry scope, newest first (`CHANGELOG.md:5-12`) |
| D10 status lines | Closed, weak | All four design documents have `**Status:**` on line 2. Two say "not re-checked against the tree on 2026-10-05" |
| D11 cited paths | Closed | No unresolved path in ARCHITECTURE; `cpp_standard_migration` has no hit in source |
| D12 removed memory pool | One site left | `docs/getting_started/INTRODUCTION.md:973-974`: "the full architecture (memory pool, work-stealing scheduler, ...)" |
| D13 other stale claims | Mostly closed | `is_fused`: no hit. `interop/` is at `README.md:536`. `provides` is at `CONTRIBUTING.md:378`. `range_fn` and `raw_kernel_fn` are now parameter placeholders (`ARCHITECTURE.md:133-134`). `.gitignore:9-11` still describes `dependencies/`. New stale line at `README.md:551` (DOCS-07) |
| D14 required files absent | Closed | All present |
| D15 no module README | Closed in count, changed in kind | 32 READMEs. None for `include/SushiRuntime/interop/`, `cmake/`, `tests/`. Content is rationale, not what the module owns (DOCS-05) |
| D16 README and ARCHITECTURE duplicate | Open | `README.md:424-555` against `ARCHITECTURE.md` §3 to §12 and §16; recorded at `REMAINING_WORK.md:100` |
| D17 `docs/agent/` shape | Closed | `docs/agent/2026_10_05_ESTATE_AUDIT/REPORT.md`; old reports archived |
| D18 `docs/CLAUDE.md` | Moved, content open | Root `CLAUDE.md` and `AGENTS.md`. `AGENTS.md:8` still says read every `.md` under `docs/`, `:41` still makes `sr` mandatory, `:49` still closes with the personal line |
| D19 em dash rate | Open | Same rates as measured above |
| D20 missing changelog entries | Open | Recorded at `REMAINING_WORK.md:109` and in `KNOWN_ISSUES.md` |
| D21 no documentation checker | Closed | `tools/documentation/` has three checkers; `ci.yml:163-175` runs them on Linux and Windows |
| D22 no ruling on shapes | Closed by adoption | The tree follows the skill's work-folder shape and line-2 status; I found no written ruling |
| D23 Turkish document | Closed | Archived as `docs/archive/slop/CODE_AND_ARCHITECTURE_REVIEW.md` |
| D24 community and API files | Changed | `SECURITY.md`, `CODE_OF_CONDUCT.md` and the main page are in `docs/reference/`; Doxygen writes to the build tree. `repository-layout` puts the two community files in `.github/` and `Doxyfile` in `.config/doxygen/` |

### Reader journeys

**(a) Newcomer, root README to a compiled quickstart.** Documents: `README.md`, `INTRODUCTION.md`, `CLI_GUIDE.md`, `examples/README.md` (4, about 2 070 lines). Breaks:

1. No step says to clone the repository. `README.md:40-53` offers the SushiStack script; whether it clones SushiRuntime is not stated.
2. Three install routes fork with no rule for choosing: the script (`README.md:48`), "install the CLI and run `sr setup`" (`:55`), `hub install-cli` (`:84`). The second never says how to install the CLI without `hub`; `pipx install ./cli` appears only at `CLI_GUIDE.md:34` and `FAQ.md:46`.
3. `sr setup` and `sr doctor` are absent from the command sequences at `README.md:83-111` and `INTRODUCTION.md:48-53`.
4. The fluent program at `INTRODUCTION.md:83-120` has no compile or run step. The only compile instructions are at `:520-538`, 400 lines later, for the low-level program, and they tell the reader to add a file to `examples/CMakeLists.txt` inside the runtime's tree.
5. `examples/quickstart.cpp` is the same idea already built as `sr_quickstart` (`examples/CMakeLists.txt:60`). No page says `sr run sr_quickstart`. The README names the examples without the `sr_` prefix (`README.md:541-544`).
6. The CMake snippet at `INTRODUCTION.md:526-531` claims to follow "the same pattern the project already uses". The project uses `sushiruntime_add_example(name source)` (`examples/CMakeLists.txt:44-58`), which also applies the build policy and stages the DLL on Windows; the hand-written snippet does neither.
7. `INTRODUCTION.md:17-18` sends the reader to the README "for the architecture and design"; the architecture page is `docs/architecture/ARCHITECTURE.md`.

**(b) Integrator.** Documents: `README.md:71-77`, `INTEGRATION.md`, `tests/package/README.md`, optionally `FAQ.md` (3 to 4). Breaks:

1. `INTEGRATION.md:129`: "See the redistributable notes in `README.md`." The README has no such notes; the word does not occur in it.
2. `INTEGRATION.md:67-70` installs with a bare `cmake --install build --prefix`, while `sr package` exists for this and `AGENTS.md:41` forbids calling cmake directly. The two pages do not mention each other.
3. `INTEGRATION.md:122-125` covers only `IntelSYCL`. A consumer on AdaptiveCpp, one of three supported toolchains, gets "configure it yourself".
4. No complete consumer `CMakeLists.txt` is shown; the reader is sent to `tests/package/`.
5. `INTEGRATION.md:204` cites ARCHITECTURE "§9.1 for what the pool guarantees and §3 for the run API"; those numbers hold today.

**(c) Operator, Windows and Linux.** Documents: `README.md`, `CLI_GUIDE.md`, `FAQ.md`, `cli/README.md` for anything about environment variables (4). Breaks:

1. No per-platform prerequisite list. Windows needs Visual Studio build tools (`vs_vcvars` in `cli/config.toml:31`) and vcpkg; this is visible only in the commented config block.
2. Configuration precedence is stated three ways: `cli/config.toml:4` says "SUSHI_* env vars -> CLI flags", `cli/README.md:10-11` says "`SR_*` environment variables", `CLI_GUIDE.md:43-58` names no environment layer. The code reads `SR_SYCL_TOOLCHAIN`, `SR_ONEAPI_ROOT`, `SR_LLVM_ROOT`, `SR_ICX`, `SR_CC`, `SR_ACPP`.
3. `README.md:87` and `CLI_GUIDE.md:54` say to copy "the commented Windows block" by hand; `CLI_GUIDE.md:52` says `sr setup` writes the file. The pages do not say when the hand route is needed.
4. No troubleshooting page for an install. Known causes (mixed oneAPI and intel-llvm library paths, `CUDA_LAUNCH_BLOCKING`) are in `cli/README.md:227-232` only.
5. `sr doctor --for test` (`CLI_GUIDE.md:349`) names one group; the list of groups is nowhere.

**(d) Contributor.** Documents: `CONTRIBUTING.md`, `ARCHITECTURE.md`, `DOCUMENTATION_STYLE_GUIDE.md`, the module README, `design/README.md` (5, about 1 800 lines plus the README). Breaks:

1. `CONTRIBUTING.md:158-170` requires a why-and-how Doxygen block with every parameter documented. The `source-comments` skill, which `CONTRIBUTING.md:149` cites for the licence block and CI enforces through `check_source_comments.py`, requires what a thing does, `@param` only for a rule, eight lines at most. The second example in CONTRIBUTING (`:202-215`) is fourteen lines.
2. `CONTRIBUTING.md:105-106` says a comment "explains *why*"; the module READMEs now hold the why and source comments hold one invariant line (`src/README.md:6-8`). CONTRIBUTING does not mention module READMEs at all.
3. `CONTRIBUTING.md:36` names two toolchains and omits AdaptiveCpp.
4. `CONTRIBUTING.md:282-283` says code samples "are checked against the real API"; `REMAINING_WORK.md:112` says they were never checked. No mechanism compiles them.
5. No path-to-layer table. The directory map exists twice (`README.md:524-555`, `ARCHITECTURE.md:1255-1279`) and neither says where a test for a layer goes.

### Findings

Ranked by how much each helps a reader reach working code or a working install.

**DOCS-01, high, S. The first program has no compile and run step.**
Evidence: `INTRODUCTION.md:81` "Here is a complete program"; next build instruction at `:520`; `examples/CMakeLists.txt:60` `sushiruntime_add_example(sr_quickstart quickstart.cpp)`; no page contains `sr run sr_quickstart`.
Change: `INTRODUCTION.md` §1 gains three lines after the verify step: `sr build`, `sr run sr_quickstart`, and a sentence that the program below is `examples/quickstart.cpp`. §4 "Compiling and running it" replaces its six-line CMake block with the one-line `sushiruntime_add_example(...)` call and a pointer to INTEGRATION for a program outside the tree. `README.md:541-544` gains the `sr_` prefixes.

**DOCS-02, high, M. Install has three routes and no decision rule; the no-`hub` route is not written down where it is promised.**
Evidence: `README.md:40-63`, `:81-85`; `INTRODUCTION.md:24-59`; `CONTRIBUTING.md:19-43`; `CLI_GUIDE.md:26-35`; `FAQ.md:21-50`. Five pages restate the install, each slightly differently.
Change: one new section "Install" in `docs/getting_started/INTRODUCTION.md` (or a new `docs/getting_started/INSTALL.md`, the owner's call) holds the single sequence: clone, install the CLI (`pipx install ./cli`, or `hub install-cli` inside a workspace), `sr setup`, `sr doctor`, `sr build`, `sr test`, with a Windows and a Linux prerequisite list. README, CONTRIBUTING §1, CLI_GUIDE "Installing the CLI" and the FAQ each lose their copy and link to it.

**DOCS-03, high, M. The root README is a 593-line manual, against the layout rule of "what the project is, one build line, a link".**
Evidence: `README.md:240-422` (API tour, duplicates `INTRODUCTION.md` §2 and §4), `:424-520` (layers, duplicates ARCHITECTURE §3 to §12), `:522-555` (directory map, duplicates `ARCHITECTURE.md:1255-1279`), `:557-589` (distributed, duplicates DISTRIBUTED_GUIDE). Recorded as open at `REMAINING_WORK.md:100`.
Change: README keeps lines 1-16, a short requirements list, the install link, one 20-line example and the licence paragraph. "Building with CMake directly" and the GPU backend paragraph (`:116-164`) move to ARCHITECTURE §14, which already holds the options. Target about 100 lines.

**DOCS-04, high, S. CONTRIBUTING's comment rules contradict the rule the checker enforces.**
Evidence: `CONTRIBUTING.md:160-163` "why the function exists ... how it does it ... document every parameter"; `:105` "explains *why*"; `:271-274` "a missing `@param` on a public symbol is a doc gap CI reports"; against `source-comments` (a comment says what, `@param` only for a rule, block of eight lines) and `REMAINING_WORK.md:42-46`, which counts 295 blocks over the ceiling with the checker while citing CONTRIBUTING §4 as the rule.
Change: CONTRIBUTING §4 "Doxygen documentation" is replaced by a paragraph naming the `source-comments` rule, the checker command and the module README as the home of reasoning. `REMAINING_WORK.md:42-43` cites the same rule.

**DOCS-05, high, L. Module READMEs hold rationale and no module facts.**
Evidence: `include/SushiRuntime/api/README.md:16` "The sections below record why the code is the way it is"; `src/README.md:6` "records the reasoning behind its sources"; `tests/functional/README.md:3-4`. `repository-layout` rule 5: a module README states what it owns, what it depends on and its public entry points. `source-comments`: why a design was chosen goes to `docs/design/<TOPIC>.md`. None of the 32 READMEs states dependencies or lists entry points.
Change, which needs the owner's ruling because it moves about 8 000 lines: each layer README gains a first screen (owns, depends on, public headers, where its tests are). The per-symbol rationale either stays below that screen, accepted as a house extension to the skill, or moves to `docs/design/`. I recommend keeping it beside the code and amending the style guide to say so, since the headers already cite these pages.

**DOCS-06, medium, M. The four oversized READMEs.**
- `tests/functional/integration/README.md` (1 614 lines) and `tests/functional/unit/README.md` (462): one section per test file and one subsection per test name, each a paragraph restating what the test asserts (`integration/README.md:43-46`). This is the test's own `@brief` at paragraph length. Change: keep a table of file, subject and design document; keep a subsection only where it records something the code cannot (a rendezvous technique, a measured threshold, a platform skip). Estimated result under 400 lines.
- `include/SushiRuntime/api/graph/README.md` (899): three kinds of content. Overload semantics of `add`, `add_reduce`, `add_prefix_scan` (`:228-510`) are user documentation and belong in the Doxygen blocks or in INTRODUCTION. The DynamicGraph composition model (`:10-37`) is architecture and belongs in `ARCHITECTURE.md` §4. Private-member notes (`rewire_base`, `steps_`, `regions_`, `:121-197`) are implementation rationale and can stay.
- `include/SushiRuntime/scheduler/README.md` (373): implementation rationale throughout (memory orders, backoff, the deque). It fits a README under the house extension; it duplicates `ARCHITECTURE.md` §9 in places and should cite it instead of restating.

**DOCS-07, medium, S. Claims that contradict the tree (spot check of 60 named claims; mismatches only).**

| Claim | At | Tree |
| --- | --- | --- |
| CI runs "inside the `intel/oneapi-basekit` image" | `README.md:238` | `ci.yml:16` `runs-on: ubuntu-latest` with `./.github/actions/setup-oneapi`; "basekit" occurs nowhere in `.github/` |
| "CMake 3.25 or newer required for the Intel oneAPI toolchain" | `README.md:27`, `FAQ.md:26` | `CMakeLists.txt:7` is 3.20; no 3.25 in `cmake/` or `CMakePresets.json` |
| "docs/ ... api/ holds the Doxygen main page" | `README.md:551` | `docs/api/` is gone; the page is `docs/reference/API_MAINPAGE.md` |
| "copy the commented Windows block from `config.toml`" | `README.md:87`, `CLI_GUIDE.md:54` | `[tool.windows]` is live at `cli/config.toml:25`; only the path lines under it are commented |
| "SUSHI_* env vars" | `cli/config.toml:4` | The CLI reads `SR_*`; `cli/README.md:10` says `SR_*` |
| Architecture design is "§11" | `DISTRIBUTED_GUIDE.md:10` | Distributed is §12 (`ARCHITECTURE.md:999`); §11 is concurrency |
| "redistributable notes in `README.md`" | `INTEGRATION.md:129` | No such text |
| "the full architecture (memory pool, ...)" | `INTRODUCTION.md:973-974` | The pool was removed |
| "following the same pattern the project already uses" | `INTRODUCTION.md:524-531` | `examples/CMakeLists.txt:44` uses `sushiruntime_add_example` |
| "Build it with `sr doxygen`"; "output lands in docs/api-site/html"; "docs/ARCHITECTURE.md" | `Doxyfile:4-6`, `:24` | Command is `sr docs`, output is `build/docs/api-site`, page moved |
| "`sr docs` builds the API site without the introduction ..." | `KNOWN_ISSUES.md` (Build, CLI and CI, row 1), `REMAINING_WORK.md:90-93` | `Doxyfile` `INPUT` already names the new paths (commit 6ab3303) |
| "No area README" | `REMAINING_WORK.md:104-106` | 32 exist |
| CI "has no release workflow" | `REMAINING_WORK.md:128-130` | `.github/workflows/release.yml` exists (commit 3adb2c6) |
| "`docs/api-site/.gitignore` repeats two root rules" | `REMAINING_WORK.md:136` | The file was deleted in 6ab3303 |
| "`check_docs_layout.py` reports five links in the estate audit report" | `KNOWN_ISSUES.md` (Documentation, row 1) | The checker prints one line, about `docs/api-site` |
| §7 has 7.1 and 7.3, no 7.2 | `ARCHITECTURE.md:605`, `:641` | Numbering gap |
| "see `docs/slop/ENGINE_BACKBONE_REFACTOR.md`", "See docs/INTEGRATION.md" | `cmake/SushiRuntimeInstall.cmake:21`, `cmake/SushiRuntimeConfig.cmake.in:16`, `tests/package/CMakeLists.txt:28` | Moved; the second one is installed with the package, so a consumer reads it |

Matches worth recording: every `sr` command and flag in README and CLI_GUIDE exists in `cli/sushiruntime/cli.py` or is stated to come from sushicore; all seven example files; `SR_BUILD_TESTS` OFF, `SR_BUILD_BENCHMARKS` ON, `SR_DETERMINISTIC_FP`, `SR_NATIVE_ARCH`, the six `SR_GPU_BACKEND` values, three presets; `SameMinorVersion`; `build/stage`; `target_bin = "sr_functional_tests"`; `Backend` enum; `Runtime::create(filter, config)`; `version_matches`; `LateCount`; `set_profiling_requested`; `Runtime::rebalancer()`.
Change: correct each line in place. The three CMake comments and `Doxyfile` are build configuration and need the owner's go-ahead.

**DOCS-08, medium, S. The backlog and the known-issues page have gone stale four days after they were written.**
Evidence: the four `REMAINING_WORK.md` rows and two `KNOWN_ISSUES.md` rows in DOCS-07.
Change: delete the six rows. Add one row for the leftover `docs/api-site/` folder the checker reports.

**DOCS-09, medium, M. No single home for the options and variables an operator sets.**
Evidence: `SR_SYCL_TOOLCHAIN` appears in README (3), CLI_GUIDE (2), FAQ, ARCHITECTURE (2), `cli/README.md` (3). `SR_BUILD_TESTS` in five pages, `SR_ENABLE_DISTRIBUTED` in five, `SR_GPU_BACKEND` in four. `SR_ONEAPI_ROOT`, `SR_LLVM_ROOT`, `SR_ICX`, `SR_CC`, `SR_ACPP` are read by the CLI and documented nowhere in the manual. `VCPKG_ROOT` and `CMPLR_ROOT` only at `README.md:137-139`.
Change: a new `docs/reference/CONFIGURATION.md` with three tables: CMake options (name, default, where defined), CLI config keys, environment variables. ARCHITECTURE §14 keeps the reasoning and links to it; README, FAQ and CLI_GUIDE name an option only in a command and link the table. New file, so the owner decides.

**DOCS-10, medium, S. Supported platforms and toolchains are stated in six places with different members.**
Evidence: `README.md:20-26` (three toolchains), `CONTRIBUTING.md:35-36` (two), `INTRODUCTION.md:55-56`, `FAQ.md:64-78`, `CLI_GUIDE.md:99-104`, `cli/README.md` (`TOOLCHAINS`). No page has an OS by toolchain by GPU backend table saying what is tested.
Change: one support table in the README's requirements section; the other five link to it.

**DOCS-11, medium, S. The generated reference pulls module READMEs in as pages and links out of its own scope.**
Evidence: `FILE_PATTERNS = *.md` with `RECURSIVE = YES` over `include/SushiRuntime/api` makes the six `api/**/README.md` files site pages, 899 lines of private-member rationale among them; the warnings log has `include/SushiRuntime/api/README.md:9: unable to resolve reference to '.../api/detail/README.md'`. Six more unresolved references come from the three manual pages linking `README.md` and `ARCHITECTURE.md` (`Doxyfile:24-28` expects one). `API_MAINPAGE.md` sends readers to `SushiRuntime::Core::RunReport`, `Core::NodeTiming` and `WorkerTiming`, which live in `core/` and are outside `INPUT`, so those names do not link. `Errc`, `Error` and `Result` are outside it too.
Change: `Doxyfile` gains `EXCLUDE_PATTERNS += */README.md` and adds `include/SushiRuntime/core/run_report.hpp`, `error.hpp`, `result.hpp` and `include/SushiRuntime/version.hpp` to `INPUT`. `ARCHITECTURE.md` joins `INPUT` or the three pages cite it in a code span. Build configuration; owner's go-ahead.

**DOCS-12, medium, S. The reference's version and honesty settings.**
Evidence: `Doxyfile:10` `PROJECT_NUMBER = "v1 (unreleased)"` against 0.3.0 (`CMakeLists.txt`); `WARN_AS_ERROR = NO` with 217 undocumented-member warnings in the last log; `CONTRIBUTING.md:273` says CI reports a missing `@param`, and `WARN_NO_PARAMDOC = YES` contradicts the `source-comments` rule that `@param` is conditional.
Change: `PROJECT_NUMBER` taken from the project version by the CLI; `WARN_NO_PARAMDOC = NO`; the 217 warnings become a backlog row with a count.

**DOCS-13, medium, S. INTEGRATION lacks a complete consumer and an AdaptiveCpp path.**
Evidence: `INTEGRATION.md:76-81` (four CMake lines), `:122-125`, `:226-228`.
Change: §3 gains the full `CMakeLists.txt` and `main.cpp` of `tests/package/` inline, and §4 a paragraph per toolchain. The install step names `sr package --prefix`.

**DOCS-14, medium, M. Missing module READMEs.**
Evidence: no `README.md` in `include/SushiRuntime/interop/` (its row is also missing from the table at `include/SushiRuntime/README.md:6-15`), `cmake/` (12 files, the home of every option), `tests/` root, `.github/`.
Change: add the three and the table row. `cmake/README.md` lists each file and the options it defines, which DOCS-09 then links.

**DOCS-15, medium, S. CONTRIBUTING does not tell a contributor where a change belongs.**
Evidence: journey (d), items 2, 3 and 5.
Change: CONTRIBUTING §2 gains a table: layer, header folder, source folder, test folder, README. §1 names all three toolchains by linking DOCS-10's table. `:282-283` either names the check that compiles samples or drops the claim.

**DOCS-16, medium, S. `AGENTS.md` still conflicts with the delegation rule and with the tree.**
Evidence: `AGENTS.md:8` "read every `.md` file under `docs/`" (about 11 000 lines with the archive); `:41` makes `sr` mandatory; `:49`. Recorded at `REMAINING_WORK.md:94`.
Change: line 8 becomes "read `docs/README.md` and the pages it lists for the area you touch"; the build paragraph says who may run `sr`. Owner's wording.

**DOCS-17, medium, S. Operator troubleshooting lives in a module README.**
Evidence: `cli/README.md:227-232` (the `LD_LIBRARY_PATH` and `CUDA_LAUNCH_BLOCKING` behaviour), `:116-190` (ASan on Windows, also at `CLI_GUIDE.md:141-158`).
Change: `CLI_GUIDE.md` gains a "When a build or a run fails" section with symptom, cause and command; `cli/README.md` keeps the reason and cites it. The ASan flags are stated once, in `cli/README.md`, and CLI_GUIDE keeps two sentences.

**DOCS-18, low, M. Humanizer pass over the seven older manual pages and two design documents.**
Evidence, eleven documents sampled:
- `INTRODUCTION.md:8` "The big idea: **you never wire up task dependencies by hand.** You just say"
- `INTRODUCTION.md:181-182` "is a decision, not an oversight"
- `INTEGRATION.md:36-37` "This is not a defect to be fixed. It is the cost of single-source, and it is the single most important thing an integrator needs to know."
- `INTEGRATION.md:110-111` table cells "Obviously."
- `README.md:321-322` "a graph worth measuring is worth naming"; `:328` "Deterministic reductions are first-class."
- `README.md:81` "the single install seam for the whole stack"
- `CONTRIBUTING.md:223` "Documentation is not a follow-up task — it ships"; `:228` "hard requirements, not suggestions"; `:129` "That is not hypothetical —"
- `CLI_GUIDE.md:9` "explains every command in plain English"; `:320` "just works"; `:373` "a convenience, not a requirement"
- `DISTRIBUTED_GUIDE.md:14` "The one idea you must internalize"
- `ARCHITECTURE.md:1227` "Invariants worth never breaking"
- `API_MAINPAGE.md:3-4` "it decides *when, where, and in what order* work runs"
- `cli/README.md:216-222` "Windows used to be frozen ... That stopped being true ... now has an effect": history, which belongs in the commit.
- Clean: `FAQ.md`, `KNOWN_ISSUES.md`, `examples/README.md`, `tools/README.md`.
Change: do the pass page by page as each is touched by DOCS-01 to DOCS-03; the README shrinks by 80 percent first, so its pass is small.

**DOCS-19, low, S. Design statuses that admit they were not checked.**
Evidence: `ENGINE_BACKBONE_REFACTOR.md:2` "BB-5, BB-7, BB-8 and the rest of BB-6 were not re-checked against the tree on 2026-10-05"; `SIMULATION_ENGINE_SUBSTRATE_PLAN.md:2` likewise.
Change: read §9 to §12 and §7 against the code once and set each status to `Open — phase N of M`.

**DOCS-20, low, S. Placement against the two skills.**
Evidence: `docs/reference/SECURITY.md` and `CODE_OF_CONDUCT.md` (layout skill: `.github/`); `Doxyfile` and `Dockerfile` at the root (layout skill: `.config/doxygen/`, `.config/docker/`); `docs/archive/changelog/v0.3.0_LONG_FORM.md` is not a `vX.Y.Z.md` name; ignored `docs/api-site/` on disk; `.github/ISSUE_TEMPLATE/bug_report.yml` names `SECURITY.md` by its old place.
Change: move the two community files to `.github/` and drop their rows from `docs/README.md` and the `exclude` list of `docs/publish.toml`; delete the on-disk `docs/api-site/`. The `Doxyfile` move belongs to the layout auditor.

**DOCS-21, low, S. `docs/publish.toml` publishes pages whose links leave the bundle.**
Evidence: `sections = ["getting_started", "guides", "architecture", "reference"]`; INTRODUCTION, CLI_GUIDE and FAQ link `../../README.md` (seven links), which is outside those sections. `CLI_GUIDE.md:261-262` says the bundle packs "the files they link to", so the 593-line README ships as a satellite page. KNOWN_ISSUES and CHANGELOG are published as they are, internal rows included.
Change: after DOCS-02 and DOCS-03 the manual no longer needs to link the README. Decide whether `reference/KNOWN_ISSUES.md` is public.

**DOCS-22, low, S. Two entries missing from the changelog since v0.3.0.**
Evidence: D20, still open at `REMAINING_WORK.md:109-111`. Also, the working copy carries an uncommitted changelog line dated 2026-10-09 with the matching `runtime.hpp` edit (`git diff --stat`: 2 files, 8 insertions).
Change: write the two lines.

### Not checked

- `docs/archive/`: listed only. Its two broken links were found by the script, not by reading.
- `docs/architecture/ARCHITECTURE.md`: read the outline, §16 and the lines the scripts flagged. The other 1 200 lines were machine-checked for identifiers and paths only; described behaviour and constants were not compared with the code.
- `docs/getting_started/INTRODUCTION.md` lines 231-404 and 546-960, `docs/guides/DISTRIBUTED_GUIDE.md` beyond lines 8-20 and three greps, `docs/guides/FAQ.md` beyond line 60, `docs/reference/GLOSSARY.md`, `SECURITY.md`, `CODE_OF_CONDUCT.md`, `CONTRIBUTING.md` §6 to §9, and the four design documents beyond their status lines.
- Of the 32 module READMEs I read the opening of nine and the heading outline of five; the bodies of the rest were not read. The claim that none states dependencies or entry points rests on those fourteen and on the shared opening sentence.
- Whether any C++ sample compiles. No build was allowed; I compared function and option names, not signatures or argument order.
- `sr setup`, `sr doctor`, `sr config`, `sr docs`, `sr link`: they are registered from sushicore, which is another repository. Their flags (`--dry-run`, `--toolchain`, `--for`, `--workspace`), `SR_NO_DEPRECATION_NOTICE`, `SUSHISYSTEMS_HOME` and the pipx claim at `CLI_GUIDE.md:30` are unverified.
- External URLs, the install scripts, and whether the script clones this repository.
- The Doxygen site itself. Statements about its content come from `Doxyfile` and a warnings log of 7 October, not from a fresh run.
- `.github/actions/setup-oneapi` was not opened; the CI claim rests on `ci.yml` and a grep for "basekit".
- The humanizer finding is eleven documents sampled by passage, not a sentence-by-sentence pass.
- `NOTICE.md`, `COMMERCIAL.md`, `.github/PULL_REQUEST_TEMPLATE.md`, `tools/packaging/README.md`, `tests/package/README.md`: link-checked only.

### Reply to the send-back

##### Dispatch block evidence

##### 1. SOLID

The report never uses the word, so no finding judges brick shape "by name". Six findings judge it in substance:

- DOCS-05 is the sibling-symmetry finding: the module READMEs are siblings and none of those I opened has the shape `repository-layout` rule 5 gives a module README (owns, depends on, entry points).
- DOCS-14 is the same check for missing siblings: `include/SushiRuntime/interop/`, `cmake/` and `tests/` have no README while their neighbours do, and the table in `include/SushiRuntime/README.md` lacks the `interop/` row.
- DOCS-06 is single responsibility for one document: `api/graph/README.md` holds user documentation, architecture and private-member rationale in one file.
- DOCS-03 is the same for the root README, which does four jobs (pitch, API tour, layer reference, directory map).
- DOCS-02, DOCS-09 and DOCS-10 are one home per fact: install steps in five pages, options in five, toolchain lists in six.

I did not audit the SOLID shape of code, the CLI or the checkers; that was outside the scope given.

##### 2. Humanizer

I invoked the `humanizer` skill as my first tool call, before any reading, and wrote the report against its `SKILL.md` checklist. I did not open `generic_english.md`, the language file the skill says to read before drafting. I have read it now (lines 1 to 140) and checked the report against it. Passages I would change:

| Where | As delivered | Replacement | Rule |
| --- | --- | --- | --- |
| DOCS-05, last evidence sentence | "None of the 32 READMEs states dependencies or lists entry points." | "None of the fourteen READMEs I opened states dependencies or lists entry points; the other eighteen were not read." | Uncertainty: a confident claim about unread files |
| Status table, D15 | "Closed in count, changed in kind" | "Closed: 32 READMEs exist. Their content is rationale (DOCS-05)" | Symmetric phrase standing in for the fact |
| Status table, D10 | "Closed, weak" | "Closed; two statuses record that they were not verified" | Evaluative word where the fact belongs |
| DOCS-07, after the table | "Matches worth recording:" | "Claims that matched:" | Evaluative filler |
| Facts, identifier check | "which have no hit in this repository and presumably live in sushicore" | "which this repository does not define. `CLI_GUIDE.md:339` attributes the provisioning commands to sushicore; I did not open that repository." | Hedge replaced by what was and was not checked |
| Journey (b), item 5 | Listed under "Breaks": "those numbers hold today" | Moved out of the list: "The section numbers cited at `INTEGRATION.md:204` (§9.1 and §3) are correct." | A non-break filed in a list of breaks |
| Throughout | Series without the serial comma, e.g. "`sr`, `cmake`, `ninja` and `ctest`" | With it: "`sr`, `cmake`, `ninja`, and `ctest`" | `generic_english.md:128` asks for the Oxford comma. The repository's own style guide and the dispatch block write without it, so this one needs the owner's word on which governs |

Checked and left as they are: em dashes (the report has none outside quoted evidence and the changelog heading), no closing summary, no filler connectives, and the finding titles in bold, which are headings carrying the required ID, severity and effort.

##### 3. Build processes

No `sr`, `se`, `cmake`, `ninja` or `ctest` process was started, in the foreground or the background. The processes I ran were `git` (ls-files, log, grep, diff, status, check-ignore), `python` on two scratch scripts and on `check_docs_layout.py` and `check_changelog.py`, and shell text tools (`grep`, `sed`, `wc`, `ls`, `du`, `cat`, `head`, `rm` in the scratchpad).

##### 4. `git status --short`, run now

```
 M docs/reference/CHANGELOG.md
 M include/SushiRuntime/api/runtime/runtime.hpp
```

These are the same two modified files the session's starting snapshot listed. I did not touch either; the report mentions them under DOCS-22.

##### 7. Scratchpad, run now

```
$ du -sh <scratchpad>
0	/c/Users/sushi/AppData/Local/Temp/claude/D--Projects-sushiruntime/0f9d94b3-1614-4dc1-92aa-81c116c6b584/scratchpad
$ ls -la <scratchpad>
total 0
drwxr-xr-x 1 sushi 197121 0 Oct  9 20:00 .
drwxr-xr-x 1 sushi 197121 0 Oct  9 19:52 ..
```

The folder is empty. I created two files there during the audit, `links.py` (the link check) and `ids.py` (the identifier check), and deleted both with `rm -f` before I wrote the report. A `links.py` another auditor saw while my audit was running was most likely mine; I cannot rule out that another auditor used the same name. There is no `links.py` there now, so nothing was left to delete, and I deleted nothing in this round.

One correction to the report: it says the scratchpad measured 9.0K with three entries I did not create. That was true at that moment; those entries belonged to other auditors and are gone now.

## Layout, build, packaging (LAYOUT)

Read-only. HEAD is bfbc137 (`v0.3.0-71-gbfbc137`), 358 tracked files. The working tree had two modified files before I started (`docs/reference/CHANGELOG.md`, `include/SushiRuntime/api/runtime/runtime.hpp`) and has the same two now.

Delegation block: (1) SOLID shape is the yardstick of the findings below. (2) `humanizer` loaded and applied to this report. (3) No `sr`, `cmake`, `ninja` or `ctest` process started; no build file edited. (4) Nothing written in the repository. (6) No source written; nothing to syntax-check. (7) I wrote nothing to the scratchpad; it holds 13 KB of other auditors' files (`cc.py`, `ids.py`, `links.py`, `graphapi`), which I left alone.

### Facts

#### Tree

- Root, tracked: `.dockerignore .gitattributes .gitignore .github/ AGENTS.md CLAUDE.md CMakeLists.txt CMakePresets.json COMMERCIAL.md Dockerfile Doxyfile LICENSE NOTICE.md README.md sushi-module.toml cli/ cmake/ docs/ examples/ include/ src/ tests/ tools/`.
- Root, on disk and untracked: `build/`, `build-asan/` (two dotfiles), `venv/`, `.superpowers/sdd/`, `.scratch_syntax/` (empty), and an empty `src/memory/` directory. The four `CXX-DetectStdlib-*.h` files are gone.
- Every one of the 479 project includes in `include/`, `src/`, `tests/`, `examples/` uses `<SushiRuntime/...>`. The exception is the umbrella `include/SushiRuntime/SushiRuntime.h:31-64`, which uses 22 quoted relative includes (`"core/export.hpp"`).

#### Module map

| Module | Headers | Sources | README (include / src) | Tests that name it |
| --- | --- | --- | --- | --- |
| api | 21 in 6 subfolders + `fluent.hpp` | none (header-only) | yes (+6 sub-READMEs) / n.a. | 32 of 43 integration files include only the umbrella |
| core | 18 | `version.cpp` only | yes / yes | unit: delegate, logger, ref_counted, node_event_record |
| topology | 7 | 4 | yes / yes | unit: affinity, topology, topology_abstraction, nvidia_topology |
| memory | 4 | none; `src/memory/` is an empty untracked folder | yes / no | unit: usm_registry, epoch_reclamation, allocation |
| graph | 5 | 2 | yes / yes | unit: dependency_tracker |
| scheduler | 8 | 1 (`scheduler.cpp`) | yes / yes | unit: event_polling, range_task, work_stealing_deque, thermal_rebalance, numa_steal |
| execution | 5 | 4 | yes / yes | none by name |
| interop | 5 + `detail/` 1 | 1 | **no** / yes | unit: interop_vocabulary; integration: interop_host_export |
| distributed | 9 | 7 | yes / yes | unit: distributed; integration: distributed_e2e, distributed_jepsen |

Pairing breaks: `src/core/version.cpp` pairs with `include/SushiRuntime/version.hpp`, which sits outside `core/`; `src/scheduler/scheduler.cpp` implements `task_scheduler.hpp`; `src/distributed/offload_policy.cpp` implements `i_offload_policy.hpp`.

#### Include edges between modules

Command (run in the repository root):

```
grep -rnoE '#include [<"]SushiRuntime/[a-z_]+/' include src | sed -E 's#^(include/SushiRuntime|src)/([a-z_]+)/[^:]*:[0-9]+:\#include [<"]SushiRuntime/([a-z_]+)/#\2 -> \3#' | awk '$1!=$3' | sort | uniq -c
```

Output (count of include lines, headers and sources together):

```
 24 api -> core            12 api -> execution        5 api -> graph
  1 api -> interop          2 api -> memory           2 api -> topology
 21 distributed -> core     1 distributed -> execution  5 distributed -> graph
 16 execution -> core       6 execution -> distributed  6 execution -> graph
  1 execution -> memory     4 execution -> scheduler    6 execution -> topology
 12 graph -> core           1 graph -> distributed      2 graph -> execution
  1 graph -> memory         2 graph -> scheduler
  2 interop -> api          2 interop -> core           1 interop -> execution
  2 interop -> memory
  2 memory -> core          1 memory -> execution
 16 scheduler -> core       1 scheduler -> distributed  3 scheduler -> execution
  2 scheduler -> graph      1 scheduler -> memory       1 scheduler -> topology
 10 topology -> core
```

`core` and `topology` include nothing above themselves. Against the stack in `docs/architecture/ARCHITECTURE.md:50-69` (api > execution > graph > scheduler > memory | topology > core), these edges run upward:

| Edge | Site |
| --- | --- |
| graph -> execution | `include/SushiRuntime/graph/task_graph.hpp:43`, `src/graph/task_graph.cpp:42` |
| scheduler -> execution | `include/SushiRuntime/scheduler/task_scheduler.hpp:48-49`, `src/scheduler/scheduler.cpp:47` |
| scheduler -> graph | `include/SushiRuntime/scheduler/event_polling.hpp:46`, `src/scheduler/scheduler.cpp:49` |
| memory -> execution | `include/SushiRuntime/memory/usm_allocation_owner.hpp:33` |
| interop -> api (and api -> interop) | `include/SushiRuntime/interop/host_export.hpp:32`, `interop.hpp:34`; back edge `api/runtime/runtime.hpp:39` |
| graph, scheduler, execution -> distributed (and back) | `graph/node.hpp:47`, `execution/runtime_context.hpp:47-52`, `src/scheduler/scheduler.cpp:50`, all behind `#ifdef SR_DISTRIBUTED`; back edges `src/distributed/worker_agent.cpp:46` and 5 distributed -> graph |

Five two-module cycles exist: execution/graph, execution/scheduler, execution/memory, graph/scheduler, api/interop; plus execution/distributed when the flag is on. `interop` does not appear in the ARCHITECTURE §2 diagram at all.

Layering checker:

```
$ python tools/layering/check_layering.py ; echo exit=$?
exit=0
$ python tools/layering/check_layering.py --report
rule_declared_tiers: 0
rule_upward_includes: 0
rule_private_reach: 0
total: 0
```

It passes because it checks nothing: `tools/layering/check_layering.py:23` is `K_TIER_ORDER: tuple[str, ...] = ()` and line 6 says it walks `modules/<tier>/<module>/`, a folder this repository does not have.

#### Build

- Library: one target, `add_library(sushiruntime SHARED ${SRC_FILES})` (`src/CMakeLists.txt:29`), sources gathered by `file(GLOB_RECURSE ... CONFIGURE_DEPENDS)` (`:12-15`) with `distributed/*.cpp` removed unless `SR_ENABLE_DISTRIBUTED`. `SHARED` is hardcoded; `BUILD_SHARED_LIBS` is read nowhere. 19 `.cpp` files compile in (12 without distributed). `api/` and `memory/` are header-only.
- Symbols: `WINDOWS_EXPORT_ALL_SYMBOLS ON` (`src/CMakeLists.txt:142`) and also `SUSHIRUNTIME_API` (`core/export.hpp:30-37`, 41 uses in 28 files, defined via `SUSHIRUNTIME_EXPORTS` at `src/CMakeLists.txt:76`). No `CXX_VISIBILITY_PRESET` anywhere, so Linux exports everything.
- `VERSION ${PROJECT_VERSION}`, `SOVERSION ${PROJECT_VERSION_MAJOR}` (`src/CMakeLists.txt:137-138`): the SONAME is `libsushiruntime.so.0`.
- CMake floors: root and examples 3.20, `tests/CMakeLists.txt:7` 3.22, presets schema version 3 (needs 3.21), `Determinism.cmake:52` takes a better path from 3.30.

#### Options and cache variables

| Name | Default | Defined at | Documented in |
| --- | --- | --- | --- |
| `SR_SYCL_TOOLCHAIN` | intel-llvm | `cmake/Sycl.cmake:39` | ARCHITECTURE §14, README, CLI_GUIDE, FAQ, GLOSSARY |
| `SR_GPU_BACKEND` | auto | `cmake/gpu/DetectGpu.cmake:39` | ARCHITECTURE §14 |
| `SR_ENABLE_CUDA` (legacy) | OFF | `cmake/gpu/DetectGpu.cmake:75` | ARCHITECTURE §14 |
| `SR_CUDA_ARCH` | none, probed | `cmake/gpu/Cuda.cmake:34`, published FORCE by `GpuBackend.cmake:210` | ARCHITECTURE §14 |
| `SR_AMD_ARCH` | none, probed | `cmake/gpu/Rocm.cmake:22` | ARCHITECTURE §14, README |
| `SR_INTEL_GPU_DEVICE` | "" | `cmake/gpu/GpuBackend.cmake:51` | **nowhere** |
| `SR_ENABLE_DISTRIBUTED` | OFF | `cmake/SushiRuntimeProjectOptions.cmake:35` | ARCHITECTURE §14, README, CONTRIBUTING, DISTRIBUTED_GUIDE |
| `SR_BUILD_TESTS` | OFF | `cmake/SushiRuntimeProjectOptions.cmake:41` | ARCHITECTURE §14, README, CLI_GUIDE, FAQ |
| `SR_BUILD_BENCHMARKS` | ON | `tests/CMakeLists.txt:27` | ARCHITECTURE §14 |
| `SR_DETERMINISTIC_FP` | ON | `cmake/Determinism.cmake:26` | ARCHITECTURE §14, INTEGRATION, FAQ |
| `SR_NATIVE_ARCH` | OFF | `cmake/Optimization.cmake:23` | ARCHITECTURE §14 |
| `SR_GCC_TOOLCHAIN` | /usr (non-Windows) | `cmake/Sycl.cmake:166` | **nowhere** |
| `SR_INSTALL_CMAKEDIR` | `${CMAKE_INSTALL_LIBDIR}/cmake/SushiRuntime` | `CMakeLists.txt:60` | **nowhere** |
| `SUSHIRUNTIME_REDIST_COMPONENT`, `_REQUIRED_RUNTIME_LIBRARIES`, `_RUNTIME_LIBRARY_PATTERNS`, `_OPTIONAL_RUNTIME_LIBRARIES` | see file | `cmake/SushiRuntimeInstall.cmake:23,35,45,56` | **nowhere** |
| `VCPKG_ROOT` | env, then `$SUSHISTACK_HOME/dependencies/vcpkg`, then `C:/vcpkg` | `cmake/SushiRuntimeVcpkg.cmake:23-30` | README |

Not an option but behaves like one: `cmake/Sanitizers.cmake:23-27` turns ThreadSanitizer on for `CMAKE_BUILD_TYPE` Debug or RelWithDebInfo on every non-Windows build. One line of `ARCHITECTURE.md:1126` mentions it; no guide does.

Interactions: `SR_ENABLE_DISTRIBUTED` gates sources (`src/CMakeLists.txt:24`), a PUBLIC define (`:112`), three tests and one benchmark (`tests/CMakeLists.txt:163,246`) and one example. `SR_BUILD_BENCHMARKS` only matters under `SR_BUILD_TESTS`. `SR_GPU_BACKEND` renders differently per `SR_SYCL_TOOLCHAIN` (`GpuBackend.cmake:227-247`). `SR_ENABLE_CUDA` only applies when `SR_GPU_BACKEND=auto`. TSan (build type) and ASan (CLI-injected `CMAKE_CXX_FLAGS`) exclude each other.

#### Presets

`CMakePresets.json` has three configure presets (`intel-llvm`, `adaptivecpp`, `oneapi`), all Release, all `binaryDir: ${sourceDir}/build`, all `SR_GPU_BACKEND=auto`. None sets `SR_BUILD_TESTS`, `SR_ENABLE_DISTRIBUTED` or a sanitizer. There are no build presets and no test presets. The CLI does not use them: `grep -rnE "preset|CMakePresets" cli/sushiruntime` returns nothing; `sr` builds its own `-D` line (`-DSR_BUILD_TESTS`, `-DSR_ENABLE_DISTRIBUTED`, `-DSR_GPU_BACKEND`, `-DSR_SYCL_TOOLCHAIN`, `-DSR_BUILD_BENCHMARKS`, `-DCMAKE_CXX_FLAGS=-fsycl...`, vcpkg paths). `CMakePresets.json:41-43` stores a comment as an environment variable named `_comment`. README.md:134-135 documents the three presets.

#### The consumer's view

- `find_package(SushiRuntime)` yields two targets: `SushiRuntime::SushiRuntime` (`EXPORT_NAME`, `src/CMakeLists.txt:153`) and `SushiRuntime::sushiruntime_deterministic_fp`. Variables: `SushiRuntime_DISTRIBUTED`, `SushiRuntime_DETERMINISTIC_FP` (`cmake/SushiRuntimeConfig.cmake.in:50,54`). Compatibility `SameMinorVersion` (`CMakeLists.txt:100`).
- INTERFACE leakage, by design: include dir; `SR_DETERMINISTIC_FP=1` plus `/fp:precise` or `-ffp-contract=off -fno-fast-math`; `SR_DISTRIBUTED` when enabled; a link to `IntelSYCL::SYCL_CXX`. Warnings, `-march`, TSan and `-ftls-model` stay out (BUILD_INTERFACE wrappers, `cmake/BuildPolicy.cmake:56-62`).
- Transitive dependencies: hwloc is dropped from the install interface (`src/CMakeLists.txt:70-72`), correct for a shared library. Threads is not named anywhere. SYCL: the config tries `find_package(IntelSYCL QUIET)` and otherwise creates an empty `IntelSYCL::SYCL_CXX` (`SushiRuntimeConfig.cmake.in:36-41`). On intel-llvm, `-fsycl` is a directory-scope `add_compile_options` (`cmake/Sycl.cmake:140-141`) and the GPU triples are too (`GpuBackend.cmake:246-247`), so neither is on any target.
- `tests/package` proves: headers found, one kernel compiles and runs in a consumer TU, version check, forbidden flags absent from the interface, and (via `tools/packaging/verify_installed_package.py`) that the program starts with only the prefix on `PATH`. It does not prove the SYCL part of the contract, because the oracle passes `-DCMAKE_CXX_FLAGS=-fsycl` itself (`verify_installed_package.py:112`). It is registered only when `SR_BUILD_TESTS` is on and Python is found; CI does not run label `package`. No test covers `add_subdirectory` or FetchContent, the distributed variant, adaptivecpp or oneapi as consumer, a GPU-offload package, or the CPack archive.

#### Tests

- Three executables: `sr_functional_tests` (59 files, +3 distributed), `sr_allocation_tests` (1), `sr_benchmark_tests` (13, +1). Every source is an explicit line in `tests/CMakeLists.txt:92-159, 227-244`; all 85 `.cpp` under `tests/` are registered (checked by script; `package/main.cpp` is the separate consumer).
- CTest labels come from the GTest suite prefix through `TEST_FILTER` (`tests/CMakeLists.txt:185-199`), not from the folder. Counts today: 142 `Unit_` in unit/, 224 `Integration_` in integration/, 3 `Regression_` in regression/, 13 `Performance_`, 6 `Stress_`. No test lacks a prefix and none sits in the wrong folder.
- Adding a test: one file, one line in `tests/CMakeLists.txt`, and by the README rule a section in the folder README.

### Prior-audit status table

| Finding | Status | Evidence |
| --- | --- | --- |
| L1 two live changelogs | closed | `docs/CHANGELOG.md` gone; `docs/reference/CHANGELOG.md` is the one file; old sections in `docs/archive/changelog/` |
| L2 CLI 1.0.0 vs product 0.3.0 | open | `cli/pyproject.toml:7`, `cli/sushiruntime/__init__.py:8`, `Doxyfile:10` unchanged |
| L3 no checkers | closed, with a hole | four checkers under `tools/`; `check_layering.py` has an empty tier order (LAYOUT-01) |
| L4 CI shape | changed | checkers job with an OS matrix (`ci.yml:163-176`), `release.yml` on `v*` tags; build jobs still `ubuntu-latest`, raw `cmake`/`ctest` (`ci.yml:28-36,56-71,146-155`), `pyflakes ... || true` still at `:92` |
| L5 no root README/AGENTS/CLAUDE | closed | all three tracked in root |
| L6 CI installs sushicore from a dead path | closed | `ci.yml:87` `pip install -e ./cli` |
| L7 release due | open, larger | 71 commits since v0.3.0, 58 Unreleased entries (lines 4-61), no new tag |
| L8 docs tree | closed | `getting_started/ architecture/ guides/ design/ reference/ archive/ agent/` exist; I did not run `check_docs_layout.py` |
| L9 `docs/slop` | closed | moved to `docs/archive/slop/` and `docs/design/` |
| L10 two licence files | closed | `docs/LICENSE` gone; licence is now PolyForm Noncommercial (9eb845f) |
| L11 `Doxyfile` in root | open | still in root; `Dockerfile` too |
| L12 `src/`, `examples/`, `tests/functional/`, `SushiRuntime.h` | open | unchanged; on the backlog (`REMAINING_WORK.md:130-133`) |
| L13 sushicore 0.7.0 without changelog line | open | `grep 0.7.0 docs/reference/CHANGELOG.md` finds nothing; backlog `:111-113` |
| L14 CONTRIBUTING forbids changelog entries | closed | `docs/CONTRIBUTING.md:230-231` |
| L15 logic in root `CMakeLists.txt` | open | `CMakeLists.txt:69-80, 91-121, 127-138`; backlog `:85-86` |
| L16 `CXX-DetectStdlib-*.h` in root | files gone, cause open | `cli/sushiruntime/services/project.py:441` still `cwd=root`; ignore rule still at `.gitignore:46` |
| L17 leftovers, missing ignore rules | open | `.scratch_syntax/`, `.superpowers/`, `build-asan/`, `venv/` on disk; no rules for the first two, `.pytest_cache/`, `.venv/` |
| L18 merged local branch | open, +1 | `feature/sushicore_help_screen` still there; `ci-fix` (merged in 2cf77b0) now also local and remote; two stashes remain |
| L19 no `.clang-format` / `.editorconfig` | open | neither exists |
| L20 duplicated and stale ignore rules | partly closed | `docs/api-site/.gitignore` removed; `.gitignore:11` `dependencies/`, `:12` `docs/html`, `.dockerignore:8` `.gemini/` remain |

### Findings

Ranked by gain in usability or operability. "Breaks consumer" means an include path, target name or option name changes.

**LAYOUT-01, high, layout.** The layering the architecture page states is not what the includes do, and the checker that should hold it checks nothing. Evidence: the upward-edge table above; `tools/layering/check_layering.py:23` `K_TIER_ORDER ... = ()`; backlog `REMAINING_WORK.md:114-117` ("checks nothing here"). Change: declare the order once in a table under `cmake/` (core; topology, memory; scheduler; graph; execution; interop; api; distributed), teach the checker a single-module mode that ranks `include/SushiRuntime/<area>/` and `src/<area>/`, and move the headers that cause the cycles to the tier their dependencies give them. Concrete candidates: `execution/runtime_settings.hpp` (PinPolicy, RuntimeSettings, WorkerPoolInfo; the scheduler needs it) down to `scheduler/` or `core/`; `memory/usm_allocation_owner.hpp` (frees through RuntimeContext) up to `execution/`; `scheduler/event_polling.hpp` needs `graph/node.hpp`, so either scheduler sits above graph in the table or the readiness seam (`ready_sink.hpp`) carries it; `interop/host_export.hpp` takes `api/data/buffer.hpp`, so the Buffer-typed overloads belong in `api/` and `interop/` keeps the vocabulary. The contents are another auditor's call; the placement rule is mine. Breaks consumer: yes for each moved header (keep a forwarding header for one MINOR). Effort: M for the table and checker, L for the moves.

**LAYOUT-02, high, packaging.** An installed package does not carry the SYCL flags it was built with, and nothing tells the consumer which ones those were. Evidence: `cmake/Sycl.cmake:140-141` `add_compile_options(-fsycl)`; `cmake/gpu/GpuBackend.cmake:246` `add_compile_options(-fsycl-targets=${_sr_targets} ...)`; `SushiRuntimeConfig.cmake.in:39-41` creates an empty `IntelSYCL::SYCL_CXX`; the oracle adds the flag by hand, `verify_installed_package.py:112`. A consumer of a CUDA-built package must rediscover `-fsycl-targets=spir64,nvptx64-nvidia-cuda --cuda-path=...` alone; `grep fsycl-targets docs/guides README.md` finds nothing. Change: export `SushiRuntime_SYCL_TOOLCHAIN`, `SushiRuntime_GPU_BACKEND` and `SushiRuntime_SYCL_FLAGS` from the config, and add an opt-in target `SushiRuntime::sycl` whose INTERFACE options are those flags (toolkit paths excluded), so the default target keeps its narrow contract. Then drop `-DCMAKE_CXX_FLAGS=-fsycl` from the oracle so the test proves it. Breaks consumer: no, additive. Effort: M.

**LAYOUT-03, high, build.** The tree is not safe to embed, although `INTEGRATION.md:87-92` says `add_subdirectory` "also works" and SushiEngine consumes it that way. Evidence: no `PROJECT_IS_TOP_LEVEL` guard anywhere; `CMakeLists.txt:87` `add_subdirectory(examples)` builds six executables unconditionally (no `SR_BUILD_EXAMPLES`); `:138` `include(CPack)` and `:127-131` set the parent's `CPACK_*`; install rules and `sushiruntime_install_runtime_dependencies()` (`:121`) always register; `tests/CMakeLists.txt:285` installs `${CMAKE_BINARY_DIR}`, the parent's tree; `Sycl.cmake:84-98` and `SushiRuntimeVcpkg.cmake` run before a `project()` that the parent already executed, so their compiler and toolchain-file selection has no effect there; `-fsycl` set at the runtime's directory scope never reaches the parent's targets. Change: add `SR_BUILD_EXAMPLES` and `SR_INSTALL`, default `${PROJECT_IS_TOP_LEVEL}` (needs CMake 3.21, which the presets already require); guard CPack with the same; use `PROJECT_BINARY_DIR` in `tests/CMakeLists.txt:282,285`; add a second tiny consumer, `tests/embed/`, that does `add_subdirectory` and is run by the oracle. Breaks consumer: embedding parents stop getting examples and install rules by default, which is the point. Effort: M.

**LAYOUT-04, high, build.** Options are declared in seven files and three of them are documented nowhere. Evidence: the options table above; `cmake/SushiRuntimeProjectOptions.cmake:9` calls itself "the one place to see every build option" and holds two of them. Undocumented: `SR_GCC_TOOLCHAIN` (`Sycl.cmake:166`, silently adds `--gcc-toolchain=/usr` to every Linux build), `SR_INTEL_GPU_DEVICE` (`GpuBackend.cmake:51`), `SR_INSTALL_CMAKEDIR`, four `SUSHIRUNTIME_*` cache lists. Change: declare every user-facing option in one file, `cmake/SushiRuntimeOptions.cmake` (the modules keep the logic and read the value), and make the table in `ARCHITECTURE.md:1104-1115` the one reference, moved to `docs/reference/BUILD_OPTIONS.md` with a row per option; the four packaging lists become plain variables, since nobody sets them from a command line. Breaks consumer: no. Effort: S.

**LAYOUT-05, high, build.** ThreadSanitizer is switched by the build type, with no option to say no. Evidence: `cmake/Sanitizers.cmake:23-27` `if(CMAKE_BUILD_TYPE MATCHES "Debug|RelWithDebInfo") ... -fsanitize=thread`; ASan arrives by another route, `-DCMAKE_CXX_FLAGS={ASAN_FLAGS}` from the CLI; `ci.yml:52` relies on the build-type trick. A Linux user who configures Debug to step through a test gets a TSan binary; a multi-config generator gets no sanitizer. Change: one option `SR_SANITIZER` with values `none|thread|address|undefined`, default `none`, rendered in `Sanitizers.cmake`; the CLI's `--type asan` and the TSan CI lane pass it. Breaks consumer: no; changes what Debug means for contributors, needs a changelog line. Effort: S.

**LAYOUT-06, high, build.** Presets and the CLI are two unrelated descriptions of the same configurations. Evidence: `grep -rnE "preset|CMakePresets" cli/sushiruntime` empty; presets lack tests, distributed, sanitizers, build and test presets; all three share `binaryDir` `build`, so switching preset reuses a cache of another toolchain; `CMakePresets.json:41-43` comment-as-environment. A user without the CLI can configure a Release library and nothing else from the presets; README.md:134 presents that as the supported path. Change: make the presets the single statement (`dev`, `dev-distributed`, `tsan`, `asan`, `ci-fast`, `ci-nightly`, each inheriting a hidden toolchain preset, with matching `buildPresets` and `testPresets` carrying the label regexes) and have `sr build` select a preset and add only machine paths; or, the cheaper direction, delete the presets and say so. The choice is the owner's. Breaks consumer: preset names. Effort: M.

**LAYOUT-07, medium, packaging.** The SONAME says two incompatible releases are the same ABI. Evidence: `src/CMakeLists.txt:138` `SOVERSION ${PROJECT_VERSION_MAJOR}` gives `.so.0`, while `CMakeLists.txt:100` states `SameMinorVersion` because "a consumer asking for 0.2 must not be handed 0.3". Change: `SOVERSION "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}"` until v1.0.0. Breaks consumer: the file name of the installed library on Linux. Effort: S.

**LAYOUT-08, medium, packaging.** Two export mechanisms run at once and neither is the rule. Evidence: `src/CMakeLists.txt:142` `WINDOWS_EXPORT_ALL_SYMBOLS ON`; `core/export.hpp:30-37` with 41 `SUSHIRUNTIME_API` uses; no hidden visibility preset. With export-all on, the macro changes nothing for functions on Windows, and on Linux everything exports regardless, so an unannotated internal symbol is public ABI on both. The macro name also differs from the `<PREFIX>_API` form of `platform-portability` (`SR_API`). Change: set `CXX_VISIBILITY_PRESET hidden` and `VISIBILITY_INLINES_HIDDEN ON`, drop `WINDOWS_EXPORT_ALL_SYMBOLS`, and let the link errors list what still needs the macro. Breaks consumer: only one that used an unannotated symbol. Effort: M (the annotation pass is the cost; I did not count missing annotations).

**LAYOUT-09, medium, packaging.** The library type is fixed and the install rules assume it. Evidence: `src/CMakeLists.txt:29` `SHARED`; the hwloc link is wrapped in `BUILD_INTERFACE` (`:70-72`) with a comment that this is only right for a shared library; `Optimization.cmake:38` `-ftls-model=initial-exec` forbids `dlopen`. Change: either state "shared only" in `INTEGRATION.md` §4 and fail configure when `BUILD_SHARED_LIBS=OFF` is passed, or support static by exporting hwloc through `find_dependency`. Owner's choice; the first is S, the second M. Breaks consumer: no.

**LAYOUT-10, medium, layout.** `include/SushiRuntime/core` is a catch-all. Of 18 headers, 10 are foundations (`bit`, `span`, `export`, `common`, `delegate`, `ref_counted`, `sushi_ptr`, `strong_id`, `result`, `error`), one is a logging subsystem of 360 lines (`logger.hpp`), and six belong to higher layers' vocabulary: `run_report.hpp`, `profiling_ledger.hpp` ("per-plan store of node event records"), `event_timestamps.hpp` (reads a SYCL event), `dispatch_width.hpp`, `resource_region.hpp`, `resource_liveness.hpp`, plus `sycl_compat.hpp`. Change: keep `core/` for the ten; `core/logger.hpp` to `logging/logger.hpp`; `run_report`, `profiling_ledger`, `event_timestamps`, `dispatch_width` to a new `observability/` area; `resource_region`, `resource_liveness` to `memory/`; `sycl_compat` to a `sycl/` area or `execution/`. Breaks consumer: include paths (forwarding headers for one MINOR; the umbrella hides it for most). Effort: M.

**LAYOUT-11, medium, layout.** `include/SushiRuntime/api` is already grouped into six subfolders with a README each, so it is not a catch-all, but two things are off. `api/detail/` holds numeric kernels and builders that are not "detail of api" in one sense: `fixed_order_reduce.hpp`, `fixed_order_scan.hpp` (algorithms), `device_fault.hpp`, `run_report_builder.hpp`, `step_machinery.hpp`, `callable_traits.hpp`; and `api/graph/graph.hpp` is 3052 lines, 31% of the two folders' 9883. Change: split `api/detail/` into `api/detail/algorithms/` and `api/detail/run/`; the split of `graph.hpp` is the API auditor's finding, I only record that one file holds a third of the public surface. Installed `detail/` headers are public by path; the README says so or they move under `src/` if nothing outside needs them (they are templates, so they cannot). Breaks consumer: no if only `detail/` moves. Effort: S.

**LAYOUT-12, medium, layout.** Folder and include path disagree for three things a user looks for. (a) `include/SushiRuntime/version.hpp` pairs with `src/core/version.cpp`; move the header to `core/version.hpp` or the source to `src/version.cpp`. (b) The umbrella is `SushiRuntime.h`, the only `.h` and the only PascalCase file, and the only header using quoted relative includes (`:31-64`). (c) The umbrella omits `interop/` and four of eight scheduler headers, so "include the umbrella" does not mean "the public API". Change: add `include/SushiRuntime/sushiruntime.hpp` as the umbrella with angle includes and every public area, and keep `SushiRuntime.h` as a one-line forwarder marked deprecated. Breaks consumer: no while the forwarder stays. Effort: S.

**LAYOUT-13, medium, layout.** Source file names do not follow their headers, so a reader cannot go from one to the other by name. Evidence: `src/scheduler/scheduler.cpp` for `scheduler/task_scheduler.hpp`; `src/distributed/offload_policy.cpp` for `i_offload_policy.hpp`; eight scheduler headers against one source. Change: rename to `task_scheduler.cpp` and `i_offload_policy.cpp` (or split the default policy into `default_offload_policy.{hpp,cpp}`). Breaks consumer: no. Effort: S.

**LAYOUT-14, medium, build.** `src/CMakeLists.txt:117-122` lists `memory/usm_allocator.cpp` among the SYCL sources; that file does not exist (`src/memory/` is an empty untracked folder, `src/README.md` says "`memory/` holds no source file"). The same hand list omits nothing else today, but it is a second registry beside the glob: a new SYCL-using source must be added there or the oneAPI lane compiles it without SYCL flags. Change: delete the stale entry and pass `${SRC_FILES}` to `add_sycl_to_target`, or keep one explicit source list for both. Also remove the empty `src/memory/` from disk. Breaks consumer: no. Effort: S. Not verified: what `IntelSYCL`'s `add_sycl_to_target` does with a missing path.

**LAYOUT-15, medium, build.** The library globs its sources and the tests list theirs, and the comment in the glob disowns it. Evidence: `src/CMakeLists.txt:10-15` "explicit listing is preferred for production stability. For now, GLOB is used"; `tests/CMakeLists.txt:92-159` explicit. Siblings shaped differently. Change: pick one. Explicit lists per area (`src/<area>/sources.cmake`, one line per file) match the tests and make the distributed subtraction (`:19-26`) unnecessary. Breaks consumer: no. Effort: S.

**LAYOUT-16, medium, build.** A test's label depends on a naming convention nothing enforces. Evidence: `tests/CMakeLists.txt:185-199` discovers by `TEST_FILTER "Unit_*"` and so on; a suite named without one of the five prefixes compiles, links and is never run by `ctest`. Today all 388 tests conform. Also lines 111-114 list four `integration/` files in the middle of the unit block. Change: one executable per folder built by one function, `sushiruntime_add_test_program(NAME LABEL SOURCES...)`, labelled by folder; the prefix becomes redundant. That also gives `repository-layout`'s "one program per kind". Breaks consumer: no. Effort: M.

**LAYOUT-17, medium, layout.** Test file names do not predict the thing tested in the unit folder. Evidence: `unit/test_fixed_order_reduce.cpp` and `unit/test_late_count.cpp` include only the umbrella; `unit/test_numa_steal.cpp`, `test_nvidia_topology.cpp`, `test_topology.cpp`, `test_topology_abstraction.cpp` include `execution/`; commit 2728826 put 70 lines testing `RuntimeContext` aligned allocation into `unit/test_topology.cpp`; `execution/` has no unit test file by name; `regression/test_regression.cpp` is one file with one suite (`Regression_Sentinel`). Change: `tests/unit/<area>/test_<header>.cpp` mirroring `include/SushiRuntime/<area>/<header>.hpp`, the allocation tests to `tests/unit/execution/test_runtime_context.cpp`, regression files named by defect. Combine with the flattening of `tests/functional/` (prior L12). Breaks consumer: no. Effort: M.

**LAYOUT-18, medium, build.** Adding a GPU vendor touches four places in one file plus two new files, and the root comment says one. Evidence: `CMakeLists.txt:52-53` "a new file under cmake/gpu/ + one branch in DetectGpu"; actual: `cmake/gpu/<Vendor>.cmake`, `cmake/gpu/locate/<Vendor>Toolkit.cmake`, then in `DetectGpu.cmake` the STRINGS list (`:41`), `SR_GPU_VENDORS` (`:46`), the auto-detect block (`:87-131`) and the if/elseif chain (`:175-179`), which uses `IntelGpu.cmake` where the locator loop derives `Intel` + `Toolkit` by rule. Change: rename `IntelGpu.cmake` to `Intel.cmake` and replace the chain with the same derived include the locators use (`${Vendor}.cmake`), so `SR_GPU_VENDORS` is the one registry. Then: two files, one list entry, one row in the options reference and in `cli/sushistack.deps.toml`. Breaks consumer: no. Effort: S.

**LAYOUT-19, medium, build.** The root `CMakeLists.txt` still carries logic (prior L15, on the backlog). Evidence: version check `:69-80`, package files `:91-116`, CPack `:127-138`. Change: `cmake/SushiRuntimeVersionCheck.cmake`, package-file generation into `cmake/SushiRuntimeInstall.cmake` together with the `install(TARGETS/DIRECTORY/EXPORT)` block now at `src/CMakeLists.txt:167-190`, and `cmake/SushiRuntimePackaging.cmake`. Install rules then live in one file instead of three. Breaks consumer: no. Effort: S.

**LAYOUT-20, medium, packaging.** `install(DIRECTORY include/)` copies everything under `include/`. Evidence: `src/CMakeLists.txt:175-177` has no `FILES_MATCHING`; the tree holds 16 README.md files, and the nine `distributed/` headers install even when the layer is off. Change: `FILES_MATCHING PATTERN "*.hpp" PATTERN "*.h"`, and `PATTERN "distributed" EXCLUDE` when `SR_ENABLE_DISTRIBUTED` is off. Breaks consumer: one that includes a distributed header against a non-distributed package now fails at the include instead of at link. Effort: S.

**LAYOUT-21, low, layout.** READMEs missing where the tree's own convention has them: `include/SushiRuntime/interop/`, `cmake/`, `cmake/gpu/`, `tests/` (root), `tests/benchmark/performance/`, `tests/benchmark/stress/`. `src/memory/` needs none once the empty folder is removed. `cmake/README.md` is the natural home of the include order (pre-project, post-project) that `CMakeLists.txt:11-27` now explains in a 17-line comment. Effort: S.

**LAYOUT-22, low, build.** `cmake/` file names follow two rules. Evidence: `SushiRuntimeVcpkg.cmake`, `SushiRuntimeProjectOptions.cmake`, `SushiRuntimeInstall.cmake` are prefixed; `Sycl.cmake`, `Dependencies.cmake`, `BuildPolicy.cmake`, `CompilerWarnings.cmake`, `Determinism.cmake`, `Optimization.cmake`, `Sanitizers.cmake` are not; `CMakeLists.txt:19-27` explains the prefix came from a name collision with the engine and that absolute-path includes are the real fix. The header comments of the first two still call themselves `ProjectOptions.cmake` and `Vcpkg.cmake` (`SushiRuntimeProjectOptions.cmake:7`, `SushiRuntimeVcpkg.cmake:7`). Since `CMAKE_MODULE_PATH` is still appended (`CMakeLists.txt:30`), a bare `Dependencies.cmake` can still shadow a parent's. Change: prefix all, or stop appending to `CMAKE_MODULE_PATH` (nothing in the tree uses module-mode `find_package` on a file of ours). Effort: S.

**LAYOUT-23, low, build.** Windows triplet hardcoded in one place and derived in another. Evidence: `cmake/Dependencies.cmake:18` `installed/x64-windows/tools/pkgconf/pkgconf.exe`; `cmake/SushiRuntimeInstall.cmake:79-81` uses `VCPKG_TARGET_TRIPLET` with an x64 fallback. Change: one helper that resolves the vcpkg installed root and triplet, used by both. Effort: S.

**LAYOUT-24, low, build.** Windows DLL staging is written twice. Evidence: `tests/CMakeLists.txt:52-67` and `examples/CMakeLists.txt:23-40` are the same 15 lines with different names. Change: one function in `cmake/`, `sushiruntime_stage_runtime_dll(<group>)`. Effort: S.

**LAYOUT-25, low, layout.** Root files the layout sends elsewhere: `Doxyfile` and `Dockerfile` (to `.config/doxygen/`, `.config/docker/`), read by `cli/sushiruntime/services/project.py` and `ci.yml`. Prior L11; still open. Also prior L12 names (`src/`, `examples/`, `tests/functional/`). Owner's decision, already on the backlog. Effort: S for the two files, M for the renames.

**LAYOUT-26, low, layout.** Ignore and attribute files. `.gitattributes` uses `* text=auto` without `eol=lf` and has no rule for `.cmd`; `platform-portability` asks for LF pinned. `.gitignore:11-12` (`dependencies/`, `docs/html`) are stale; no rule for `.superpowers/`, `.scratch_syntax/`, `.pytest_cache/`, `.venv/`; `:46` hides the probe files whose cause (`project.py:441`, `cwd=root`) is still there. `.dockerignore:8` lists `.gemini/` and omits `venv/`, `build-asan/`, `build-tsan/`, `docs/`. Effort: S.

**LAYOUT-27, low, packaging.** Manifests. `sushi-module.toml` carries no version and writes the repository URL in lower case; `cli/sushistack.deps.toml` pins no versions (hwloc, the intel/llvm nightly), against the "exact version" rule of `dependencies`, and marks all three SYCL toolchains `required = true` while `provides` makes them alternatives. Whether `hub` accepts a version field I did not check. Effort: S once the format question is answered.

**LAYOUT-28, low, build.** CMake floor stated three ways: 3.20 root, 3.22 tests, 3.21 implied by presets. Change: one floor, 3.22, in the root. Effort: S.

#### Cost of adding the next thing (today)

| Thing | Files touched | Which |
| --- | --- | --- |
| A source file in an existing module | 1, or 2 if it includes SYCL | the file; `src/CMakeLists.txt:117` list |
| A module (area) | 6 or more | `include/.../<area>/` + README, `src/<area>/` + README, `src/README.md` table, `SushiRuntime.h`, `ARCHITECTURE.md` §2 and §16; a checker tier entry once LAYOUT-01 lands |
| A GPU backend | 2 new + 4 edits in `DetectGpu.cmake`, then `deps.toml`, options table | LAYOUT-18 |
| A test | 2, plus a README section | the file, `tests/CMakeLists.txt` |
| An example | 3 | the file, `examples/CMakeLists.txt:59-64`, `examples/README.md` |
| A CMake option | 2 to 4 | the module, `ARCHITECTURE.md` §14, CLI `-D` line, presets |

### Not checked

- No configure, build or test ran. Every statement about what CMake does with a file is from reading it. In particular: the behaviour of `add_sycl_to_target` with the missing `memory/usm_allocator.cpp` (LAYOUT-14), and whether an embedded build fails or only misbehaves (LAYOUT-03).
- `cmake/gpu/Cuda.cmake`, `Rocm.cmake`, `GpuBackend.cmake` and the three locators were read by structure (options, includes, dispatch), not line by line; `DetectGpu.cmake` lines 76-131 only by grep.
- `Dockerfile`, `Doxyfile` beyond five lines, `.github/` templates and the composite action were not read. `ci.yml` and `release.yml` were read by grep for the prior-audit rows only.
- `docs/CONTRIBUTING.md` and `docs/architecture/ARCHITECTURE.md` were read in the sections that bear on layout (§2, §14, §16; CONTRIBUTING's changelog rule); `REMAINING_WORK.md` "Structural debt", "Documentation" and "Repository hygiene" in full; `INTEGRATION.md` §3-§4. The rest of the manual was not read.
- `check_docs_layout.py`, `check_changelog.py` and `check_source_comments.py` were not run; the L8 row rests on the file listing.
- The CLI's Python was only grepped for presets, `-D` flags and `cwd=`.
- The contents of the C++ headers: I counted lines, read `@brief` lines and include lines, nothing more. The regrouping in LAYOUT-10 and LAYOUT-11 follows those briefs and should be confirmed by the module auditors.
- Whether SushiEngine's `add_subdirectory` use works today, and what `hub` reads from the two manifests.
- Git history for removed large blobs; remote state of tags and CI runs.

## Command line, CI, tools (OPS)

Read-only. Nothing was run except `git log`, `git ls-tree`, `git check-ignore`, `grep`/`cat`, and one `pip index versions sushicore` query. No `sr`, cmake, ninja, ctest or pytest was started. No file was written.

### Facts

**Shared layer.** `sr` does not import `sushicli`. Every shared piece comes from `sushicore` (D:/Projects/sushicore, 0.8.0 at HEAD; PyPI lists 0.8.0, 0.7.0, 0.4.0 ...). No `sushicli` import exists anywhere under `cli/`. `cli/pyproject.toml:15` requires `sushicore>=0.7.0`.

**Command tree** (all in `cli/sushiruntime/cli.py` unless noted):

| Command | Options and defaults | Line |
| --- | --- | --- |
| root | `--version`, `--describe` (from `sushicore.root_options`) | 52 |
| `toolchain [NAME]` | NAME free string, default None (list) | 78-86 |
| `build` | `--type/-t` release (release, debug, relwithdebinfo, asan); `--distributed` off; `--no-cuda` off; `--clean` off; `--force` off; `--no-test` off | 97-117 |
| `test` | `--suite/-s` functional (unit, regression, integration, functional, stress, performance, benchmark, package, all); `--distributed` off; `--filter/-f` None; `--asan` off; `--repeat/-r` 0 | 128-149 |
| `run [TARGET]` | `--sort` off; `--distributed ROLE` None (master, worker); `--asan` off; args after `--` | 160-177 |
| `package` | `--prefix` None (build/dist); `--no-archive` off; `--force` off | 187-205 |
| `clean` | none | 213-215 |
| `docs`, `docs bundle --release X.Y.Z [--out]` | from `sushicore.docs_bundle` | 228-236 |
| `container build` | `--no-cache` off; `--oneapi` off | 244-254 |
| `container run` | `--admin` off; `--no-gpu` off | 263-272 |
| `config` | none (from `sushicore.diag_commands`) | 294-295 |
| `env` | `--all/-a` off; `--asan` off | 305-312 |
| `setup` | `--dry-run`, `--yes`, `--toolchain` (repeatable), `--no-gpu` (sushicore `provision/commands.py:266-282`) | 315-322 |
| `doctor` | `--for GROUP` (build, test, infer, eval) | same |
| `link`, `unlink` | `--workspace` | same |
| hidden aliases | `sr doxygen`, `sr docker build`, `sr docker run`, removed after 2027-03-25 (`aliases.py:14-20`) | 284-291 |

Panels: Toolchain, Project, Diagnostics, Container. Console scripts `sr` and `sushiruntime` both bind `sushiruntime.cli:main` (`pyproject.toml:19-21`).

**Environment (`env.py`).** Reads: `os.environ` whole, `cfg.vs_vcvars` (Windows), `cfg.oneapi_root` (Linux, oneapi only), `cfg.llvm_root`/probe, `cfg.acpp_exe`, `cfg.vcpkg_root`/`vcpkg_triplet`. Writes into the child environment: `PATH` (toolchain bin, compiler-rt `lib/clang/*/lib/windows`, vcpkg `installed/<triplet>/bin`), `LD_LIBRARY_PATH` (Linux, toolchain lib), `LSAN_OPTIONS` (if unset and `tests/lsan_suppressions.txt` exists). `services/project.py` then pops `CUDA_LAUNCH_BLOCKING` for test and run (479, 499), restores the shell's `LD_LIBRARY_PATH` for oneapi (126-143), and for `run --asan` sets defaults for `ONEAPI_DEVICE_SELECTOR=opencl:cpu` and `ASAN_OPTIONS` (502-505). sushicore strips six device-selection variables from any cached snapshot (`build_env.py:56-73`). The snapshot is cached in `<build>/.sushiruntime_env.json`, keyed on platform, vs_vcvars, oneapi_root, toolchain, resolved llvm_root, acpp_exe (`env.py:33-47`).

**Precedence.** `config.toml` -> workspace `workspace.toml` (when linked) -> `config.local.toml` -> `SR_*` environment (`sushicore/module_config.py:715-734`). No command flag overrides a config key. Environment names: `SR_SYCL_TOOLCHAIN`, `SR_CC`, `SR_ONEAPI_ROOT`, `SR_ICX`, `SR_LLVM_ROOT`, `SR_ACPP` (`config.py:31-38`) plus `SR_CXX`, `SR_CMAKE_GENERATOR`, `SR_VCPKG_ROOT`, `SR_VCVARS`, `SR_NINJA`, `SR_CMAKE`, `SR_CTEST`, `SR_RC`, `SR_PKGCONF`, `SR_DOXYGEN`, `SR_VCPKG_TRIPLET`, `SR_TARGET_BIN` (`sushicore/profile.py:536-549`).

**Toolchain choice.** `cfg.toolchain` (default `intel-llvm`, `config.py:97`), set by `sr toolchain` into `config.local.toml` or by `SR_SYCL_TOOLCHAIN`. Linux: compiler pair from `TOOLCHAIN_COMPILERS`, looked up on the snapshot `PATH` (`project.py:203-259`). Windows: `resolved_windows_compiler()` gives a full path or a bare name (`config.py:137-151`). Two SYCL stacks: setvars is sourced only for `oneapi` (`env.py:92-95`); at run time the oneapi lane throws away the setvars `LD_LIBRARY_PATH` (`project.py:126-143`). Nothing detects or reports a second stack already present in the parent shell.

**Config keys.** `[tool]`/`[tool.<platform>]`: cxx, generator ("Ninja"), use_vcpkg (False), vcpkg_root, vs_vcvars, ninja_exe, cmake_exe, ctest_exe, rc_exe, pkgconf_exe, doxygen_exe, vcpkg_triplet ("x64-windows"), target_bin, oneapi_root, icx_compiler, llvm_root, acpp_exe, toolchain ("intel-llvm"), cc. `[cli]`: theme, icons, color, background. Unknown keys are dropped silently (`sushicore/config_base.py:318-319`). Malformed TOML is one line and exit 1 (pinned by `cli/tests/test_cli.py:62`).

**Exit codes.** 0 success; 1 service failure or `SushiCoreError` (`sushicore/entry.py:22`); 2 Typer usage and sushicore usage; child code passed through for cmake, ctest, docker and the run target. Output is Rich only. `--describe` is the only JSON. No `--quiet`, no `--json`, no `--verbose`. Build output is the inherited child stdout; ctest output is drained line by line (`sushicore/proc.py`, `run_drained`).

**Docker.** Base `nvidia/cuda:12.4.1-devel-ubuntu22.04` (tag, no digest). Layers: apt base + gcc-13 PPA; Kitware cmake; Intel OpenCL runtime; intel/llvm nightly `2026-06-12` to `/opt/llvm-sycl`; AdaptiveCpp `v24.10.0` built from source with clang-17; optional oneAPI (`WITH_ONEAPI`); `COPY .`; `pip install ./cli`. Size drivers: the CUDA devel base, the llvm bundle, llvm-17-dev + libclang-17-dev, oneAPI when asked.

**CI (`ci.yml`).** Triggers: push to main, pull_request, workflow_call, nightly cron. Jobs: `functional` (oneAPI icpx, Release, distributed ON, ubuntu), `sanitizers` (TSan, RelWithDebInfo), `cli` (pytest + pyflakes), `docs` (doxygen), `benchmark` (schedule only), `checkers` (ubuntu + windows matrix, four checkers + tools unit tests). No cache anywhere. `release.yml`: on `v*` tag, calls ci.yml, then `sr docs bundle` and `gh release upload`; its header says it has never run.

**tools/.** Four checkers share `common/checker.py` (`Checker`, `Issue`, `Finding`, `run`; exit 0/1/2; `--report`, `--rule`). `write_license_block.py` and `packaging/verify_installed_package.py` have their own `argparse` and exit codes. Tests exist for `check_docs_layout`, `check_source_comments`, `write_license_block`.

### Prior-audit status table

| ID | Finding | Status | Evidence |
| --- | --- | --- | --- |
| C1 | `package --prefix` deletes anything | Closed | `services/stage_tree.py:46-96`, `packaging.py:60-68`, `tests/test_stage_tree.py` |
| C2 | Root marker is any CMakeLists.txt | Closed | `config.py:29` `root_marker="sushi-module.toml"`; `tests/test_config.py:30,42` |
| C3 | `hub install-cli --no-editable` documented | Closed | `docs/guides/CLI_GUIDE.md:27,30` |
| C4 | `--no-cuda` called Linux-only | Closed | `CLI_GUIDE.md:123` |
| C5 | Stale MSVC annotation macros in guide | Closed | `CLI_GUIDE.md:151-158` |
| C6 | `test --distributed` is dead | Open | `cli.py:132-133`; `project.py:472-473` prints a note only |
| C7 | `--distributed` two shapes; bad role accepted | Changed | Role is an enum now (`cli.py:166`, `project.py:56-60`, exit 2 pinned at `test_cli.py:132`); the bool/ROLE split under one name remains |
| C8 | `--sort` vs `--pick` vs `--select` | Open | `cli.py:164-165` |
| C9 | `docker` vs `container` | Closed in sr | `cli.py:65-69`, `aliases.py:18-19` |
| C10 | No `--version`, `--json`, `--describe` | Changed | `--version` and `--describe` added (`cli.py:52`); no `--json` |
| C11 | Malformed config gives a traceback | Closed | `sushicore/workspace.py:45-52` raises `ConfigError`; `entry.py:50-52`; `test_cli.py:62` |
| C12 | Install hints disagree | Changed | `services/install_hint.py`, `test_install_hint.py:37`. The Linux branch still says only "install it" (`project.py:246-250`) and one message still says `sr docker run` (`project.py:238`) |
| C13 | ASan lane: `--type asan` vs `--asan` | Open | `cli.py:98-100`, `137`, `169`, `308` |
| C14 | Services print Rich themselves | Open | `services/select.py:19,60-80`; `project.py:516`; every service calls `console.*` |
| C15 | Meta note on the audit itself | Not applicable | |
| C16 | `--force` ignored on Windows | Open | `project.py:427-433` never reads `force`; help at `cli.py:108-111` has no platform note |
| C17 | Unknown toolchain exits 1 | Open | `cli.py:79` `Optional[str]`; `select.py:92-95` |
| C18 | Dead code | Changed | `DockerAction` and unused imports are gone; `services/discovery.py:22,23,27` still export three names no file uses (grep finds only the definitions) |
| C19 | `config.toml` names `SUSHI_*` and "CLI flags" | Open | `cli/config.toml:4` |
| C20 | `.tools` fallback vs `setup` writing `cli/` | Open | `config.py:53-58`; `sushicore/provision/commands.py:213,315,336` |
| C21 | Missing docker prints cmake guidance | Open | `services/docker.py:31,46`; `sushicore/proc.py:278-284` |
| C22 | Outside a checkout: raw SystemExit line | Open | `sushicore/module_config.py:661-663` |
| C23 | `env --all` has no `-a` | Closed | `cli.py:307` |
| C24 | Guide calls docs/README the top-level README | Closed | root `README.md` exists; `CLI_GUIDE.md:375` links it |

`docs/reference/KNOWN_ISSUES.md:26` (CI installs sushicore from `./.sushistack/sushicore`) is stale: `ci.yml:87` now runs `pip install -e ./cli`. Line 27 (`|| true`) is still true (`ci.yml:92`). `docs/design/REMAINING_WORK.md:128-130` ("runs none of the checkers, has no release workflow") is stale on both counts.

### Findings

Ranked by how much each shortens the path from clone to a working, diagnosable run.

**OPS-01. high, cli.** `sr` cannot start against the published sushicore. Evidence: `cli.py:19` `from sushicore.docs_bundle import ApiSource, register_docs_commands` at module scope; `git ls-tree v0.8.0 sushicore/` in the sushicore checkout has no `docs_bundle` (it appears only at HEAD, under "Unreleased" in sushicore's changelog); PyPI's newest is 0.8.0; `cli/pyproject.toml:15` says `sushicore>=0.7.0`. `release.yml:7-8` admits it: "needs a published sushicore that carries `sushicore.docs_bundle`". A fresh `pipx install ./cli`, the Dockerfile's `python3 -c "import sushiruntime.cli"` (line 122) and the CI `cli` job all end in `ImportError`. I compared the git tag, not the wheel on PyPI. Change: release sushicore with `docs_bundle`, raise the floor in `pyproject.toml` to that version, and add a changelog line. Command change: none. Effort: S (plus a sushicore release).

**OPS-02. high, ci.** CI builds and tests around `sr`, on a toolchain that is not the default. Evidence: `ci.yml:27-36`, `51-71`, `145-155` call `cmake -B`, `cmake --build`, `ctest` with `icx`/`icpx`; `ci.yml:114` calls `doxygen Doxyfile`; `ci.yml:96` calls `python -m pytest`. The default toolchain (`intel-llvm`, `config.py:97`), `sr build`, `sr test`, `env.py`, the configure stamp and `_configure_args_*` never run in CI. `SR_SYCL_TOOLCHAIN` is not passed, so CMake infers it. Change: install the CLI in each job and call `sr build --distributed`, `sr test`, `sr docs`; add an `sr`-level command for the CLI tests and the checkers (see OPS-09) so the workflow holds no direct tool call. Command change: none. Effort: M.

**OPS-03. high, ci.** No Windows build or test job, and no ASan job. Evidence: only `checkers` has `windows-latest` (`ci.yml:159-162`). Never exercised in CI: Windows configure (`project.py:351-385`), vcvars snapshot, vcpkg path, `--type asan` on either platform, `--type debug`, `--no-cuda`, `--no-test`, the `package` suite, `sr package`, toolchains `intel-llvm` and `adaptivecpp`, the container image, `SR_ENABLE_DISTRIBUTED=OFF` for the functional suite. Change: add a Windows functional job and a Linux ASan job through `sr`; add the `package` suite to the tag workflow. Command change: none. Effort: L.

**OPS-04. high, cli.** No single report for a bug ticket. Evidence: the facts are spread over `sr doctor`, `sr config`, `sr env`, `sr toolchain` and `sr --version`; each prints a Rich table (`sushicore/diag.py:462-469`, `504-509`; `select.py:60-80`); `.github/ISSUE_TEMPLATE/bug_report.yml:57-63` asks the reporter to type OS, toolchain, CMake version, GPU and commit by hand. `sr doctor`'s compiler check passes on any `clang++` or `g++` on `PATH` (`sushicore/provision/checks.py:73-78`), not the selected toolchain's compiler, and it does not print the resolved compiler path, the configure stamp or the snapshot source. Change: add `sr doctor --report` (or `sr report`) that prints plain text or JSON: CLI and sushicore versions, platform, toolchain and resolved compiler with its `--version` line, config values with their source file, the build-relevant environment as `KEY=VALUE`, cache and stamp state of `build/` and `build-asan/`, the doctor rows. Point the issue template at it. Command change: additive. Effort: M.

**OPS-05. high, environment.** A second SYCL stack in the parent shell is neither detected nor reported. Evidence: `env.py:157,163,166` merge the snapshot over the whole `os.environ`; `_inject_toolchain_paths` (85-89) only prepends. A shell that sourced oneAPI's setvars keeps oneAPI's lib dir in `LD_LIBRARY_PATH` behind the bundle's; the `Dockerfile:45` sets `LD_LIBRARY_PATH=/opt/llvm-sycl/lib` for every process, and `_neutralize_runtime_ld` (`project.py:139-141`) restores exactly that value for the oneapi lane, so an icpx-built binary in the container resolves `libsycl`/`libur_loader` from the llvm bundle first. This is the same class as the past `urVirtualMemMap` crash. Read from source, not observed. Change: one function in `env.py` that lists every directory on `PATH`/`LD_LIBRARY_PATH` holding a SYCL runtime (`libsycl*`, `sycl*.dll`, `libur_loader*`), warns when more than one stack is present, and strips the ones that do not belong to the selected toolchain from the child environment; surface the same list in OPS-04's report and as a doctor row. Command change: none. Effort: M.

**OPS-06. high, cli.** Cold start has unchecked steps and two entry points that disagree. Path today, Windows and Linux alike: (1) clone; (2) get Python >= 3.10 and pipx (assumed, unchecked); (3) `hub install-cli sushiruntime` (`CONTRIBUTING.md:39`, `CLI_GUIDE.md:27`), which needs SushiStack that a single-repo clone does not have, or `pipx install ./cli` (`CLI_GUIDE.md:34`); (4) `sr setup`; (5) `sr build`; (6) `sr test`. Gaps: step 3 hits OPS-01; `sr setup` needs network, winget or apt and sudo, none checked up front; `sr build` on Linux with a missing compiler prints "install it" without the command (`project.py:246-250`) although `toolchain_install_command` exists; on Windows `_configure_args_windows` interpolates empty `vcpkg_root`, `ninja_exe`, `pkgconf_exe` into `-DCMAKE_TOOLCHAIN_FILE=/scripts/buildsystems/vcpkg.cmake`, `-DCMAKE_MAKE_PROGRAM=`, `-DPKG_CONFIG_EXECUTABLE=` (`project.py:354-369`) when `sr setup` was skipped, so the failure is a CMake error, not a message naming `sr setup`; a missing vcvars is silent (`sushicore/build_env.py:93-95`). Change: `sr build` runs the `build` doctor group first and stops with the failing rows and their fix commands; the Windows configure validates the three paths before assembling arguments; the Linux message uses `toolchain_install_command`; the guides lead with `pipx install ./cli` then `sr setup` for a lone checkout. Command change: none. Effort: M.

**OPS-07. medium, cli.** Configuration mistakes pass silently. Evidence: unknown keys dropped (`sushicore/config_base.py:318-319`); an invalid `toolchain` value is rewritten to `intel-llvm` without a word (`config.py:162-163`), so `SR_SYCL_TOOLCHAIN=onapi` builds with the wrong compiler; `sr toolchain X` reports success while `SR_SYCL_TOOLCHAIN` still wins (`select.py:98-99`); `sr config` labels both files "config.toml" and never reads the workspace file (`sushicore/diag.py:416-431`). The test for the fallback re-implements the guard instead of calling it (`cli/tests/test_config.py:64-71`). Change: raise `ConfigError` on an unknown key and on an unknown toolchain, naming file or variable; have `sr toolchain` warn when the environment overrides it; make `sr config` name the exact layer. Command change: none; a config with a typo that used to load now stops. Effort: M (half in sushicore).

**OPS-08. medium, ci.** A red run says little. Evidence: configure, build and test share one step per job (`ci.yml:23-36`, `47-71`, `141-155`), so the failing phase is not visible in the job list; no test result file (`--output-junit`) and no log artifact is uploaded; no step prints the environment; `python -m pyflakes sushiruntime || true` cannot fail (`ci.yml:92`) and its step name "Import-safety check" does not describe pyflakes; Doxygen warnings are reported and never fatal (`ci.yml:115-122`). Change: one step per phase named after the `sr` command it runs, upload ctest JUnit and `LastTest.log`, make the lint step fail or delete it. Command change: none. Effort: S.

**OPS-09. medium, tools.** The checkers are not reachable through `sr`. Evidence: `ci.yml:169-178` and `tools/README.md` call `python tools/...` directly; no `check`/`lint` command in `cli.py`; `CONTRIBUTING.md` never mentions them. Change: `sr check [--report] [--rule]` running the four checkers and `tools/tests`, and `sr check --cli` (or `sr test --suite cli`) running the CLI's pytest suite; CI calls those. Command change: additive. Effort: S.

**OPS-10. medium, ci.** Nothing is cached and every job repeats the slowest step. Evidence: `.github/actions/setup-oneapi/action.yml:10-20` apt-installs `intel-oneapi-compiler-dpcpp-cpp` (several GB) in `functional`, `sanitizers` and `benchmark`; no `actions/cache` in either workflow; no `concurrency:` group, so superseded pushes keep running; push to main and its pull request both trigger. Change: cache the toolchain keyed on the hash of `cli/sushistack.deps.toml` plus the pinned bundle date (the continuous-integration skill's rule), add a `concurrency` group with cancel-in-progress, add `timeout-minutes`. Command change: none. Effort: M.

**OPS-11. medium, docker.** The build context carries host-only files into the image. Evidence: `.dockerignore` lists `build/`, `bin/`, `.git/`, `**/build/`, caches; it omits `build-asan/`, `venv/`, `cli/config.local.toml`, `cli/config.local.toml.bak`, `.github/`, `docs/agent/`. `Dockerfile:108` is `COPY . ...`, so the host's `config.local.toml` (its `[tool] toolchain` key included) and an ASan tree configured for a Windows path land in the image. `sr container run` then mounts the checkout over it (`docker.py:45`), so the copy only serves `pip install`. Change: copy only `cli/` before the pip step, drop `COPY .`, and add the missing ignore rules. Command change: none. Effort: S.

**OPS-12. medium, docker.** Pins and floats are mixed, and one pin contradicts the manifest. Pinned: base tag `12.4.1` (no digest), `INTEL_LLVM_DATE=2026-06-12` (no checksum on the `wget`, `Dockerfile:37-42`), `ADAPTIVECPP_VERSION=v24.10.0`, `ACPP_LLVM=17`. Floating: every apt package, `cmake` from Kitware (the comment says 3.31, the install takes whatever is current, `Dockerfile:21`), `pip`, `setuptools`, `wheel`, `typer`, `rich`, `sushicore`. `cli/sushistack.deps.toml:59` says CUDA is "Pinned to 12.6"; the image is 12.4.1. The container and `sr setup` fetch the bundle through two code paths with two date pins. Change: one source for the bundle date and CUDA version (the deps fragment), read by the Dockerfile as build args that `sr container build` passes; verify the archive digest; pin cmake. Command change: none. Effort: M.

**OPS-13. medium, cli.** `sr toolchain` reports the working compiler as "not installed" wherever the bundle is on `PATH` but outside the provision roots. Evidence: `select.py:37-40` requires `resolved_llvm_root()`; `config.py:126-130` searches only `toolchains/llvm-sycl[-nightly]` under the dependency roots (`sushicore/provision/probe.py:128-134`); the image installs to `/opt/llvm-sycl` (`Dockerfile:40-44`). The other two toolchains fall back to `shutil.which`; intel-llvm does not. The command then advises `sr setup --toolchain intel-llvm` inside a container that already builds. Change: one `ToolchainProbe` per toolchain with the same three steps (pinned path, provision root, `PATH`), used by `select.py`, `project.py` and doctor. Command change: none. Effort: S.

**OPS-14. medium, cli.** Lane and feature options differ between sibling commands. `build --type asan` vs `test/run/env --asan` (C13); `--distributed` is a build switch, a no-op on `test` (C6) and a ROLE on `run` (C7); `package` has no lane option and always reads `build/` (`packaging.py:49`); `clean` has no lane option and removes both trees while `build --clean` removes one; `--no-cuda` on `build` vs `--no-gpu` on `setup` and `container run`, where `--no-cuda` actually sets `SR_GPU_BACKEND=cpu` for every vendor (`project.py:409-412`); `--force` means "fall back to clang" on `build` and "clear a foreign directory" on `package`; `test` has `--filter/-f` and `--repeat/-r` but `--asan` has no short form anywhere; `--sort` picks interactively (C8). Change: one lane option `--lane {default,asan}` on build, test, run, env, package, clean; `run --role`; remove `test --distributed` or make it fail on a tree built without it; `build --no-gpu`; `build --fallback-compiler`; `run --pick`. Each old spelling stays as a dated row in `aliases.py`. Command change: yes, with aliases. Effort: M.

**OPS-15. medium, cli.** `sr test` and `sr build` give the operator no control over parallelism or time. Evidence: `project.py:449` and `480-483` pass no `jobs`, although `CMakeDriver.compile` and `ctest_run` accept it (`sushicore/cmake_driver.py:511-557`); ctest therefore runs serially; no `--timeout`; no `--verbose` to see the compiler lines; no elapsed-time line at the end of a build. Change: `--jobs/-j` on build and test, `--timeout` on test, a final line with duration and tree path. Command change: additive. Effort: S.

**OPS-16. medium, cli.** Nothing is machine-readable beyond `--describe`. Evidence: no `--json` or `--quiet` in `cli.py`; services print as they go (C14); `sr test` cannot emit JUnit. Change: services return result objects and the command layer renders them (the C14 prerequisite); then `--json` on `config`, `env`, `toolchain`, `doctor`, and `test --junit PATH`. Command change: additive. Effort: L.

**OPS-17. medium, cli.** Layering inside `cli/` breaks the python-code-style rule that a service neither imports the framework nor writes to the terminal, and `packaging.py` reaches into another module's private names. Evidence: `packaging.py:18` `from .project import _build_dir, _DRIVER, _RUNNER`; `services/diag.py:48-49` rebuilds the `build-asan` path by hand instead of sharing `_build_dir`; `project.py` (540 lines) holds build types, suites, ASan flags, the stamp, compiler resolution for two platforms, configure arguments for two platforms, and five commands; `project.py:114-115` and `docker.py:17` create module-level `Runner`/`CMakeDriver` singletons; `_configure_args_linux` and `_configure_args_windows` are not shaped alike (one takes `cc, cxx`, the other resolves inside). Change: split into `build_tree.py` (lane -> directory, stamp), `sanitizer_flags.py`, `compiler_resolver.py` with one class per platform behind one protocol, `configure_arguments.py` likewise, and thin `build.py`/`test.py`/`run.py`; inject the runner. Command change: none. Effort: L.

**OPS-18. medium, cli.** Code that belongs in sushicore or `ss`, not here: the container argv builders (`services/docker.py`, C9); the Linux setvars snapshot (`env.py:92-114`), the sibling of `snapshot_windows` that sushicore already owns; `_inject_toolchain_paths` (`env.py:50-89`), a near copy of `StackBuildEnv.inject_toolchain` (`sushicore/build_env.py:185-210`); `load_build_env` (`env.py:138-166`), the same three-branch body as `StackBuildEnv.load` (212-238); the `env` command declared locally only to add `--asan` (`cli.py:298-312`); `_DOXYGEN_HINT` (`project.py:118-123`), a copy of sushicore's default with one clause added; toolchain presence probing (`select.py:34-52`) beside `probe.toolchain_status`; the bundle directory names (`config.py:71`). Module boundaries are the owner's decision; this is the list. Command change: none. Effort: M.

**OPS-19. medium, tools.** `check_layering.py` passes while checking nothing. Evidence: `K_TIER_ORDER = ()` (`tools/layering/check_layering.py:23`); `collect` returns no subject when `modules/` is absent (62-67), so even `rule_declared_tiers` never fires; CI shows it green (`ci.yml:175-176`). Change: decide whether the include-area order of ARCHITECTURE section 2 is enforced; until then the checker should print that it found no modules and the CI step should say so. Command change: none. Effort: S to report, M to enforce.

**OPS-20. medium, tools.** No tool covers the CLI and CI surface. Not checked by anything: `check_changelog.py` and `check_layering.py` have no tests under `tools/tests/`; `CLI_GUIDE.md` against `sr --describe` (the drift C3-C5 were); workflow files calling tools directly (the rule in OPS-02); `TOOLCHAINS` in `config.py:68` against `SR_SYCL_TOOLCHAIN` in `CMakeLists.txt` (kept in step by a comment); `CMakePresets.json` against the arguments `project.py` passes; Python style (pyflakes is non-fatal, no formatter, no type check although `py.typed` ships). Change: a test that diffs the guide's command table against `--describe`, tests for the two untested checkers, and a fatal lint step. Command change: none. Effort: M.

**OPS-21. low, tools.** `verify_installed_package.py` and `write_license_block.py` are not shaped like their siblings. Evidence: own `argparse`, constants without the `K_` prefix (`verify_installed_package.py:24-26`), `[FAIL]`/`[OK]` lines instead of `path:line: rule: message`, exit 1 for a bad path where the checkers return 2 (`:91-98` vs `common/checker.py:92-95`); the Linux scrubbed `PATH` is `/usr/bin:/bin:<prefix>/bin` (`:57`) while the Windows one names `SystemRoot` only, and nothing documents the difference. Change: share the exit-code constants from `common/checker.py`, rename the constants, state the two environments in `tools/packaging/README.md`. Command change: none. Effort: S.

**OPS-22. low, cli.** Stale text an operator reads. `cli/config.toml:4` (`SUSHI_* env vars -> CLI flags`, C19); `cli/config.toml:40` and `config.py:157,171,182` and `sushistack.deps.toml:22-24,34` still say `hub install` where the lone-checkout path is `sr setup`; `project.py:238` and `Dockerfile:81,133` say `sr docker ...`; `conftest.py:23-24` and its error text point at `<sushistack>/sushicore`, a path that no longer exists; `cli/README.md:5` says sushicore "ships in the SushiStack repository"; `pyproject.toml:8` description says "docker"; `services/docker.py:6` names scripts that are gone; `bug_report.yml:10` names `SECURITY.md` at the root. Change: edit the lines. Command change: none. Effort: S.

**OPS-23. low, cli.** Small defects from the prior audit, still open: `--force` ignored on Windows (C16); unknown toolchain exits 1 where a bad `--type` exits 2 (C17); three unused exports in `services/discovery.py:22-28` (C18); the `.tools` fallback that `setup` does not honour (C20); a missing `docker` prints cmake guidance (C21); outside a checkout the message bypasses the console (C22). Change: as the prior audit proposed; type `toolchain` as an enum built from `TOOLCHAINS`. Command change: C17 changes one exit code. Effort: S each.

**OPS-24. low, cli.** `rmtree(..., ignore_errors=True)` hides a partial delete on the build tree. Evidence: `project.py:426` before configure, `sushicore/cmake_driver.py:563` in `clean_tree`, which then prints "removed" unconditionally. A locked DLL on Windows leaves a half tree and the next configure fails for an unrelated-looking reason. `StageTree.reset` already does this correctly (`stage_tree.py:104-110`). Change: the same raise-and-report shape for the build tree. Command change: none. Effort: S.

**OPS-25. low, cli.** The configure stamp keys on the raw `cfg.cxx` setting, not the resolved compiler. Evidence: `project.py:419-420` passes `cfg.cxx` (empty unless pinned) while the Linux branch may substitute a fallback compiler (`project.py:229-234`, `253-256`). sushicore's `compiler_changed` covers a changed `CMAKE_CXX_COMPILER` path, so this is belt-and-braces today; `--force` appearing or disappearing between two builds is the uncovered case. Change: build the stamp from the resolved pair. Command change: none. Effort: S.

**OPS-26. low, ci.** `release.yml` has never run and repeats a version rule in inline Python. Evidence: header lines 7-8; the tag check at 33-42 is a heredoc in the workflow, and the same version lives in `CMakeLists.txt`, `version.hpp`, `cli/pyproject.toml:7` (1.0.0) and `cli/sushiruntime/__init__.py:8`. The job installs doxygen with apt and the CLI with pip, uncached. Change: move the check into a tool under `tools/` (or `sr docs bundle --check-tag`), and decide the CLI version question already in `REMAINING_WORK.md:121-124`. Command change: none. Effort: S.

**OPS-27. low, docker.** `sr container` hard-codes the image name, the mount point and `/bin/bash`. Evidence: `docker.py:15` `IMAGE = "sushiruntime"`, `:45`. No tag option, no way to run one command non-interactively (`-it` is fixed at `:37`, so it cannot be used from CI), no check that docker or the NVIDIA runtime is present before `--gpus all`. Change: `container run [-- COMMAND]`, drop `-it` when stdin is not a terminal, read the image name from config. Command change: additive. Effort: S.

### Not checked

- No command was run, so help pages, real exit codes, the ImportError of OPS-01 and the library resolution order of OPS-05 are read from source and from `git ls-tree`, not observed. The PyPI 0.8.0 wheel was not downloaded; only its git tag was compared.
- sushicore: read `entry.py`, `root_options.py`, `diag_commands.py`, `diag.py`, `proc.py`, `build_env.py`, `config_base.py`, `cmake_driver.py`, `module_config.py`, `profile.py`, `provision/commands.py`, `provision/config.py`, parts of `provision/checks.py`, `probe.py`, `home.py`, `workspace.py`. Not read: `docs_bundle/`, `aliases.py`, `describe.py`, `typer_help.py`, `cli_console.py`, `cmake_cache.py`, `toolchain_args.py` beyond its first function, the provision pipeline, steps, package managers, lock, `doctor.py` rendering. What `sr setup` does on a network or permission failure is unknown.
- `ss`/SushiStack and the `sushicli` repository were not opened. The brief names `sushicli` as the shared presentation layer; the code imports `sushicore` only, and I did not establish how the two relate.
- `cli/tests/`: test names were read in full; bodies only for `conftest.py`, the malformed-config test and the toolchain-fallback test. `cli/README.md` was read to line 115.
- `tools/documentation/check_docs_layout.py`, `check_source_comments.py` and `licensing/write_license_block.py` were outlined by grep (definitions, constants, registration), not read line by line; the three files under `tools/tests/` were not read. `feature_request.yml` was not read.
- `CMakeLists.txt`, `cmake/`, `tests/CMakeLists.txt` and `CMakePresets.json` are outside this scope and were only grepped, so the claims about `SR_SYCL_TOOLCHAIN` inference and presets rest on comments in the CLI.
- CI run history, durations and the state of the last run on GitHub were not queried; "duration drivers" is inferred from the workflow text.
- macOS: the code treats every non-Windows platform as Linux; not assessed.

### Delegation block evidence

1. SOLID: applied as a named review item; OPS-13, OPS-17, OPS-18 and OPS-21 are the shape findings.
2. Humanizer: loaded before writing; this report was passed against the generic English checklist (no em dashes, no closing summary, facts in place of evaluative words).
3. No `sr`, `se`, `cmake`, `ninja` or `ctest` process was started; no build configuration file was written.
4. Nothing was written in the repository. `git status --short` at the end shows the same two modified files as at the start: `docs/reference/CHANGELOG.md`, `include/SushiRuntime/api/runtime/runtime.hpp`.
5. Verifications are quoted inline with file:line.
6. No source written; nothing to syntax-check.
7. Scratchpad: I wrote nothing to it. `du -sh` reports 4.0K; it holds one file, `links.py`, that another agent left and I did not touch.

## Tests and benchmarks (TEST)

Read-only audit of `D:\Projects\sushiruntime\tests`. Nothing was built or run; every statement below comes from reading files and from grep.

### Facts

- Size: 83 C++ files and 20,319 lines under `tests/` (READMEs and CMake included). 388 `TEST(` macros: 136 in 24 unit files, 223 in 43 integration files, 3 in 1 regression file (the brief says 2 files; `regression/` holds one `.cpp` and one README), 20 in 12 performance files, 6 in 2 stress files. 27 of those sit in the three distributed files, which compile only with `SR_ENABLE_DISTRIBUTED`.
- Fixtures: none. `grep -rc "TEST_F\|TEST_P\|INSTANTIATE_TEST" tests` returns zero for every file. Every test is a free `TEST` that builds its own runtime.
- Binaries (`tests/CMakeLists.txt`): `sr_functional_tests` (lines 92-169), `sr_allocation_tests` (211-221, its own process because it replaces `operator new`), `sr_benchmark_tests` (226-265, behind `SR_BUILD_BENCHMARKS`), and two `package` tests (284-308).
- Registration: `gtest_discover_tests(... DISCOVERY_MODE PRE_TEST TEST_FILTER "Unit_*" PROPERTIES LABELS "unit")` and the same for `Integration_*`, `Regression_*`, `Stress_*`, `Performance_*`. One label per test, taken from the suite-name prefix. The folder a file sits in does not decide its label.
- Selection through the CLI (`cli/sushiruntime/services/project.py:63-87, 457-484`, `sushicore/cmake_driver.py:130-157`): `sr test --suite {unit,regression,integration,functional,stress,performance,benchmark,package,all}` maps to `ctest -L`; `--filter` maps to `ctest -R` over `Suite.Case`; `--repeat N` maps to `--repeat until-fail:N`. `jobs` is never passed, so ctest runs serially. No `TIMEOUT` property is set anywhere, so ctest's 1500 s default applies.
- Sanitizers: `cmake/Sanitizers.cmake:23-28` turns ThreadSanitizer on for every Debug and RelWithDebInfo build off Windows. AddressSanitizer is `sr build --type asan`, an isolated `build-asan` tree, CUDA off, `SR_BUILD_BENCHMARKS=OFF` (`project.py:44-45, 282`). CI (`.github/workflows/ci.yml`) has a TSan job and no ASan job.
- Under a sanitizer: `test_steady_state_allocation.cpp:53-60` replaces all five tests with one `SkippedUnderSanitizer` (ASan and TSan). `test_main.cpp:62-66` leaves through `std::_Exit` under TSan to dodge a crash in the Intel runtime's teardown. The whole benchmark binary is left out of ASan builds because `test_recompose_churn_perf.cpp` replaces `operator new`.
- Suppressions: `tests/lsan_suppressions.txt` holds one line, `leak:libintelocl`. `cli/sushiruntime/env.py:118-135` points `LSAN_OPTIONS` at it for every build.
- Random sources: all seeded (`test_counting_sort.cpp:97,147,190,382,438,524`, `test_distributed_jepsen.cpp:157,172,310`). No `random_device`, no `rand()`.
- Float comparisons: 14 `EXPECT_NEAR`/`FLOAT_EQ`/`DOUBLE_EQ` uses; the determinism and reduce tests compare bit patterns on purpose (`test_fixed_order_reduce.cpp:84-101`).
- Stored baselines: none. `git ls-files | grep -iE "baseline|bench.*\.(json|csv|txt)"` finds no data file. No benchmark writes JSON or CSV; the nightly CI job uploads no artifact.

#### Commands used

```
# header -> test files
for h in $(find include/SushiRuntime -name '*.hpp' -o -name '*.h' | sed 's|include/||' | sort); do
  grep -rl --include=*.cpp --include=*.hpp "#include <$h>" tests; done
# non-api includes per test file
grep -o "#include <SushiRuntime/[a-z_/]*\.h[p]*>" $f | grep -v "SushiRuntime.h\|api/"
# skips, sleeps, clocks
grep -rn "GTEST_SKIP\|DISABLED_" tests ; grep -rn "sleep_for\|sleep_until" tests
grep -rn "steady_clock\|duration_ms" tests/functional
# entry-point sampling: one regex per symbol, counted over tests/functional, tests/benchmark, examples
grep -rE "\.run_async\(" tests/functional --include=*.cpp | wc -l      (and 60 more of the same shape)
```

Sampling method for public entry points: I took every public method of `Runtime`, `AdvancedControls`, `Cluster`, `Buffer`, `State`, `RunHandle`, the free functions in `vocabulary/dynamic.hpp`, the types in `vocabulary/captures.hpp`, `combine.hpp`, `launch/*.hpp`, `core/error.hpp`, `version.hpp`, and the distinct public method names of `Graph` and `DynamicGraph` (read off a regex over declarations at 16-space indent, so overload sets count once). About 60 symbols. I did not walk the 18 `Graph::add` overloads one by one.

### Coverage table

"Direct" means a test file includes the header. Everything under `api/` reaches tests through the umbrella `SushiRuntime.h`, which 36 functional files, 4 benchmark files and `tests/package/main.cpp` include, so the api rows list tests by symbol use.

#### Public API (by symbol)

| Header | Tests that exercise it | Gaps |
| --- | --- | --- |
| `api/runtime/runtime.hpp` | `Runtime::create` in 37 functional files; `advanced()` in 8; `rebalancer` in 6 (`test_perf_controls`); `worker_pool` in `test_co_tenancy`, `test_determinism`; transfer byte counters in `test_observability`; `interop()` in `test_interop_host_export`; `unsafe_context()` in 4 files | none found |
| `api/runtime/cluster.hpp` | `test_distributed_e2e` only (`FluentClusterOffload`, `FluentDistributedStateParity`) | `wait_for_workers` timeout path, `liveness_timeout`, `task_deadline`: 2 hits each, set but never asserted to take effect |
| `api/data/buffer.hpp` | `unit/test_allocation` (direct), `test_device_resident`, `test_fluent_api`, `test_device_count` | `host()` 2 hits in 1 file; `distributed()` 4 hits in 1 file; a moved-after-add buffer is a known issue with no test |
| `api/data/state.hpp` | `test_fluent_lifecycle`, `test_device_resident`, `test_fluent_hardening`, `test_cancellation` | `State::advance()` is public and no test calls it; `read_range`/`write_range` on a State: 2 files |
| `api/graph/graph.hpp` | 30+ integration files. `add`, `add_host` (6 files), `add_untracked` (4), `add_reduce` (7), `add_segmented_reduce` (3), `add_prefix_scan` (4), `add_counting_sort` (2), `name_last` (4), `run`, `try_run` (5), `run_until` (3), `run_async` (3), `compile_count` (12) | `run_for`: 1 file, 2 tests; `cancel`: 1 file; `has_dangling_resource()`: no direct call (reached through `try_run` in `DanglingResourceIsReported`) |
| `api/graph/dynamic_graph.hpp` | `test_dynamic_graph` (13), `test_dynamic_graph_node_events` (5), `test_region_emitter` (4), `test_async_step`, `test_run_report_reuse` | `drop` in 3 files; no test drops a region while an async step is in flight |
| `api/graph/run_handle.hpp` | `test_async_step` (10), `test_run_report_reuse`, `test_steady_state_allocation` | `try_wait`: 1 test; `ready`: 1 test; the move constructor is untested |
| `api/graph/region.hpp` | `test_dynamic_graph`, `test_region_emitter` | none found |
| `api/launch/extent.hpp`, `nd_range.hpp` | `test_fluent_hardening`, `test_fluent_api` (2 files each) | 3-D extents and ranges: no test |
| `api/vocabulary/dynamic.hpp` | `when` 3 files, `sized` 3, `sized_from_device` 3, `based_at` 1, `based_at_device` 2 (`test_late_bindings`, `test_device_count`, `test_device_sized_primitives`) | none found |
| `api/vocabulary/captures.hpp` | `In`/`Out`/`InOut`: 2 functional files, 2 hits each (`test_fluent_hardening`) | pinned more by `examples/` (12 hits) than by tests (5) |
| `api/vocabulary/combine.hpp` | `Sum` 7 files; `Minimum` 1 file; `Maximum` 2 | none found |
| `api/vocabulary/dependencies.hpp` | `Reads`/`Writes` in 27 files | none found |
| `api/detail/*` (6 headers) | `unit/test_late_count` names `API::detail::plan_tiled_levels`, `live_tile_levels`, `plan_reduction`, `resolve_late_count`, `fill_radix_plan`, `radix_digit_count`; `unit/test_fixed_order_reduce` names `detail::` 3 times | `callable_traits.hpp`, `device_fault.hpp`, `run_report_builder.hpp`, `step_machinery.hpp`: only through the facade |
| `version.hpp` | `tests/package/main.cpp` (`version_matches`) | no functional test; the only check lives in the package lane, which `--suite functional` does not run |
| `core/error.hpp`, `core/result.hpp` | `SushiException` in 13 files; `Errc::invalid_graph` 3 hits, `invalid_access` 4, `dangling_resource` 1, `unsupported` 3 | `Errc::compile_failed`, `allocation_failed`, `kernel_failed`, `device_lost` appear in no test. Unit tests for Result and Error: already known, prior audit C20 |

#### Internal modules

| Module | Header | Test files |
| --- | --- | --- |
| core | `delegate.hpp` | `unit/test_delegate` |
| | `logger.hpp` | `unit/test_logger` (2 tests), `common/test_main` |
| | `ref_counted.hpp`, `sushi_ptr.hpp` | `unit/test_ref_counted` |
| | `run_report.hpp` | `unit/test_node_event_record`, `unit/test_node_dispatch_width` |
| | `span.hpp`, `sycl_compat.hpp` | included for use only, by the distributed and topology tests |
| | `bit.hpp`, `dispatch_width.hpp`, `event_timestamps.hpp`, `profiling_ledger.hpp`, `resource_liveness.hpp`, `resource_region.hpp`, `strong_id.hpp`, `result.hpp`, `error.hpp` | no direct test (already known, C20, for most of them) |
| execution | `runtime_context.hpp` | 25 files: 8 unit, 9 integration, 8 benchmark, plus regression |
| | `execution_engine.hpp` | `unit/test_registration_index` |
| | `chain_plan.hpp`, `chain_run.hpp`, `runtime_settings.hpp` | no direct include (already known, C20). Behaviour is held from above by `test_chain_layer` (4) and `test_device_sized_primitives` (6) |
| graph | `task_graph.hpp`, `node.hpp`, `task_types.hpp` | `test_graph`, `test_sycl_execution`, `test_device_pipeline`, `test_heterogeneous_pipeline`, `unit/test_node_dispatch_width`, `unit/test_registration_index`, `test_graph_compile_perf`, the three distributed files |
| | `dependency_tracker.hpp` | `unit/test_dependency_tracker` (11) |
| | `late_bindings.hpp` | no direct include; `test_late_bindings` (11) reaches it through the facade |
| memory | `allocation.hpp` | `unit/test_allocation` |
| | `epoch_reclamation.hpp` | `unit/test_epoch_reclamation`, `stress/test_epoch_stress`, regression |
| | `usm_registry.hpp` | `unit/test_usm_registry` (10) |
| | `usm_allocation_owner.hpp` | none (already known, C20) |
| scheduler | `task_scheduler.hpp` | `test_scheduler_integration`, `unit/test_numa_steal`, regression, 4 performance files, `stress/test_scheduler_stress` |
| | `work_stealing_deque.hpp` | `unit/test_work_stealing_deque` (3) |
| | `event_polling.hpp` | `unit/test_event_polling` (4) |
| | `task.hpp` | `unit/test_range_task`, `common/test_helpers.hpp` |
| | `workload_coordinator.hpp`, `rebalancer_interfaces.hpp`, `thermal_proxy_strategy.hpp` | `test_workload_coordinator`, `unit/test_thermal_rebalance` |
| | `ready_sink.hpp` | none (already known, C20) |
| topology | `affinity.hpp` | `unit/test_affinity` |
| | `hw_discovery.hpp`, `numa_mapper.hpp` | `unit/test_topology`, `unit/test_nvidia_topology` |
| | `hwloc_topology_provider.hpp`, `matrix_distance_model.hpp`, `remapping_topology_provider.hpp` | `unit/test_topology_abstraction`, `unit/test_numa_steal`, `test_steal_numa_effect_perf` |
| | `i_topology_provider.hpp` | indirect only |
| interop | `device_uuid.hpp`, `interop_capabilities.hpp` | `unit/test_interop_vocabulary` (2) |
| | `interop.hpp`, `host_export.hpp` | `test_interop_host_export` (4), through `rt.interop()` |
| | `external_memory.hpp`, `detail/bindless_detection.hpp` | none; the import path has one test, the refusal, and it skips when the device can import (`test_interop_host_export.cpp:82`) |
| distributed | `protocol.hpp`, `buffer_registry.hpp` | `unit/test_distributed` (16) |
| | `in_process_transport.hpp`, `i_offload_policy.hpp`, `kernel_registry.hpp`, `worker_agent.hpp` | `unit/test_distributed`, `test_distributed_e2e`, `test_distributed_jepsen`, `test_distributed_offload_perf` |
| | `tcp_transport.hpp` | `test_distributed_e2e` only (already known, C20) |
| | `distributed_coordinator.hpp`, `i_transport.hpp` | indirect only |

### Benchmark table

"Per-node path" asks whether the file can answer "did this change make the per-node submit and complete path faster".

| File | Measures | Metric | Runs | Speed threshold | Machine-readable | Per-node path |
| --- | --- | --- | --- | --- | --- | --- |
| `test_chain_dispatch_perf` | `Graph::run` over a chain of K dependent trivial nodes, K = 1, 16, 256, 1024, scheduler layer and chain layer | ms/step, ns/(node*step), from `report.total_duration_ms` | 1 | none (`EXPECT_GT(ms, 0)`) | no, `printf` table | Yes. This is the closest thing to the engine's 96-98% figure: the scheduler rows are one round trip per node with no fan-out. One run, no spread |
| `test_scheduler_dispatch_perf` | 500,000 `AtomicCounterTask` through `TaskScheduler` against an inline loop | ns/task, tasks/s | best of 3 | none | no | Partly. The timed loop includes `new AtomicCounterTask` per task (line 122) and ends on a 1 ms poll (line 67), and tasks have no dependencies, so it measures submit, steal and dispatch without completion hand-off to a successor |
| `test_wide_fanout_perf` | 20,000 steps of 64 independent tasks, drained each step | us/step, ns/task, best and worst step | 1 | none | no | Partly: the per-step drain tail, again with a `new` per task in the timed region |
| `test_root_fanout_perf` | `Graph::run` over R independent roots, R = 1..1024 | ms/step, ns/(root*step) | 1 | none | no | Yes for injection and completion of roots, not for successor hand-off |
| `test_recompose_churn_perf` | `DynamicGraph` commit: one `region().add()` plus `run(1)`, 256 regions, 64 commits, against a pure replay step | ms and allocations per commit and per replay step | replay best of 3, commits averaged | none | no | No. It answers graph build and commit cost; on a Windows DLL build its allocation count sees the executable side only (README lines 98-102) |
| `test_dynamic_graph_perf` | 256 regions streamed in, 16 mutations | initial compose+run ms | 1 | none | `RecordProperty("initial_compose_run_ms", int)`, truncated to whole ms | No |
| `test_graph_compile_perf` (2 tests) | `TaskGraph::compile()` for 64..16,384 nodes; H2D/D2H overlap with compute | us, us/node; ms, speedup | best of 3 | none | no | No. It times the internal `TaskGraph`, not `Graph::add` plus first run through the facade |
| `test_profiling_queue_perf` | Submission cost on a profiling queue against a plain queue | us/submit | best of 3, order alternated | none | no | No (SYCL submit cost, outside the scheduler) |
| `test_async_overlap_perf` | CPU and GPU kernels overlapping on two queues | ms, speedup | best of 3 | Yes: fails at 0.95 of serial, fails above 0.75 except on WSL2 where it skips (lines 236-247) | no | No |
| `test_steal_locality_perf` | Local against remote steals, 300,000 tasks | tasks/s, local fraction | 1 | `local_frac > 0.5` above 1000 steals; skips on one NUMA node | no | No |
| `test_steal_numa_effect_perf` | NUMA-aware against blind stealing on a synthetic two-node topology | tasks/s, speedup | best of 3 | `local_frac > 0.5` only on real multi-socket | no | No |
| `test_distributed_offload_perf` | 256 offloaded vector adds over `InProcessTransport` | tasks/s, us/task, worker split | 1 | none | no | No. The nightly CI job never builds it: `ci.yml:171-176` omits `-DSR_ENABLE_DISTRIBUTED=ON` |
| `stress/test_scheduler_stress` (4 tests) | 1M-task drain, 8 producers, abrupt stop, priority bypass | pass/fail | 1 | 60 s style deadlines, priority window | no | No |
| `stress/test_epoch_stress` (2 tests) | Retire and reclaim under 8 threads and thread churn | pass/fail | 1 | reclaimed more than half | no | No |

No file prints its build type. None prints the worker count and machine in a form a script could parse. `tests/benchmark/README.md:79-80` quotes one stored number (24 us on an RTX 3080 Ti); that is the only reference figure in the tree.

Measurements that do not exist:

- Per-node scheduling overhead as a repeatable number: `test_chain_dispatch_perf` has the right shape and runs once per case with no spread.
- Graph build cost through the public API: time for N `Graph::add` calls, then first `run()` (compile + chain plan + first dispatch) separated from replay.
- Chain planning: nothing times `ChainPlan` construction or the decision between chain and scheduler layers.
- Steady-state allocation as a number: `test_steady_state_allocation` asserts ratios (`< 4 * short`, lines 258, 303, 362) and records properties, but reports no allocations per tick for the scheduler layer per node, and it only counts the calling thread, so worker-thread allocations are invisible.
- Start-up time: nothing times `Runtime::create()` or the first `run()` on a cold process. With one process per ctest case this is also the suite's own running cost.
- Compile time of a translation unit that includes `SushiRuntime.h`: no measurement. `graph.hpp` is 3,052 lines and `dynamic_graph.hpp` 1,136, and 41 test files include the umbrella.
- Completion latch and `run_async` round trip per step.

### Findings

Ranked by how much each protects the refactor or makes an optimisation claim checkable.

#### TEST-01 [high] [benchmark] The per-node number runs once, prints to stdout and has no stored baseline

Evidence: `tests/benchmark/performance/test_chain_dispatch_perf.cpp:104-109` calls `run_chain` once per case and prints `"  %8zu | %9s | %12.5f | %14.1f\n"`. `test_scheduler_dispatch_perf.cpp:49` `ROUNDS = 3` keeps the minimum and discards the rest. No file in the repository stores a result. The `performance` skill asks for five runs and a spread.

Change: add one helper in `tests/common` (a `Measurement` that takes a callable and a run count, returns median, min and max, and prints one line per case in a fixed `key=value` or JSON-lines form including build type, worker count and input size). Use it in `test_chain_dispatch_perf` and `test_root_fanout_perf` first. Store the output of one release run per machine under `docs/` or `tests/benchmark/baselines/` so a `perf` commit can quote before and after. Where the baseline lives is a data-ownership choice for the owner.

Effort: M.

#### TEST-02 [high] [benchmark] The two scheduler benchmarks time the allocator along with the path they name

Evidence: `test_scheduler_dispatch_perf.cpp:120-124`: the clock starts, then `sched.submit(new AtomicCounterTask(counter))` 500,000 times. `test_wide_fanout_perf.cpp:89-91` does the same per step. The first also ends on `sleep_for(1ms)` polling (line 67), which adds up to 1 ms to each timed round. The printed label says "submit+steal+dispatch".

Change: allocate the tasks before `t0` (or draw from a pool), end the timed region on a latch counted down by the last task, and print allocation as its own line if it is wanted. Without this a change that removes a scheduler lock and a change in the allocator are indistinguishable.

Effort: S.

#### TEST-03 [high] [determinism] The cancellation tests can lose the cancel and then hang the process

Evidence: `tests/functional/integration/test_cancellation.cpp:52-56`: `std::async(... g.run_until([] { return false; }))`, then `sleep_for(20ms)`, then `g.cancel()`. `include/SushiRuntime/api/graph/graph.hpp:2209-2210` resets cancellation at the start of a run ("A cancel that ended an earlier run must not abort this one"). If the async thread has not reached that reset within 20 ms, the cancel is erased and `run_until` never returns. The `ASSERT_EQ(fut.wait_for(5s), ready)` then fails and the `std::async` future's destructor blocks forever. No ctest `TIMEOUT` is set, so the case holds the lane for 1500 s. Line 63 also asserts `total_tasks_executed > 0`, which needs a compile and one full step inside the same 20 ms. The same pattern repeats at lines 79-82.

Change: gate on progress instead of time. Let the predicate set an atomic once it has been called, wait for that atomic in the test thread, then cancel. Add a `TIMEOUT` property (see TEST-12).

Effort: S.

#### TEST-04 [high] [determinism] Seventeen tests turn any constructor failure into a skip

Evidence: `test_distributed_e2e.cpp:186-194` wraps `std::make_unique<RuntimeContext>()` in `catch (const std::exception& e) { GTEST_SKIP() << "No SYCL device available: " ...` and repeats it at lines 302, 435, 556, 657, 785, 868, 978. Line 1033 catches around the whole scenario (`evolve_distributed_state`), so an exception thrown by the code under test reads as "cluster unavailable". The same shape is at `test_distributed_jepsen.cpp:266, 274, 294`, `test_distributed_offload_perf.cpp:194` and `test_graph_compile_perf.cpp:158`. A refactor that makes `RuntimeContext` throw on construction would turn these green-with-skips.

Change: one helper in `tests/common` that asks SYCL whether any device exists (`sycl::device::get_devices().empty()`) and skips on that answer only; construction then runs outside any catch. Line 1033 needs the probe moved in front of the scenario.

Effort: S.

#### TEST-05 [high] [coverage] Twenty-three integration and unit files drive internal types that the refactor will move

Evidence: these files build `Execution::RuntimeContext`, `Graph::TaskGraph` or `Scheduler::TaskScheduler` directly and never go through `Runtime`: `test_graph.cpp`, `test_sycl_execution.cpp`, `test_device_pipeline.cpp`, `test_heterogeneous_pipeline.cpp`, `test_scheduler_integration.cpp`, `test_async_execution.cpp`, `test_profiling_queue.cpp`, `test_workload_coordinator.cpp`, the three distributed files, regression, and 8 unit files. `RuntimeContext` is included by 25 test files. Internal accessors used: `.get_queue(` 12, `.get_numa_local_queue(` 7, `.get_execution_engine(` 7, `.live_usm_count(` 9, `scheduler.num_workers` (a public data member, `test_scheduler_integration.cpp:130`, `test_scheduler_stress.cpp:194`), `TaskGraph::add_task(meta, {in}, {out}, lambda(sycl::queue&, const std::vector<sycl::event>&))` in 6 files.

These tests break on a rename of `RuntimeContext` members or a change to the `add_task` signature while behaviour stays the same. The refactor has to decide, per file, whether the thing tested is a module's own contract (then the test belongs to that module and moves with it) or a behaviour the facade already pins (then it is a duplicate). `test_graph.cpp` (`SequentialUSMDependency`, `IndependentRootsExecution`, `FanOutFanInDiamond`) duplicates what `test_fluent_api` and `unit/test_dependency_tracker` hold.

Change: before the refactor, tag each of the 23 files as "module contract" or "facade duplicate" in the plan. Give `TaskScheduler` a `worker_count()` use in tests in place of the `num_workers` field. Not a rewrite; a triage list.

Effort: M.

#### TEST-06 [high] [coverage] Layout, widths and tile sizes are pinned by tests that an optimisation of the same paths must edit

Evidence:
- `unit/test_node_event_record.cpp:41` `static_assert(sizeof(Core::NodeEventRecord) == 40u, ...)`.
- `unit/test_late_count.cpp:39-46` calls `API::detail::plan_tiled_levels` and expects level counts at 255, 256, 257, 65536, 65537; `unit/test_fixed_order_reduce.cpp:103` `constexpr std::size_t TILE = 256;` copies the constant.
- `InternalNodeWidthsMatchTheDispatchTable` in `test_counting_sort.cpp` and `test_prefix_scan.cpp`, and all four tests in `test_reduce_dispatch_width.cpp`, assert how many internal nodes a public call expands to and their widths.
- `SharesOneNodeIndexAcrossStepsAndPasses` (two files) asserts the internal registration index.
- Three friend hooks in public headers serve these (already known, prior audit C26).

Some of these are contracts: the tile size fixes the floating-point combination order, so it is part of the determinism promise. Others (record size, node count per level) are implementation.

Change: split the two kinds. Export the tile size as one named public constant and have tests read it, so the determinism contract is stated once. Move the node-count and width assertions next to the module that owns the expansion, and accept that they change with it. Keep the bit-equality tests as the refactor's guard.

Effort: M.

#### TEST-07 [high] [coverage] The strongest refactor guards are bit-equality tests, and they cover five primitives on one device

Evidence: `test_chain_layer.cpp` (`ChainMatchesForcedSchedulerBitForBit`, `NonChainablePlanMatchesChainBitForBit`), `test_device_sized_primitives.cpp` (five `...ChainMatchesScheduler...` tests), `test_determinism.cpp` (`OutputIsByteEqualAcrossWorkerCounts`, `RepeatedConfigurationsAgreeByteForByte`, `ReplayDoesNotDriftAcrossSteps`). These compare whole outputs with `memcmp` and need no knowledge of internals. Nothing equivalent exists for `DynamicGraph` (no test compares a dynamic graph's output with the same work on a static `Graph`), for `add_host` mixed with kernels, for `run_async` against `run` beyond one test (`MatchesTheSynchronousResult`), or for `Extent`/`NdRange` launches.

Change: before touching the hot path, add layer-equivalence tests for those four shapes, in the same form as `test_chain_layer.cpp`. They are the cheapest insurance the suite can buy.

Effort: M.

#### TEST-08 [medium] [coverage] Half the error codes and several public entry points have no test

Evidence: `grep -r "Errc::<name>" tests` returns 0 for `compile_failed`, `allocation_failed`, `kernel_failed`, `device_lost`; `dangling_resource` has one hit. `State::advance()` (`api/data/state.hpp:178`, public) is called by no test. `version_matches()` is checked only in `tests/package/main.cpp`. `Cluster::liveness_timeout` and `task_deadline` are set in tests and never asserted to change behaviour. `RunHandle`'s move constructor is untested. `In`/`Out`/`InOut` have 5 hits in tests and 12 in `examples/`, and no example is registered as a test (`grep add_test examples/CMakeLists.txt` is empty), so the captured-argument form is held mostly by code that CI builds and never runs.

Change: one test per unreached `Errc` where the condition can be produced without hardware (`allocation_failed` through an oversized request, `kernel_failed` through a throwing kernel: `test_error_isolation` already throws, and asserts no code). Decide whether `State::advance()` is meant to be public; if not, the refactor removes it. Register `quickstart` and `vecadd_demo` as `integration`-labelled smoke tests.

Effort: M.

#### TEST-09 [medium] [tooling] No device label, so GPU-dependent tests are indistinguishable from the rest

Evidence: `tests/CMakeLists.txt:185-199` assigns exactly one label from the name prefix. Tests that need a GPU or specific hardware and skip without it: `unit/test_nvidia_topology.cpp:93,116,135,152,180,191,206`, `test_async_execution.cpp:82,166`, `test_cross_context_guard.cpp:41`, `test_interop_host_export.cpp:82`, `test_device_pipeline.cpp:74`. `test_nvidia_topology.cpp` is in `unit/` and needs CUDA. The `testing` skill asks for a device label. On the CI runners every one of these is a skip, so the GPU paths have no automated lane at all.

Change: a suite-name token such as `Unit_Gpu_...` or a second `gtest_discover_tests` call per device class with `TEST_FILTER "*Nvidia*"`, giving a `gpu` label, and `sr test --suite gpu`. The CMake comment at lines 177-184 explains why two labels on one call do not work in this CMake version; a separate discover call per label avoids that.

Effort: S.

#### TEST-10 [medium] [tooling] No way to run one module or the fast subset

Evidence: `project.py:76-87` offers category labels only. `--filter` is a regex over `Suite.Case`, and suite names do not carry the module: `Integration_ChainLayer`, `Unit_LateCount`, `Unit_RegistrationIndex` give no hint that they belong to execution, api and graph. `sushicore/cmake_driver.py:153` passes `--parallel` only when `jobs` is given, and `project.py:481-484` never gives it.

Change: either a module token in the suite name (`Unit_Scheduler_WorkStealingDeque`) or one `gtest_discover_tests` call per module with a module label, so `sr test --suite scheduler` exists. A refactor that reshuffles one module should be able to run that module's tests alone. Whether to pass `--parallel` is a separate decision: several tests assume they own all cores (`test_co_tenancy`, `test_determinism`, every benchmark).

Effort: M.

#### TEST-11 [medium] [shape] Zero fixtures; the same set-up is written out in 37 files

Evidence: `Runtime::create` appears 212 times in 42 files, `rt.buffer<` 351 times in 33 files, a local `RuntimeContext` 51 times in 23 files. `wait_for(const std::atomic<size_t>&, size_t, seconds)` is defined five times (`test_scheduler_dispatch_perf.cpp:59`, `test_steal_locality_perf.cpp:54`, `test_steal_numa_effect_perf.cpp:65`, `test_wide_fanout_perf.cpp:49`, `test_scheduler_integration.cpp:54`), four with a 1 ms sleep and one with `yield`. `launch_heavy` is defined three times, `pin_current_thread_to_core` three times, `first_difference` three times, `register_vec_add` twice, `register_vec_double` three times. `tests/common/test_barrier.hpp` is included by no test (`grep -rl test_barrier.hpp tests --include=*.cpp` is empty), although the README presents it as the shared barrier.

Change: three bricks in `tests/common`: a `wait_until(predicate, budget)`; a `RuntimeFixture` that owns a `Runtime` with the rebalancer off and offers `filled_buffer<T>(n, value)`; a byte-comparison helper (`first_difference`). Either use `Barrier` or delete it; deleting needs the owner's approval.

Effort: M.

#### TEST-12 [medium] [tooling] No per-test timeout, and one process per test case

Evidence: `grep -n "TIMEOUT" tests/CMakeLists.txt` is empty. `DISCOVERY_MODE PRE_TEST` registers each of the 388 cases as its own ctest test, so each case starts the binary, loads the SYCL runtime and discovers devices. No number exists for what that costs; a refactor aimed at "easier to operate" should know it.

Change: `PROPERTIES TIMEOUT 120` on the functional discover calls and a larger value on the benchmark ones. Measure the wall time of `sr test` against one direct run of `sr_functional_tests` once, and record both.

Effort: S.

#### TEST-13 [medium] [benchmark] The nightly benchmark job produces nothing anyone can compare

Evidence: `ci.yml:154-180` builds on `ubuntu-latest`, runs `ctest -L "stress|performance"` and uploads no artifact. It omits `-DSR_ENABLE_DISTRIBUTED=ON`, so `test_distributed_offload_perf.cpp` is never compiled there. Shared runners have no GPU and one NUMA node, so `test_async_overlap_perf`, both steal benchmarks and the transfer-overlap test skip or report meaningless figures. The comment at line 178 mentions `sanity_gate()`, a function that does not exist in `tests/` (grep finds no definition).

Change: treat the nightly job as a "benchmarks still run" check and say so; turn distributed on; upload the output once TEST-01 gives it a parseable form. Real numbers come from the owner's machines through `sr test --suite performance`.

Effort: S.

#### TEST-14 [medium] [determinism] Functional tests that depend on sleeps and on the scheduler winning a race

Already known in outline (prior audit C21 counted sleeps per file). New detail on what each one risks:

- `test_workload_coordinator.cpp:126-133`: expects 3 heartbeats within 500 ms, then sleeps 50 ms and expects no further evaluation.
- `test_scheduler_integration.cpp:101-140` and `test_scheduler_stress.cpp:154-203`: the same priority-bypass test written twice. Each task sleeps 1 ms (`test_helpers.hpp:120`) and the high-priority task must appear within the first `2 * num_workers` entries.
- `test_graph.cpp:139-147`: `fan_out_success` is a plain `bool` written from two worker threads; a TSan run that hits the failure path reports a race on the test's own variable.
- `test_distributed_e2e.cpp:708` and `test_distributed_jepsen.cpp:180-183, 237`: fixed 25 to 60 ms sleeps stand in for "the message has been delivered"; line 237's comment is `// drain late delivery`.

Change: keep one priority-bypass test, in functional. Make `fan_out_success` atomic. Replace the delivery sleeps with a counter the in-process fabric already could expose.

Effort: M.

#### TEST-15 [medium] [coverage] The suppression pattern is wider than the leak it describes

Evidence: `tests/lsan_suppressions.txt:17` is `leak:libintelocl`. The comment (lines 7-13) describes a fixed 1.4 KB start-up leak with no SushiRuntime frame in the stack. LeakSanitizer matches a `leak:` pattern against every frame of the allocation stack, so any leak whose stack passes through the OpenCL CPU runtime is suppressed too, including one where SushiRuntime calls into it and loses the result. I did not run an ASan build, so whether a real USM leak takes that route is unverified. The suite does have its own guard for USM (`live_usm_count()` in 9 places, `unit/test_topology.cpp` `LiveUsmCountFollowsAllocateFreeAndForget`).

Change: narrow the pattern to the function names of the one-time initialisation, read from one ASan report, and add an ASan job to CI. Today ASan runs only when someone types `sr build --type asan` locally.

Effort: S for the pattern, M for the CI lane.

#### TEST-16 [medium] [benchmark] The one allocation gate is failing on Linux and blind to worker threads

Evidence: `docs/reference/KNOWN_ISSUES.md:12` records `ProfiledReuseTicksDoNotAllocatePerTick` failing on CI since 2026-10-05 (already known). Beyond that: `test_steady_state_allocation.cpp:67-75` counts only when `g_counting_this_thread` is set, a `thread_local`, so allocations made on scheduler workers during a tick are never counted. The assertions are ratios (`scheduler_long < 4 * scheduler_short`, lines 258 and 362), so a path that allocates a constant 3 times per node per tick passes. `test_recompose_churn_perf.cpp:60-78` carries a second, differently built `operator new` replacement with a process-wide gate.

Change: one allocation counter in `tests/common` with both modes (calling thread, whole process), used by both files. Add a reported figure "allocations per node per tick, all threads" for the scheduler layer; that is the number an optimisation of the per-node path has to move.

Effort: M.

#### TEST-17 [medium] [shape] `test_distributed_e2e.cpp` repeats a 50-line cluster set-up nine times

Evidence: the block at lines 185-240 (two contexts in a try/catch, USM allocation by raw `sycl::malloc_shared`, fabric, two transports, kernel registry, `WorkerAgent`, `enable_distributed_master`, a 300 x 10 ms poll for `distributed_supports`) recurs at 300-370, 433-475, 554-600, 655-720, 783-830, 866-910 and 976-1000. The file already has a second, smaller set-up for the fluent path at lines 130-150. The raw `malloc_shared` calls leak on an early `ASSERT` failure even though `Test::UsmShared` exists in `tests/common/test_helpers.hpp:141` for that purpose. 1,048 lines for 9 tests.

Change: a `ClusterFixture` (master context, N workers, transport kind as a parameter, `wait_until_supported(op)`), with buffers held by `UsmShared`. Split the file by subject: round trip, reliability (fencing, deadline, worker death), TCP, fluent. Estimated result: under 450 lines in four files.

Effort: M.

#### TEST-18 [medium] [coverage] What the "math" files pin

The runtime owns fixed-order reduce, segmented reduce, prefix scan and counting sort as public `Graph` methods (`graph.hpp:740, 982, 1130, 1376`), so tests of them are tests of public API. What each file holds:

- `unit/test_fixed_order_reduce.cpp` (460 lines, 11 tests): the orchestration contract is real here. `SumMatchesTheFixedOrderReferenceExactly` and `ResultIsStableAcrossSchedules` assert that the combination order depends on layout and not on scheduling, with an input (`1e8f` then ones) chosen so order changes the bits. `LateBoundChangingCountKeepsOneCompile` pins replay. `MinimumAndMaximum` and `EdgeCounts` are arithmetic checks.
- `integration/test_counting_sort.cpp` (691 lines, 17 tests): 8 tests check the algorithm against `std::stable_sort` (stability, ties across digits, one element, zero elements, 20-bit keys, large random case). 9 tests check orchestration: one compile across changing counts, digits above the live key range not running, per-run keys, node index sharing, node widths, over-capacity failing the run, regions.
- `integration/test_prefix_scan.cpp` (312 lines, 8 tests): 3 algorithm, 5 orchestration, same split.
- `integration/test_reduce_dispatch_width.cpp`, `unit/test_late_count.cpp`: planner arithmetic and node widths, all internal.

So roughly 14 tests verify a sort or a scan as an algorithm. They are legitimate while the methods stay in the runtime. If the owner's rule "the runtime knows no math" is applied to these four methods, those 14 tests move with them to the domain layer and the orchestration half (late count, one compile, device-sized dispatch, fail on over-capacity) stays, rewritten against a trivial kernel. That is a module-boundary decision for the owner.

`test_fixed_order_reduce.cpp` and `test_late_count.cpp` sit in `unit/` and build a full `Runtime`; by the folder README's own definition they are integration tests.

Effort: S to relabel; L if the primitives move.

#### TEST-19 [low] [shape] Sibling naming drifts

Evidence: suite names: 21 end in `Test` (`Unit_AffinityTest`), 7 in `Bench` (`Performance_AsyncOverlapBench`), 55 in neither (`Unit_Allocation`, `Performance_ChainDispatch`). Benchmark files are named `test_*_perf.cpp`; the `performance` skill names them `benchmark_<subject>`. Test constants are bare upper case 150 times against `K_` once. 15 files include helpers as `"../../common/test_helpers.hpp"` although `tests/CMakeLists.txt:73` puts `common/` on the include path. Eight unit files build a `Runtime` or `RuntimeContext`. Test names inside one file mix styles: `Scheduler_AbruptStopDrains` next to `AThreadOutlivingTheRuntimeTearsDownCleanly` in `test_regression.cpp`.

Change: pick one suffix rule (none), rename in one mechanical commit, switch the includes to `"test_helpers.hpp"`. Renaming suites changes ctest names, so do it before any baseline or filter is stored.

Effort: S.

#### TEST-20 [low] [benchmark] Small defects in benchmark output

Evidence: `test_scheduler_dispatch_perf.cpp:141-142` prints `sched_per_task_ns` twice in one line (`"%8.2f ns/task  (wall %.0f ns amortized)"`). `test_dynamic_graph_perf.cpp:82` records `static_cast<int>(initial_ms)`, losing everything below a millisecond. `test_chain_dispatch_perf.cpp:64` derives its figure from `report.total_duration_ms`, the runtime's own clock; a change to what the report includes moves the benchmark without moving the cost.

Change: fix the print, record a double, and time the chain benchmark with the test's own `steady_clock` around `g.run(steps)` beside the reported figure.

Effort: S.

#### TEST-21 [low] [coverage] Regression suite holds three tests for a codebase with a long defect history

Already known (prior audit C20: one file, not named per defect). One addition: defects recorded in `KNOWN_ISSUES.md` that are testable today have no failing or quarantined test: the buffer moved after `Graph::add` (line 11), and the unlocked `peers` vector in `tcp_transport.cpp` (line 17), which a TSan test with two connecting workers would catch.

Effort: S each.

#### Every `GTEST_SKIP` (38) and `DISABLED_` (0)

| File:line | Condition |
| --- | --- |
| `performance/test_async_overlap_perf.cpp:153` | no CPU+GPU pair |
| `performance/test_async_overlap_perf.cpp:243` | WSL2 and overlap between 0.75 and 0.95 of serial |
| `performance/test_distributed_offload_perf.cpp:194` | any exception constructing contexts |
| `performance/test_graph_compile_perf.cpp:158` | any exception constructing the context |
| `performance/test_steal_locality_perf.cpp:91` | single NUMA node |
| `performance/test_steal_numa_effect_perf.cpp:150` | fewer than 2 workers |
| `performance/test_steal_numa_effect_perf.cpp:179` | single physical NUMA node (after the measurements, before the locality assertion) |
| `integration/test_async_execution.cpp:82` | no CPU+GPU pair |
| `integration/test_async_execution.cpp:166` | no GPU |
| `integration/test_co_tenancy.cpp:84` | fewer than 4 logical cores |
| `integration/test_cross_context_guard.cpp:41` | fewer than two device contexts |
| `integration/test_determinism.cpp:145` | built with `SR_DETERMINISTIC_FP=OFF` |
| `integration/test_determinism.cpp:186` | single-core pool |
| `integration/test_device_pipeline.cpp:74` | NUMA node 0 exposes more than one device queue |
| `integration/test_distributed_e2e.cpp:193, 302, 435, 556, 657, 785, 868, 978` | any exception constructing contexts |
| `integration/test_distributed_e2e.cpp:1033` | any exception from the whole scenario |
| `integration/test_distributed_jepsen.cpp:266, 274` | scenario returned a string starting `SKIP:` |
| `integration/test_distributed_jepsen.cpp:294` | any exception constructing contexts |
| `integration/test_dynamic_graph.cpp:375` | `hardware_concurrency() < 2` |
| `integration/test_interop_host_export.cpp:82` | device 0 can import external memory (the test covers the refusal only) |
| `integration/test_steady_state_allocation.cpp:58` | ASan or TSan build |
| `unit/test_numa_steal.cpp:81, 120` | fewer than 2 workers |
| `unit/test_nvidia_topology.cpp:93, 116, 152, 191` | no CUDA GPU |
| `unit/test_nvidia_topology.cpp:135` | hwloc cannot map the GPU to a NUMA node (WSL) |
| `unit/test_nvidia_topology.cpp:180, 206` | no CUDA-backed queue in the context |

`test_device_pipeline.cpp:74` means the one diamond test of all-kernel nodes on the internal `TaskGraph` does not run on a machine with a CPU device and a GPU on node 0, which is the owner's development machine.

### Not checked

- No build, test, benchmark or `sr` command was run. Every claim about flakiness is from reading code; none was observed failing.
- Read in full: `tests/CMakeLists.txt`, `tests/common/*`, `tests/lsan_suppressions.txt`, `tests/package/*`, `tests/benchmark/README.md`, `test_chain_dispatch_perf`, `test_scheduler_dispatch_perf`, `test_wide_fanout_perf`, `test_root_fanout_perf`, `test_cancellation`, `.github/workflows/ci.yml`, `cmake/Sanitizers.cmake`.
- Read in part (heads, set-up, assertions by grep): the other 8 performance files, both stress files, `test_distributed_e2e`, `test_distributed_jepsen`, `test_counting_sort`, `test_fixed_order_reduce`, `test_prefix_scan`, `test_late_count`, `test_steady_state_allocation`, `test_graph`, `test_scheduler_integration`, `test_workload_coordinator`.
- Known only by test names and grep counts: the remaining 30 integration files and 20 unit files. The per-test split of "algorithm" against "orchestration" in TEST-18 is from test names and file headers, not from reading every body.
- `tests/functional/integration/README.md` (1,614 lines) and `unit/README.md` (462 lines): headings counted (39 and 20 sections), prose not read. `tests/functional/README.md`, `regression/README.md` and `common/README.md` were read.
- The coverage table is include- and symbol-based. It shows that a test names a symbol, not that the test asserts anything about it. Overload-level coverage of the 18 `Graph::add` forms was not traced.
- `docs/CONTRIBUTING.md`, `docs/architecture/ARCHITECTURE.md` and `docs/design/REMAINING_WORK.md` were grepped for test, benchmark and sanitizer items and not read whole. `KNOWN_ISSUES.md` was read whole. The prior audit report was grepped; C20, C21 and C26 were read in full.
- `cli/tests/` (the Python tests of `sr`) is outside this scope and was not opened.
- Whether `leak:libintelocl` hides a real SushiRuntime leak (TEST-15) needs an ASan run to settle.
- The cost of one process per test case (TEST-12) is not measured.

### Dispatch checklist

1. SOLID: no code written. The proposed helpers (`Measurement`, `wait_until`, `RuntimeFixture`, `ClusterFixture`, one allocation counter) are each one responsibility in `tests/common`, and TEST-11, TEST-16 and TEST-17 are about making siblings the same shape.
2. Humanizer: skill loaded and applied to this report (English, generic register).
3. No `sr`, `se`, `cmake`, `ninja` or `ctest` process was started; no build configuration file was written or edited.
4. No file was written in the repository. `git status --short` at the end shows the same two modified files as at the start (`docs/reference/CHANGELOG.md`, `include/SushiRuntime/api/runtime/runtime.hpp`), neither touched by me.
5. Done and not done are listed above under "Not checked".
6. No source written; nothing to syntax-check.
7. Scratchpad: I wrote nothing to it. `du -sh` on the scratchpad reports 9.0K, which was there from other work in this session; I deleted nothing because I created nothing.
