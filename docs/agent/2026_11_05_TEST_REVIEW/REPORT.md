# Test review: report

**Status:** Open — every test has a verdict; the plan's jobs are next

Every test in the repository has a verdict from `skills/testing/REVIEWING.md` of SushiSkills, reviewed
on 2026-11-05 as part of J15 (`docs/agent/2026_11_03_INDUSTRIAL_TESTING/` in SushiSkills). The review read
the code and ran nothing. Each record names the files it read. The tests of the shared checkers under
`tools/` are reviewed once, in SushiSkills; this report covers this repository's own tests and its own
checkers' tests.

## What the review found

- **A runtime or a device made in the test body.** Medium tests build their own `Runtime`,
  `sycl::queue` or `DeviceQueueSet`; 46 of 76 api bodies call `Runtime::create`. SushiRuntime has no
  `testing::Environment` fixture base, so the one-device-per-process rule has no home here.
- **Stand-ins without adapters** for `TopologyProvider`, `OffloadCoordinator`, `OffloadSink`,
  `OffloadPayload`, `OffloadPolicy`, `TelemetryProvider`, `TaskMigrator`, `RebalanceStrategy`, `Task`,
  `Transport`, `AllocationOwner`, `RunObserver`, `USMReleaseObserver`, `FeedAttachment` and
  `CapabilityCache::Probe`. `InProcessTransport`, the in-memory `Transport`, lives in
  `modules/extension/distributed`, not beside the port in `modules/extension/transport`.
  `RecordingObserver` and `LogCapture` live in `test_runner`, not in the observer's or the logger's
  module.
- **Labels wrong both ways:** 52 pure-logic tests filed as medium, real hwloc reads filed as small.
- **Loops** over flavours, devices, threads, rounds and hwloc nodes; sleeps, yields and wall-clock waits.
- **`Performance_` and `Stress_` suites** in the test tree are benchmarks; `benchmark_dynamic_graph.cpp`
  replaces the process-wide `operator new`.
- About 20 surface tests and 13 others reach `Detail::` or `Internal::` names.

## Defects the review found

- Tests that pass on an empty capture: `AFloatingPoolLogsNoCoreMapLine`,
  `ARunNobodyCancelledLogsNoSkipLine`. Tests that depend on the machine's core count:
  `LogsAWorkerWhoseCoreHasNoNumaNode`, `FlatTopologyKeepsAllVictims`. `EachDeviceIsProbedOnce` skips on
  a one-device machine.
- `test_node_description.cpp`: the `@brief` says device 0, the assertion says none.
- `test_before_step_hooks.cpp` is `Medium_` without a device; `AFileDescriptorAboveIntIsRefusedWithItsValue`
  compiles only off Windows.
- Pages that say something the code does not: `tests/engine/task/README.md` lines 19 to 21 say
  `place_on` throws `InvalidGraph`, but `node.hpp` uses `SR_VERIFY`, which aborts. Four test READMEs
  (`tests/engine/scheduler`, `tests/engine/task` lines 123 and 147, `tests/platform/memory`,
  `tests/surface/memory_exchange` lines 12 to 14, `tests/surface/context`) hold pasted test source or
  garbled rows; the memory-exchange README misses `test_memory_exchange_log_category.cpp`.
- `tests/README.md` defines sizes as 60 s and 300 s and allows a child process and a thread.
- `K_ALL_KEYS` in `profile_keys.hpp` has no reader outside its test.
- A stray doc comment at `test_offload_policy.cpp` line 135.
- `tools/testing/check_test_additions.py` and its test are a leftover: SushiSkills removed the checker in
  phase B, and the copier never removes a file upstream dropped. Both are deleted.
- `tools/tests/test_test_budget_settings.py` skips here, since the repository has no `skills/` folder, so
  its promise is checked nowhere; it also defines `read_budget` and `differences` in the test file.

## Decisions

The owner delegated these decisions on 2026-11-05. Each ruling below is final for the plan; its reason follows it.

1. `InProcessTransport` moves beside the `Transport` port, into `modules/extension/transport`. A port's in-memory adapter belongs with the port, and `distributed` keeps its dependency on the port only.
2. A name is public when code outside its module calls it, and internal otherwise. The job decides `Allocation`, `translate_exceptions`, `require_same_release` and `is_log_enabled` by a search for callers. The bridge promises under `Detail::` are tested through the public bridge, at medium.
3. `register_profiler_parts` takes the registration interface it uses in place of a concrete `Runtime`. Every seam is added as a parameter or overload whose default keeps today's behaviour, so existing callers compile unchanged and the change is a minor version. A value passed in is preferred to a reset of process-wide state.
4. `K_ALL_KEYS` is deleted, with its test. Nothing reads it.
5. Death tests are medium: each one starts a child process. The testing skill records this.
6. The size timeouts follow the testing skill: 1 s, 10 s and 120 s. The skill is the one home of these numbers; a measurement on the owner's machine changes the skill, not a repository.

## Dispatches

dispatch: J15 review, 16 chunks — haiku high, 1 round each, passed, rung 1

## Totals

| Tests | Keep | Rewrite | Resize | Split | Merge | Delete | Characterize | Add |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 1667 | 266 | 737 | 242 | 195 | 92 | 135 | 0 | 85 |

Kept as they stand: 266 of 1667 (15%).

## Verdicts by question

| Verdict | Question | What it means | Tests |
| --- | --- | --- | --- |
| rewrite | 1 | a forbidden kind | 577 |
| resize | 8 | wrong size | 242 |
| split | 3 | no single promise | 195 |
| delete | 1 | a forbidden kind | 111 |
| merge | 4 | a duplicate | 92 |
| rewrite | 8 | wrong size | 69 |
| rewrite | 7 | a long-test sign | 46 |
| rewrite | 5 | tied to the inside | 28 |
| delete | 4 | a duplicate | 15 |
| rewrite | 6 | oracle is the code | 8 |
| rewrite | 2 | cannot fail | 6 |
| delete | 3 | no single promise | 5 |
| delete | 2 | cannot fail | 4 |
| rewrite | 3 | no single promise | 3 |

## Modules

| Module | Files | Tests | Keep | Rewrite | Resize | Split | Merge | Delete | Characterize |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| `surface/api` | 64 | 462 | 20 | 248 | 73 | 80 | 15 | 26 | 0 |
| `extension` | 49 | 294 | 17 | 142 | 66 | 43 | 13 | 13 | 0 |
| `engine` | 52 | 224 | 48 | 106 | 17 | 25 | 7 | 21 | 0 |
| `foundation` | 54 | 200 | 74 | 44 | 27 | 22 | 5 | 28 | 0 |
| `surface/profiling` | 23 | 138 | 29 | 66 | 10 | 9 | 11 | 13 | 0 |
| `platform` | 27 | 130 | 22 | 49 | 18 | 12 | 18 | 11 | 0 |
| `cli` | 20 | 147 | 54 | 60 | 20 | 1 | 5 | 7 | 0 |
| `tools` | 5 | 36 | 0 | 2 | 10 | 1 | 18 | 5 | 0 |
| `surface/interop` | 4 | 11 | 2 | 3 | 0 | 1 | 0 | 5 | 0 |
| `surface/memory_exchange` | 3 | 8 | 0 | 5 | 1 | 0 | 0 | 2 | 0 |
| `surface/context` | 3 | 5 | 0 | 3 | 0 | 0 | 0 | 2 | 0 |
| `surface/context_access` | 1 | 5 | 0 | 5 | 0 | 0 | 0 | 0 | 0 |
| `surface/engine_handles` | 2 | 4 | 0 | 2 | 0 | 0 | 0 | 2 | 0 |
| `applications` | 2 | 3 | 0 | 2 | 0 | 1 | 0 | 0 | 0 |

## Records

One line per test: `file::test | verdict | question | reason | files read`.

### `cli/tests/test_aliases.py`

```
test_the_table_holds_each_renamed_command | resize | 8 | aliases[1]: pure table check, no file, process or clock; size should be small | cli/tests/test_aliases.py; cli/sushiruntime/aliases.py (grep)
test_every_row_is_removed_after_the_shared_date | delete | 1 | forbidden: test of a constant (the shared removal date is data, not behaviour); the expiry promise is aliases[2] | cli/tests/test_aliases.py; cli/sushiruntime/aliases.py (grep)
test_no_row_has_expired_today | resize | 8 | aliases[2]: reads the real clock (date.today()); pass a fixed date to ALIASES.expired | cli/tests/test_aliases.py; cli/sushiruntime/aliases.py (grep)
test_doxygen_still_runs_docs | rewrite | 1 | forbidden: stub of owned project.docs (monkeypatch); the alias promise aliases[4] has no other copy after cli/tests/test_cli.py is split; rewrite needs an injected process runner (no in-memory runner exists) | cli/tests/test_aliases.py; cli/sushiruntime/services/project.py (grep); cli/sushiruntime/cli.py (grep)
test_docker_still_runs_container[build] | rewrite | 1 | forbidden: stub of owned docker.build (lambda recorder); aliases[3] | cli/tests/test_aliases.py; cli/sushiruntime/services/docker.py (grep)
test_docker_still_runs_container[run] | rewrite | 1 | forbidden: stub of owned docker.run (lambda recorder); aliases[3] | cli/tests/test_aliases.py; cli/sushiruntime/services/docker.py (grep)
test_the_old_words_are_absent_from_the_help_screen | keep | 9 | aliases[5] | cli/tests/test_aliases.py
```

### `cli/tests/test_build_jobs.py`

```
test_build_passes_jobs_to_the_compile_call | rewrite | 1 | forbidden: stubs owned load_config, find_project_root, _resolve_compiler, load_build_env, _stamp_differs, console (SimpleNamespace), and a recording driver; build_jobs[1] | cli/tests/test_build_jobs.py; cli/sushiruntime/services/project.py (grep); cli/sushiruntime/console.py
test_build_uses_the_hardware_threads_when_no_jobs_are_given | rewrite | 1 | forbidden: same stubs as above; build_jobs[2]; kept here as the copy in the jobs unit's own file (see test_build_trees.py::test_the_compile_gets_the_hardware_threads_without_jobs) | cli/tests/test_build_jobs.py; cli/tests/test_build_trees.py; cli/sushiruntime/services/project.py (grep)
test_the_build_command_forwards_the_flag[build --jobs 4 / -j 12 / build] | rewrite | 1 | forbidden: stub of owned project.build (lambda recorder); build_jobs[3] | cli/tests/test_build_jobs.py; cli/sushiruntime/services/project.py (grep)
test_the_build_command_rejects_zero_jobs | rewrite | 1 | forbidden: stub of owned project.build used as spy; the rejection needs no stub, assert exit 2 and no tree created; build_jobs[4] | cli/tests/test_build_jobs.py
```

### `cli/tests/test_build_trees.py`

```
test_the_default_build_type_is_debug | delete | 1 | forbidden: test of constants (K_DEFAULT_TYPE, K_BENCH_TYPE); defaults are promised by build_trees[3] and build_trees[4] | cli/tests/test_build_trees.py; cli/sushiruntime/services/project.py (grep)
test_each_build_type_has_its_own_tree[5 cases] | resize | 8 | build_trees[1]: build_tree is pure; tmp_path is only a prefix; size small | cli/tests/test_build_trees.py; cli/sushiruntime/services/project.py (grep)
test_an_explicit_build_dir_overrides_the_tree_of_the_type | resize | 8 | build_trees[2]: pure path logic; size small | cli/tests/test_build_trees.py; cli/sushiruntime/services/project.py (grep)
test_the_build_command_defaults_to_debug[build / build --type release] | rewrite | 1 | forbidden: stub of owned project.build; build_trees[3] | cli/tests/test_build_trees.py; cli/sushiruntime/services/project.py (grep)
test_the_tree_commands_default_to_debug_and_bench_to_release | rewrite | 1 | forbidden: stubs of owned project.test, project.run, project.bench and packaging.package; four commands in one body (split candidate on rewrite); build_trees[4] | cli/tests/test_build_trees.py; cli/sushiruntime/services/packaging.py (grep)
test_asan_selects_the_sanitizer_tree_in_the_tree_commands | rewrite | 1 | forbidden: stub of owned project.test; build_trees[5] | cli/tests/test_build_trees.py
test_a_build_configures_and_compiles_in_the_tree_of_its_type | rewrite | 1 | forbidden: fixture stubs of owned load_config, load_build_env, _resolve_compiler, configure_arguments, library_packages record functions, and a recording driver; build_trees[6] | cli/tests/test_build_trees.py; cli/sushiruntime/services/project.py (grep)
test_the_packages_get_the_compiler_the_build_path_finds | rewrite | 1 | forbidden: stubs owned library_packages.resolve and load_build_env; build_trees[12]; the logic is _on_build_path, a value function that can be tested small | cli/tests/test_build_trees.py; cli/sushiruntime/services/project.py (grep)
test_a_tree_of_another_build_type_is_refused | rewrite | 1 | forbidden: recording console (_Console) and recording driver replace owned console and driver; build_trees[8]; in-memory console port is sushicore PlainRenderer over io.StringIO (../sushicore/source/sushicore/renderer.py) | cli/tests/test_build_trees.py; ../sushicore/source/sushicore/renderer.py (grep)
test_a_stopped_gate_refuses_the_build | rewrite | 1 | forbidden: same recording console and driver; build_trees[9] | cli/tests/test_build_trees.py
test_the_configure_carries_both_launcher_entries | rewrite | 1 | forbidden: fixture stubs; build_trees[10]; _launcher_arguments is the value seam | cli/tests/test_build_trees.py; cli/sushiruntime/services/project.py (grep)
test_no_build_type_but_tsan_asks_for_thread_sanitizer[4 cases] | delete | 1 | forbidden: fixture stubs; promise is tested without stubs by test_thread_sanitizer_applies.py::test_no_other_linux_type_implies_tsan | cli/tests/test_build_trees.py; cli/tests/test_thread_sanitizer_applies.py
test_the_tsan_type_asks_for_thread_sanitizer_in_its_own_tree | rewrite | 1 | forbidden: fixture stubs; build_trees[7] | cli/tests/test_build_trees.py
test_the_compile_gets_the_hardware_threads_without_jobs | delete | 4 | duplicate of build_jobs[2] in cli/tests/test_build_jobs.py::test_build_uses_the_hardware_threads_when_no_jobs_are_given, the jobs unit's own file; also forbidden stubs (q1) | cli/tests/test_build_trees.py; cli/tests/test_build_jobs.py
test_the_report_is_printed_and_written_after_the_compile | rewrite | 1 | forbidden: stubs owned build_report.ninja_log_offset and build_report.of_build, plus fixture stubs; build_trees[11] | cli/tests/test_build_trees.py
test_no_report_is_read_without_the_flag | rewrite | 1 | forbidden: stub of build_report.of_build that fails the test when called; build_trees[11] | cli/tests/test_build_trees.py
```

### `cli/tests/test_cli.py`

```
test_docs_runs_the_api_build_and_bundle_is_its_subcommand | split | 3 | two promises: the doxygen part repeats aliases[4] (delete at q4, see test_aliases.py::test_doxygen_still_runs_docs), and the docs bundle usage error is cli[1]; the stubs of project.docs and cli.find_project_root (q1) go with the split | cli/tests/test_cli.py; cli/tests/test_aliases.py; cli/sushiruntime/cli.py (grep)
test_main_reports_a_malformed_config_as_one_line_and_exit_one | resize | 8 | cli[2]: reaches process-wide console._lazy and sys.argv, and the real cwd; resize with a seam: main takes argv and a console | cli/tests/test_cli.py; cli/sushiruntime/console.py; cli/sushiruntime/config.py (grep)
test_main_exits_zero_after_a_command_that_succeeds | resize | 8 | cli[3]: reaches process-wide sys.argv; resize with main(argv) | cli/tests/test_cli.py
test_version_names_the_distribution | rewrite | 6 | cli[4]: the oracle is sushicore.root_options.version_line, the same function the CLI prints through, so it is the code under test; name an independent oracle (the distribution name and version from cli/pyproject.toml) | cli/tests/test_cli.py
test_describe_lists_every_visible_command | keep | 9 | cli[5] | cli/tests/test_cli.py
test_describe_leaves_the_hidden_spellings_out | delete | 4 | cli[6] is already checked by cli/tests/test_cli.py::test_describe_lists_every_visible_command, whose exact list excludes doxygen and docker | cli/tests/test_cli.py
test_config_runs_the_diagnostics_object | rewrite | 1 | forbidden: stub of owned diag.DIAGNOSTICS class method config_show; cli[7] | cli/tests/test_cli.py; cli/sushiruntime/services/diag.py (grep)
test_env_keeps_its_own_asan_flag[--all / -a] | rewrite | 1 | forbidden: stub of owned diag.env_dump; cli[8] | cli/tests/test_cli.py
test_run_forwards_a_known_role | rewrite | 1 | forbidden: stub of owned project.run; cli[9] | cli/tests/test_cli.py
test_run_rejects_an_unknown_role | rewrite | 1 | forbidden: stub of owned project.run used as a spy; the exit 2 needs no stub; cli[10] | cli/tests/test_cli.py
test_docs_bundle_writes_the_archive_of_the_checkout_it_runs_in | rewrite | 1 | forbidden: cli.console replaced by a SimpleNamespace recorder (owned sink); cli[11]; the real git and filesystem are a valid boundary; in-memory console is sushicore PlainRenderer over io.StringIO | cli/tests/test_cli.py; cli/sushiruntime/console.py
test_the_api_source_reads_the_xml_doxygen_writes_under_the_site_folder | rewrite | 1 | forbidden: stub of owned cli.find_project_root; cli[12]; _api_source takes the root as an argument would remove the stub (cli.py:289 calls find_project_root() directly) | cli/tests/test_cli.py; cli/sushiruntime/cli.py (grep)
```

### `cli/tests/test_config.py`

```
test_find_project_root_raises_outside_checkout | keep | 9 | config[1]; boundary (filesystem walk) | cli/tests/test_config.py; cli/sushiruntime/config.py (grep)
test_find_project_root_walks_up_to_marker | keep | 9 | config[2] | cli/tests/test_config.py; cli/sushiruntime/config.py (grep)
test_find_project_root_passes_a_nested_cmake_project | keep | 9 | config[3] | cli/tests/test_config.py; cli/sushiruntime/config.py (grep)
test_a_cmake_project_without_the_module_manifest_is_not_a_checkout | keep | 9 | config[4] | cli/tests/test_config.py; cli/sushiruntime/config.py (grep)
test_config_dir_prefers_cli | keep | 9 | config[5] | cli/tests/test_config.py; cli/sushiruntime/config.py (grep)
test_config_dir_defaults_to_cli_when_absent | keep | 9 | config[6] | cli/tests/test_config.py; cli/sushiruntime/config.py (grep)
test_toolchain_fallback_on_unknown | rewrite | 1 | control flow in the test body (if) and the test runs its own copy of the guard; load_config (cli/sushiruntime/config.py lines 169-171) is never called; config[7] needs a loader seam, since stubbing _MODULE.load would be a stub of an owned function | cli/tests/test_config.py; cli/sushiruntime/config.py
test_llvm_root_is_found_under_the_provision_root | keep | 9 | config[8]; boundary (files under a temporary provision root) | cli/tests/test_config.py; cli/sushiruntime/config.py (grep)
test_a_pinned_llvm_root_wins | resize | 8 | config[9]: the environment is set but unused; the promise is pure; drop the environment and size small | cli/tests/test_config.py; cli/sushiruntime/config.py (grep)
```

### `cli/tests/test_console_lazy.py`

```
test_import_does_not_build_console | resize | 8 | console_lazy[1]: reloads the process-wide sushiruntime.console module; seam is LazyConsole(config_dir) from ../sushicore/source/sushicore/cli_console.py, whose built flag is the observable | cli/tests/test_console_lazy.py; cli/sushiruntime/console.py; ../sushicore/source/sushicore/cli_console.py
test_import_outside_project_does_not_raise | resize | 8 | console_lazy[2]: reloads the process-wide module and changes the process cwd; seam LazyConsole(config_dir raising SystemExit) | cli/tests/test_console_lazy.py; cli/sushiruntime/console.py; ../sushicore/source/sushicore/cli_console.py
test_output_helper_falls_back_outside_project | resize | 8 | console_lazy[3]: reloads the module and changes cwd; seam LazyConsole(config_dir raising SystemExit).sources() == [] and built after info | cli/tests/test_console_lazy.py; ../sushicore/source/sushicore/cli_console.py
test_sources_inside_project_are_this_repos_config | resize | 8 | console_lazy[4]: depends on the unpinned process cwd and the reloaded module; seam LazyConsole(lambda: cli_dir).sources() | cli/tests/test_console_lazy.py; cli/sushiruntime/console.py
test_console_attribute_resolves | resize | 8 | console_lazy[5]: reloads the module and changes cwd; seam LazyConsole.attribute("console") | cli/tests/test_console_lazy.py; ../sushicore/source/sushicore/cli_console.py
test_unknown_attribute_raises_attribute_error | resize | 8 | console_lazy[6]: reloads the process-wide module; seam LazyConsole.attribute("does_not_exist") raises AttributeError (cli_console.py) | cli/tests/test_console_lazy.py; ../sushicore/source/sushicore/cli_console.py
```

### `cli/tests/test_docker.py`

```
test_build_with_no_flags | rewrite | 1 | forbidden: stub of owned config.find_project_root (imported into docker.py); the Runner.run stub is an out-of-process boundary and is allowed; docker[1]; root becomes an argument | cli/tests/test_docker.py; cli/sushiruntime/services/docker.py (grep)
test_build_with_no_cache_and_oneapi | rewrite | 1 | forbidden: stub of owned find_project_root; docker[2] | cli/tests/test_docker.py; cli/sushiruntime/services/docker.py (grep)
test_run_with_no_flags_passes_through_gpu | rewrite | 1 | forbidden: stub of owned find_project_root; docker[3] | cli/tests/test_docker.py
test_run_without_gpu_passthrough | rewrite | 1 | forbidden: stub of owned find_project_root; docker[4] | cli/tests/test_docker.py
test_run_admin_adds_privileged_flags | rewrite | 1 | forbidden: stub of owned find_project_root; docker[5] | cli/tests/test_docker.py
test_run_is_still_interactive_through_runner | rewrite | 1 | forbidden: stub of owned docker.find_project_root; the subprocess.run stub is an out-of-process boundary and is allowed; docker[6] is the only test of stdio inheritance | cli/tests/test_docker.py; cli/sushiruntime/services/docker.py (grep); ../sushicore/source/sushicore/proc.py (grep)
```

### `cli/tests/test_gpu_backend_delegation.py`

```
test_the_windows_configure_carries_the_resolved_backend | rewrite | 1 | forbidden: _StubConfig replaces owned type Config; gpu_delegation[1] | cli/tests/test_gpu_backend_delegation.py; cli/sushiruntime/config.py (grep)
test_the_windows_configure_can_still_be_told_cpu | merge | 4 | gpu_delegation[2] is the same promise as gpu_delegation[1] with another input; merge into test_the_windows_configure_carries_the_resolved_backend as a row table over auto and cpu, with a real Config | cli/tests/test_gpu_backend_delegation.py
```

### `cli/tests/test_gpu_backend_resolution.py`

```
test_an_explicit_list_targets_every_vendor_in_it | keep | 9 | gpu_resolution[1]; boundary (cmake configure) | cli/tests/test_gpu_backend_resolution.py; cmake/gpu/GpuBackend.cmake (grep)
test_auto_requests_every_toolkit_it_finds | keep | 9 | gpu_resolution[2] | cli/tests/test_gpu_backend_resolution.py
test_a_vendor_with_no_hardware_drops_out_and_the_others_survive | keep | 9 | gpu_resolution[3] | cli/tests/test_gpu_backend_resolution.py
test_every_vendor_declining_falls_back_to_cpu | keep | 9 | gpu_resolution[4] | cli/tests/test_gpu_backend_resolution.py
test_a_named_vendor_with_no_hardware_fails_instead_of_declining | keep | 9 | gpu_resolution[5] | cli/tests/test_gpu_backend_resolution.py
test_cpu_cannot_be_combined_with_a_vendor | keep | 9 | gpu_resolution[6] | cli/tests/test_gpu_backend_resolution.py
test_an_unknown_vendor_is_rejected | keep | 9 | gpu_resolution[7] | cli/tests/test_gpu_backend_resolution.py
test_cpu_alone_still_resolves_to_cpu | keep | 9 | gpu_resolution[8]; distinct from [4] (explicit cpu, not declined) | cli/tests/test_gpu_backend_resolution.py
test_the_legacy_switch_still_names_cuda | keep | 9 | gpu_resolution[9] | cli/tests/test_gpu_backend_resolution.py
```

### `cli/tests/test_gpu_probe_paths.py`

```
test_a_real_compute_capability_still_parses | keep | 9 | gpu_probe[1]; the pattern is lifted from cmake/gpu/Cuda.cmake, so the file is under test | cli/tests/test_gpu_probe_paths.py; cmake/gpu/Cuda.cmake
test_a_separator_that_is_not_a_dot_is_rejected | keep | 9 | gpu_probe[2] | cli/tests/test_gpu_probe_paths.py; cmake/gpu/Cuda.cmake
test_a_versioned_prefix_is_expanded | keep | 9 | gpu_probe[3] | cli/tests/test_gpu_probe_paths.py; cmake/gpu/GpuBackend.cmake (grep)
test_the_newest_toolkit_comes_first | keep | 9 | gpu_probe[4] | cli/tests/test_gpu_probe_paths.py
test_an_empty_arch_does_not_swallow_the_probe_result | keep | 9 | gpu_probe[5] | cli/tests/test_gpu_probe_paths.py
test_a_stated_arch_is_published_unchanged | keep | 9 | gpu_probe[6] | cli/tests/test_gpu_probe_paths.py
test_an_unstated_arch_publishes_what_the_probe_found | keep | 9 | gpu_probe[7] | cli/tests/test_gpu_probe_paths.py
test_no_match_yields_an_empty_list | keep | 9 | gpu_probe[8] | cli/tests/test_gpu_probe_paths.py
```

### `cli/tests/test_help_screen.py`

```
test_root_help_lists_the_four_groups_with_the_sub_app_last | keep | 9 | help_screen[1] | cli/tests/test_help_screen.py
test_every_command_sits_under_its_own_group[4 groups] | merge | 4 | help_screen[2] is the set-equality promise of test_no_command_is_listed_under_a_group_it_does_not_belong_to; merge its ordered comparison into that test | cli/tests/test_help_screen.py
test_no_command_is_listed_under_a_group_it_does_not_belong_to | keep | 9 | help_screen[3]; receives the order check merged from test_every_command_sits_under_its_own_group | cli/tests/test_help_screen.py
test_a_command_help_carries_examples[command] | keep | 9 | help_screen[4] | cli/tests/test_help_screen.py
test_a_container_command_help_carries_examples[build / run] | keep | 9 | help_screen[5] | cli/tests/test_help_screen.py
test_the_container_sub_app_draws_a_page_listing_its_commands | keep | 9 | help_screen[6] | cli/tests/test_help_screen.py
test_the_container_sub_app_without_arguments_draws_the_same_page | merge | 4 | help_screen[7] is the same listing promise with the no-argument input; merge into test_the_container_sub_app_draws_a_page_listing_its_commands | cli/tests/test_help_screen.py
test_the_root_help_lists_the_root_options | keep | 9 | help_screen[8] | cli/tests/test_help_screen.py
test_the_root_without_arguments_draws_the_group_page | keep | 9 | help_screen[9] | cli/tests/test_help_screen.py
test_the_colour_scan_sees_markup_inside_an_f_string_and_a_style_argument | keep | 9 | help_screen[10]; a self-test of the scanner, which lives only in this file; the brick rule says it belongs in its own module (not a verdict change) | cli/tests/test_help_screen.py
test_no_raw_colour_name_is_left_in_the_package_source | keep | 9 | help_screen[11] | cli/tests/test_help_screen.py; cli/sushiruntime/ (package source)
```

### `cli/tests/test_install_hint.py`

```
test_the_install_command_is_sr_setup_for_the_toolchain[3 names] | keep | 9 | install_hint[1]; specified command string | cli/tests/test_install_hint.py; cli/sushiruntime/services/install_hint.py (grep)
test_a_missing_windows_compiler_names_sr_setup[3 names] | rewrite | 1 | forbidden: stubs owned Config.resolved_windows_compiler and replaces owned project.console.error with a list append; install_hint[2]; in-memory console is sushicore PlainRenderer over io.StringIO | cli/tests/test_install_hint.py; cli/sushiruntime/services/project.py (grep)
test_no_service_message_sends_the_user_to_hub_install | rewrite | 5 | the test reads service source text, not the messages printed (q5: a rename or a comment-free reflow changes the result); rewrite against install_hint[3] by checking the printed messages, or move the source rule to a checker under tools/ | cli/tests/test_install_hint.py; cli/sushiruntime/services/ (grep)
```

### `cli/tests/test_library_packages.py`

```
test_build_asks_for_the_closure_with_its_own_compiler | rewrite | 1 | forbidden: fixture stubs owned cfg.sushi_packages, load_config, find_project_root, load_build_env, _resolve_compiler, console (SimpleNamespace), and a recording driver; library_packages[1] | cli/tests/test_library_packages.py; cli/sushiruntime/services/project.py (grep)
test_an_asan_build_links_release_packages | rewrite | 1 | forbidden: same fixture stubs; library_packages[2] | cli/tests/test_library_packages.py
test_a_tsan_build_links_thread_sanitized_log_and_profiler_packages | rewrite | 1 | forbidden: same fixture stubs; library_packages[3] | cli/tests/test_library_packages.py
test_the_configure_passes_the_package_and_no_checkout_directory | delete | 1 | forbidden: fixture stubs; library_packages[4] is an exact argument-list assertion in test_the_definitions_skip_a_packaged_pin (library_packages[5]), which is itself rewritten below | cli/tests/test_library_packages.py
test_the_definitions_skip_a_packaged_pin | rewrite | 1 | forbidden: stub of owned Config.resolve_sushi; library_packages[5]; the fragment write is a valid file boundary | cli/tests/test_library_packages.py; cli/sushiruntime/config.py (grep)
test_the_run_dirs_lead_the_search_path_of_the_build_and_of_a_later_run | rewrite | 1 | forbidden: fixture stubs; library_packages[6] | cli/tests/test_library_packages.py; cli/sushiruntime/services/library_packages.py (grep)
test_the_windows_run_dirs_lead_path | resize | 8 | library_packages[7]: prepend_run_dirs on values only; size small | cli/tests/test_library_packages.py; cli/sushiruntime/services/library_packages.py (grep)
test_lean_and_the_build_dir_reach_the_tree_and_the_configure | rewrite | 1 | forbidden: fixture stubs; library_packages[8] is two promises (lean args; tree dir), split candidate on rewrite | cli/tests/test_library_packages.py
test_the_build_command_forwards_build_dir_and_lean | rewrite | 1 | forbidden: stub of owned cli.project_svc.build; library_packages[9] | cli/tests/test_library_packages.py
test_the_package_command_takes_the_tree_and_no_archive | rewrite | 1 | forbidden: stub of owned packaging.package; library_packages[10] | cli/tests/test_library_packages.py
test_stack_cxx_outranks_the_configured_compiler | resize | 8 | library_packages[11]: resolved_compilers and resolved_windows_compiler (cli/sushiruntime/config.py 108-117, 141-150) read values only; size small | cli/tests/test_library_packages.py; cli/sushiruntime/config.py
test_load_config_reads_the_stack_compiler_from_the_environment | rewrite | 1 | forbidden: stub of owned config._MODULE.load; the environment reach is a second problem; library_packages[12] | cli/tests/test_library_packages.py; cli/sushiruntime/config.py (grep)
```

### `cli/tests/test_packaging.py`

```
test_package_prints_one_line_for_the_install | rewrite | 1 | forbidden: packaging.console replaced by a recording _Console (owned sink); stubs owned load_config, find_project_root, load_build_env; packaging[1]; the _capture stub is an out-of-process boundary and is allowed | cli/tests/test_packaging.py; cli/sushiruntime/services/packaging.py (grep)
test_package_shows_cmakes_output_when_the_install_fails | rewrite | 1 | forbidden: same recording console and stubs; packaging[2] | cli/tests/test_packaging.py
test_package_refuses_a_foreign_directory_with_one_error_line | rewrite | 1 | forbidden: recording console and stubs; packaging[3] | cli/tests/test_packaging.py
test_package_refuses_a_protected_prefix_even_with_force[4 prefixes] | rewrite | 1 | forbidden: recording console and stubs; packaging[4] | cli/tests/test_packaging.py
test_package_with_force_clears_a_foreign_directory_and_marks_it | rewrite | 1 | forbidden: stubs owned load_config, find_project_root, load_build_env, recording console; packaging[5] | cli/tests/test_packaging.py
test_package_clears_its_default_stage_without_force | rewrite | 1 | forbidden: recording console and stubs; packaging[6] | cli/tests/test_packaging.py
test_the_command_passes_force_to_the_service | rewrite | 1 | forbidden: stub of owned packaging.package; packaging[7] | cli/tests/test_packaging.py
```

### `cli/tests/test_provision_commands.py`

```
test_the_four_provision_commands_are_registered | delete | 2 | provision_commands[1]: the substring "link" matches "unlink", so removing link would not fail; the exact list in cli/tests/test_cli.py::test_describe_lists_every_visible_command covers it | cli/tests/test_provision_commands.py; cli/tests/test_cli.py
test_setup_takes_the_shared_options | keep | 9 | provision_commands[2] | cli/tests/test_provision_commands.py
test_config_is_a_provision_config | rewrite | 5 | the docstring promises every field provision reads; the body asserts an inheritance relation, which fails on a refactor that keeps every field; rewrite as provision_commands[3] | cli/tests/test_provision_commands.py; cli/sushiruntime/config.py (grep)
```

### `cli/tests/test_stage_tree.py`

```
test_a_protected_directory_is_refused_even_with_force[10 cases] | keep | 9 | stage_tree[1]; boundary (real paths and the check) | cli/tests/test_stage_tree.py; cli/sushiruntime/services/stage_tree.py (grep)
test_the_root_of_another_drive_is_protected | resize | 8 | stage_tree[2]: path rules on a Windows drive letter, no filesystem needed; size small | cli/tests/test_stage_tree.py; cli/sushiruntime/services/stage_tree.py (grep)
test_a_foreign_directory_with_content_is_refused_without_force | keep | 9 | stage_tree[3] | cli/tests/test_stage_tree.py; cli/sushiruntime/services/stage_tree.py (grep)
test_a_foreign_directory_with_content_is_accepted_with_force | keep | 9 | stage_tree[4]; no assertion, but pytest fails on the raised StageTreeError | cli/tests/test_stage_tree.py
test_a_missing_or_empty_directory_is_accepted | keep | 9 | stage_tree[5]; same no-exception form as [4] | cli/tests/test_stage_tree.py
test_the_default_stage_is_accepted_without_a_marker | keep | 9 | stage_tree[6] | cli/tests/test_stage_tree.py
test_a_file_is_refused | keep | 9 | stage_tree[7]; pytest.raises without a message check, a weakness to tighten but not a verdict change | cli/tests/test_stage_tree.py
test_reset_empties_the_tree_and_writes_the_marker | keep | 9 | stage_tree[8] | cli/tests/test_stage_tree.py
test_a_tree_the_cli_staged_is_accepted_again_without_force | keep | 9 | stage_tree[9] | cli/tests/test_stage_tree.py
test_reset_reports_a_delete_that_fails | keep | 9 | stage_tree[10]; stubs stdlib shutil.rmtree to inject a fault, an OS boundary that the repository does not own | cli/tests/test_stage_tree.py
```

### `cli/tests/test_test_module.py`

```
test_module_selects_its_label_exactly | rewrite | 1 | forbidden: stubs owned load_config, find_project_root, load_build_env, _neutralize_runtime_ld, console (SimpleNamespace), and cmake_cache.is_stale; test_module[1] | cli/tests/test_test_module.py; cli/sushiruntime/services/project.py (grep)
test_size_label_applies_without_a_module | rewrite | 1 | forbidden: same stubs; test_module[2] | cli/tests/test_test_module.py
test_no_label_runs_every_size_but_the_quarantined | rewrite | 1 | forbidden: same stubs; test_module[3] | cli/tests/test_test_module.py
test_a_size_run_leaves_the_quarantined_tests_out | merge | 4 | test_module[4] is the exclude promise already asserted by test_no_label_runs_every_size_but_the_quarantined; merge as a row | cli/tests/test_test_module.py
test_the_quarantined_run_excludes_nothing | rewrite | 1 | forbidden: same stubs; test_module[5] | cli/tests/test_test_module.py
test_jobs_reach_ctest | rewrite | 1 | forbidden: same stubs; test_module[6] | cli/tests/test_test_module.py
test_no_jobs_leaves_the_choice_to_ctest | merge | 4 | test_module[6] with the None input; merge into test_jobs_reach_ctest | cli/tests/test_test_module.py
```

### `cli/tests/test_thread_sanitizer_applies.py`

```
test_windows_never_gets_tsan | keep | 9 | thread_sanitizer[1] | cli/tests/test_thread_sanitizer_applies.py; cli/sushiruntime/services/project.py (grep)
test_linux_tsan_type_gets_tsan | keep | 9 | thread_sanitizer[2] | cli/tests/test_thread_sanitizer_applies.py
test_no_other_linux_type_implies_tsan[4 types] | keep | 9 | thread_sanitizer[3] | cli/tests/test_thread_sanitizer_applies.py
```

### `cli/tests/test_toolchain_selection.py`

```
test_a_stated_toolchain_survives_a_pinned_compiler | keep | 9 | toolchain_selection[1]; boundary (cmake) | cli/tests/test_toolchain_selection.py; cmake/Sycl.cmake (grep)
test_a_mismatch_between_the_two_is_reported | keep | 9 | toolchain_selection[2] | cli/tests/test_toolchain_selection.py
test_inference_still_runs_when_no_toolchain_was_stated | rewrite | 7 | the body holds three inputs (acpp, icpx, clang++) as three configures; rows, as the sign for a loop over inputs says; toolchain_selection[3] | cli/tests/test_toolchain_selection.py
test_a_matching_pair_does_not_warn | keep | 9 | toolchain_selection[4] | cli/tests/test_toolchain_selection.py
test_the_cache_help_text_survives_inference | keep | 9 | toolchain_selection[5] | cli/tests/test_toolchain_selection.py
test_the_stamp_changes_with_the_toolchain | resize | 8 | toolchain_selection[6]: _configure_stamp is a pure function of values; size small | cli/tests/test_toolchain_selection.py; cli/sushiruntime/services/project.py (grep)
test_the_stamp_changes_with_a_pinned_compiler | resize | 8 | toolchain_selection[7]; size small | cli/tests/test_toolchain_selection.py
test_the_stamp_changes_with_a_pinned_library_checkout | resize | 8 | toolchain_selection[8]; size small | cli/tests/test_toolchain_selection.py
test_the_stamp_is_stable_for_an_unchanged_configuration | resize | 8 | toolchain_selection[9]; size small | cli/tests/test_toolchain_selection.py
```

### `cli/tests/test_windows_cxx_flags.py`

```
test_a_release_configure_states_empty_cxx_flags[3 toolchains] | rewrite | 1 | forbidden: _StubConfig replaces owned type Config; windows_cxx_flags[1] | cli/tests/test_windows_cxx_flags.py; cli/sushiruntime/config.py (grep)
test_an_asan_configure_states_the_asan_flags_only | rewrite | 1 | forbidden: _StubConfig replaces owned type Config; windows_cxx_flags[2] | cli/tests/test_windows_cxx_flags.py
test_the_base_flags_are_part_of_the_configure_stamp | keep | 9 | windows_cxx_flags[3]; monkeypatches a module constant (BASE_CXX_FLAGS), an input the stamp reads, not a stub of a function or type | cli/tests/test_windows_cxx_flags.py; cli/sushiruntime/services/project.py (grep)
```

### `tests/applications/examples/adapter/test_diffusion_backend.cpp`

```
AUniformFieldStaysUniform | rewrite | 1 | forbidden: a Small_ test reaches the devices (create_backend creates a backend on the machine's devices, diffusion_backend.hpp:89); the typed tolerance K_TOLERANCE = 1e-5f bypasses tolerance.hpp; diffusion_backend[1]; split into a host step test (small) and a device-to-host-reference test (medium) | tests/applications/examples/adapter/test_diffusion_backend.cpp; applications/examples/adapter/diffusion_backend.hpp
EveryCodeHasAName | rewrite | 1 | forbidden: a trivial default-value check (Status{}.is_ok()); the name promises every code but the body checks three of six (diffusion_backend.hpp:41-48); diffusion_backend[2] | tests/applications/examples/adapter/test_diffusion_backend.cpp; applications/examples/adapter/diffusion_backend.hpp
```

### `tests/applications/examples/test_distributed_kernels.cpp`

```
TheKernelAddsTwoVectors | split | 3 | two promises: distributed_kernels[1] (registration) and distributed_kernels[2] (element-wise sum); the body also makes its own sycl::queue, which the one-device-per-process rule forbids in a test body (testing SKILL.md, Running fast 3); raw USM is not freed on an early ASSERT | tests/applications/examples/test_distributed_kernels.cpp; applications/examples/distributed_kernels.hpp
```

### `tests/engine/dependency_tracking/test_dependency_tracker.cpp`

```
RawCreatesEdge | keep | 9 | dependency_tracker[1]; in_degree is the observable the module README names, so q5 is no | tests/engine/dependency_tracking/test_dependency_tracker.cpp; modules/engine/dependency_tracking/include/SushiRuntime/dependency_tracking/dependency_tracker.hpp; tests/engine/dependency_tracking/README.md
WarAndWawChain | split | 3 | the name needs "and"; the rd==1 check repeats RawCreatesEdge (dependency_tracker[1]); w2==2 joins WAR and WAW, which a count cannot tell apart; split into dependency_tracker[2] as a WAR test and a WAW test | tests/engine/dependency_tracking/test_dependency_tracker.cpp; modules/engine/dependency_tracking/include/SushiRuntime/dependency_tracking/dependency_tracker.hpp
ReadersDependOnWriterNotEachOther | keep | 9 | dependency_tracker[3]; the r1 check is setup, not a second promise | tests/engine/dependency_tracking/test_dependency_tracker.cpp
SharedBuffersCreateSingleEdge | keep | 9 | dependency_tracker[4] | tests/engine/dependency_tracking/test_dependency_tracker.cpp
NoSelfDependency | keep | 9 | dependency_tracker[5] | tests/engine/dependency_tracking/test_dependency_tracker.cpp
DisjointSubRegionsAreParallel | keep | 9 | dependency_tracker[6] | tests/engine/dependency_tracking/test_dependency_tracker.cpp
OverlappingSubRegionsAreOrdered | keep | 9 | dependency_tracker[7] | tests/engine/dependency_tracking/test_dependency_tracker.cpp
WholeBufferConflictsWithSubRegion | keep | 9 | dependency_tracker[8] | tests/engine/dependency_tracking/test_dependency_tracker.cpp
ReadDependsOnAllOverlappingWrites | keep | 9 | dependency_tracker[9] | tests/engine/dependency_tracking/test_dependency_tracker.cpp
WriteWalkContinuesPastPartialWriter | split | 3 | dependency_tracker[10] is the covering-write count; the wp==0 check repeats dependency_tracker[6] (DisjointSubRegionsAreParallel), a second promise | tests/engine/dependency_tracking/test_dependency_tracker.cpp
DistinctAllocationsAreParallel | keep | 9 | dependency_tracker[11] | tests/engine/dependency_tracking/test_dependency_tracker.cpp
```

### `tests/engine/execution/benchmark_runtime_context.cpp`

```
Performance_ProfilingQueueBench.ProfilingQueueDeltaOverPlainQueue | rewrite | 7 | benchmark kind: prints wall-clock figures and asserts only progress; move to the bench kind (`<cli> bench`), keep its equal-result check as a small test | tests/engine/execution/benchmark_runtime_context.cpp; tests/engine/execution/README.md; modules/engine/runtime_assembly/include/SushiRuntime/runtime_assembly/context_factory.hpp
Performance_AsyncOverlapBench.CPUGPUOverlapSpeedup | rewrite | 7 | benchmark kind: asserts on wall-clock overlap ratios and skips on WSL; move to the bench kind | tests/engine/execution/benchmark_runtime_context.cpp; tests/engine/execution/README.md; modules/engine/runtime_assembly/include/SushiRuntime/runtime_assembly/context_factory.hpp
```

### `tests/engine/execution/benchmark_scheduler_execution_engine.cpp`

```
Performance_GraphCompileBench.CompileScaling | rewrite | 7 | benchmark kind: measures compile latency over sizes, asserts only progress; move to the bench kind | tests/engine/execution/benchmark_scheduler_execution_engine.cpp; tests/engine/execution/README.md
Performance_GraphCompileBench.TransferComputeOverlapSpeedup | rewrite | 1 | bare tolerance literal 1e-1f in two result comparisons (forbidden kind); result loops with break in the body; benchmark kind otherwise | tests/engine/execution/benchmark_scheduler_execution_engine.cpp; tests/engine/execution/README.md; modules/engine/runtime_assembly/include/SushiRuntime/runtime_assembly/context_factory.hpp
Performance_SchedulerDispatchBench.DispatchOverheadVsInlineBaseline | rewrite | 7 | benchmark kind: per-task wall time against an inline baseline; move to the bench kind | tests/engine/execution/benchmark_scheduler_execution_engine.cpp; tests/engine/execution/README.md; modules/engine/runtime_assembly/include/SushiRuntime/runtime_assembly/context_factory.hpp
Stress_TaskSchedulerStress.OneMillionTaskBomb | rewrite | 1 | sleep-polling wait to synchronise with a 30 s wall-clock limit (forbidden kind); the promise (every task runs) needs a waiting seam that takes a clock | tests/engine/execution/benchmark_scheduler_execution_engine.cpp; modules/engine/runtime_assembly/include/SushiRuntime/runtime_assembly/context_factory.hpp
Stress_TaskSchedulerStress.MultiProducerContention | rewrite | 1 | wall-clock spin wait with a 30 s limit to synchronise with producers (forbidden kind) | tests/engine/execution/benchmark_scheduler_execution_engine.cpp
Stress_TaskSchedulerStress.RapidArmAndStopThrashing | rewrite | 1 | its only check is SUCCEED() (forbidden kind); a stress test with a sanitizer oracle still needs asserted counts, e.g. executed tasks per cycle | tests/engine/execution/benchmark_scheduler_execution_engine.cpp
Stress_TaskSchedulerStress.PriorityBypassUnderLoad | rewrite | 1 | sleep-polling wait with a 5 s wall-clock limit (forbidden kind); loop with break in the body; order check needs a seeded or single-thread executor | tests/engine/execution/benchmark_scheduler_execution_engine.cpp; modules/engine/probe_tasks/include/SushiRuntime/probe_tasks/priority_tracking_task.hpp
Performance_StealLocalityBench.StealStaysNodeLocal | rewrite | 7 | benchmark kind: locality ratio from a timed run on real NUMA hardware; the deterministic check is Medium_NumaStealTest in tests/engine/scheduler/test_task_scheduler.cpp | tests/engine/execution/benchmark_scheduler_execution_engine.cpp; tests/engine/execution/README.md
Performance_StealNumaEffectBench.NUMAAwareVsBlindThroughput | rewrite | 7 | benchmark kind: throughput comparison with GTEST_SKIP on single-node hosts; the locality promise is Medium_NumaStealTest, named in its skip text | tests/engine/execution/benchmark_scheduler_execution_engine.cpp; tests/engine/execution/README.md
Performance_WideFanout.ManySmallStepDrainLatency | rewrite | 7 | benchmark kind: per-step wall-clock latency and throughput; move to the bench kind | tests/engine/execution/benchmark_scheduler_execution_engine.cpp; tests/engine/execution/README.md
```

### `tests/engine/execution/test_chain_plan.cpp`

```
Medium_ChainPlan.KernelsOnOneDeviceChainInOrder | resize | 8 | classify reads which device owns USM from the context, so the test needs a device; the seam is a device-ownership query with an in-memory adapter, then the test is small | tests/engine/execution/test_chain_plan.cpp; modules/engine/graph/include/SushiRuntime/graph/chain_plan.hpp; modules/engine/runtime_assembly/include/SushiRuntime/runtime_assembly/context_factory.hpp
Medium_ChainPlan.AHostNodeOrForeignMemoryKeepsThePlanOff | split | 3 | three promises: host node keeps the plan off, foreign memory keeps it off, empty plan is not chainable | tests/engine/execution/test_chain_plan.cpp; modules/engine/graph/include/SushiRuntime/graph/chain_plan.hpp
```

### `tests/engine/execution/test_chain_run.cpp`

```
Medium_ChainRun.EachStepRunsTheWholeChain | rewrite | 2 | the in-order claim cannot fail: add_one commutes, so the final value 4 holds in any order; name and body disagree; rewrite with a non-commutative step | tests/engine/execution/test_chain_run.cpp; modules/engine/runtime_assembly/include/SushiRuntime/runtime_assembly/context_factory.hpp
Medium_ChainRun.WaitReturnsAfterTheStep | keep | 9 | ChainRun: wait returns once a step is done (value is 1 after wait; device boundary) | tests/engine/execution/test_chain_run.cpp; modules/engine/runtime_assembly/include/SushiRuntime/runtime_assembly/context_factory.hpp
```

### `tests/engine/execution/test_dropped_dispatches.cpp`

```
Small_DroppedDispatches.LogsNothingWhenNoneWasLost | rewrite | 8 | installs a sink on the process-wide logger through LogCapture; the function logs through the global macro, so the rewrite needs a logger port with an in-memory adapter | tests/engine/execution/test_dropped_dispatches.cpp; modules/foundation/test_runner/include/SushiRuntime/test_runner/log_capture.hpp; modules/engine/execution/include/SushiRuntime/execution/dropped_dispatches.hpp
Small_DroppedDispatches.LogsOneLineNamingEachCause | rewrite | 8 | process-wide logger sink (LogCapture); needs the logger port with an in-memory adapter | tests/engine/execution/test_dropped_dispatches.cpp; modules/foundation/test_runner/include/SushiRuntime/test_runner/log_capture.hpp; modules/engine/execution/include/SushiRuntime/execution/dropped_dispatches.hpp
Small_DroppedDispatches.ALossThatNamesNoNodeLeavesTheNodeOut | rewrite | 8 | process-wide logger sink (LogCapture); needs the logger port with an in-memory adapter | tests/engine/execution/test_dropped_dispatches.cpp; modules/foundation/test_runner/include/SushiRuntime/test_runner/log_capture.hpp; modules/engine/execution/include/SushiRuntime/execution/dropped_dispatches.hpp
```

### `tests/engine/execution/test_execution_engine.cpp`

```
Medium_RunInProgress.TheInterfaceDefaultReportsNoRun | resize | 8 | MinimalEngine needs no device, thread or context; the test is small by nature and only its size label is wrong | tests/engine/execution/test_execution_engine.cpp
Medium_RunInProgress.AFreshEngineReportsNoRun | delete | 1 | default value (forbidden kind); the interface default it would test is what TheInterfaceDefaultReportsNoRun states | tests/engine/execution/test_execution_engine.cpp
Medium_RunInProgress.ABlockingRunIsInProgressUntilExecuteReturns | resize | 8 | the check runs on a scheduler thread through a device context; the seam is an executor the engine is given, then a small test | tests/engine/execution/test_execution_engine.cpp; tests/engine/execution/README.md
Medium_RunInProgress.AnAsynchronousRunIsInProgressUntilCompleteReturns | resize | 8 | threads and a device context; the same executor seam makes it small | tests/engine/execution/test_execution_engine.cpp; tests/engine/execution/README.md
Medium_RunInProgress.ARunWhoseTaskThrowsReturnsKernelFailedAndLeavesNoRunInProgress | split | 3 | two promises: a blocking run that throws returns KernelFailed; an asynchronous run that throws does the same in complete | tests/engine/execution/test_execution_engine.cpp
Medium_RunInProgress.AChainedRunIsInProgressUntilFinishReturnsOrItIsDestroyed | rewrite | 1 | control flow in the body: GTEST_SKIP when classify_chain does not chain, so a classification regression is reported as a skip, not a failure (forbidden kind); also two promises | tests/engine/execution/test_execution_engine.cpp; modules/engine/graph/include/SushiRuntime/graph/chain_plan.hpp
Medium_RunInProgress.AScheduledRunInsideAChainedRunLeavesTheChainedRunShowing | rewrite | 1 | control flow in the body: the same GTEST_SKIP on classification (forbidden kind); also two promises | tests/engine/execution/test_execution_engine.cpp; modules/engine/graph/include/SushiRuntime/graph/chain_plan.hpp
```

### `tests/engine/execution/test_execution_log_category.cpp`

```
Small_ExecutionLogCategory.TheCategoryIsNamedAfterTheModule | delete | 1 | static_assert with SUCCEED() (forbidden kinds); a log category name is a constant | tests/engine/execution/test_execution_log_category.cpp
```

### `tests/engine/execution/test_run_failures.cpp`

```
Small_RunFailures.NeitherFailureSucceedsQuietly | rewrite | 8 | LogCapture installs the process-wide sink; the pure merge result is separable, the quiet log needs the logger port with an in-memory adapter | tests/engine/execution/test_run_failures.cpp; modules/foundation/test_runner/include/SushiRuntime/test_runner/log_capture.hpp; modules/engine/execution/include/SushiRuntime/execution/run_failures.hpp
Small_RunFailures.ATaskFailureAloneIsReturned | rewrite | 8 | LogCapture on the process-wide logger; the returned failure is pure and stays, the empty-log check needs the logger port | tests/engine/execution/test_run_failures.cpp; modules/foundation/test_runner/include/SushiRuntime/test_runner/log_capture.hpp; modules/engine/execution/include/SushiRuntime/execution/run_failures.hpp
Small_RunFailures.AQueueFailureAloneIsReturned | rewrite | 8 | LogCapture on the process-wide logger; the returned failure is pure, the empty-log check needs the logger port | tests/engine/execution/test_run_failures.cpp; modules/foundation/test_runner/include/SushiRuntime/test_runner/log_capture.hpp; modules/engine/execution/include/SushiRuntime/execution/run_failures.hpp
Small_RunFailures.LogsTheQueueErrorADroppedTaskErrorHid | rewrite | 8 | the WARN line is checked through the process-wide sink; needs the logger port with an in-memory adapter | tests/engine/execution/test_run_failures.cpp; modules/foundation/test_runner/include/SushiRuntime/test_runner/log_capture.hpp; modules/engine/execution/include/SushiRuntime/execution/run_failures.hpp
```

### `tests/engine/execution/test_runtime_context.cpp`

```
Medium_ProfilingQueue.ProfilingQueueAddsThePropertyAndSharesTheContext | keep | 9 | profiling queue carries enable_profiling and shares the plain queue's context; sycl queue property is a device boundary | tests/engine/execution/test_runtime_context.cpp; modules/engine/runtime_assembly/include/SushiRuntime/runtime_assembly/context_factory.hpp
Medium_ProfilingQueue.NoProfilingQueueExistsBeforeTheFirstRequest | rewrite | 7 | loops over device indices in the body (rows sign); rewrite as one row per device index, with the lazy-build promise stated once | tests/engine/execution/test_runtime_context.cpp
Medium_ProfilingQueue.UsingThePlainQueueBuildsNoProfilingQueue | merge | 4 | same promise as NoProfilingQueueExistsBeforeTheFirstRequest (plain queue builds no profiling queue) with a different input; merge into that test's rows | tests/engine/execution/test_runtime_context.cpp
Medium_ProfilingQueue.TheProfilingFlavourIsNotThePlainQueue | keep | 9 | distinct queue identity for the profiling flavour; device boundary | tests/engine/execution/test_runtime_context.cpp
Medium_ProfilingQueue.USMFromThePlainQueueIsUsableOnTheProfilingQueue | rewrite | 7 | loops in the body build input and check every element (rows and builder sign); a kernel result compared through a helper would keep the promise | tests/engine/execution/test_runtime_context.cpp
Medium_RuntimeContext.InvalidNUMAFallbackQueue | merge | 4 | same promise as InvalidNUMAFallsBackToAUsableNode (invalid NUMA id gives a usable queue), input 999 instead of -5; merge into that test's rows | tests/engine/execution/test_runtime_context.cpp
Medium_RuntimeContext.InvalidNUMAFallsBackToAUsableNode | split | 3 | three promises: a negative NUMA id gives a queue without throwing; an allocation falls back and is recorded; release clears the owner | tests/engine/execution/test_runtime_context.cpp
Medium_RuntimeContext.LiveUSMCountFollowsAllocateFreeAndForget | split | 3 | two promises: the count follows allocate and release; the count follows adopt and forget | tests/engine/execution/test_runtime_context.cpp
Medium_RuntimeContext.AlignedAllocationsStartOnTheAlignment | rewrite | 1 | loops, an if with continue in the body, and a silent RecordProperty skip when the probe fails (forbidden control flow; a broken shared allocator passes) | tests/engine/execution/test_runtime_context.cpp
Medium_RuntimeContext.AlignedAllocationsAreRecordedAndFreed | rewrite | 7 | loop with checks between allocate and release (rows sign); rows over the kinds, one check per action | tests/engine/execution/test_runtime_context.cpp
Medium_RuntimeContextDeathTest.AnAlignedAllocationOnAMissingDeviceStops | keep | 9 | death test: an allocation on a missing device index stops the process; death tests fork, which the page does not size, so the medium label is kept here | tests/engine/execution/test_runtime_context.cpp
Large_RuntimeContextNvidia.NvidiaGPUDiscovered | keep | 9 | hardware discovery finds the NVIDIA GPU on a CUDA host; large, and skipped when no CUDA device exists | tests/engine/execution/test_runtime_context.cpp
Large_RuntimeContextNvidia.NvidiaGPUHasNUMAMapping | rewrite | 2 | GTEST_SKIP when the mapped id is -1 turns a failed mapping into a skip; the promise cannot fail in the case it names; skip only on a declared environment (WSL) | tests/engine/execution/test_runtime_context.cpp
Large_RuntimeContextNvidia.RuntimeContextExposesNvidiaQueue | rewrite | 1 | loop with break in the body and EXPECT_NO_THROW around a submit (forbidden control flow); pick the CUDA queue with a helper and assert one result | tests/engine/execution/test_runtime_context.cpp
Large_RuntimeContextNvidia.NvidiaGPUVectorAdd | rewrite | 1 | loop with break in the body computing the verdict (forbidden control flow); messages name a GTX 1060, which is stale for the owner's RTX 3080 Ti | tests/engine/execution/test_runtime_context.cpp
Medium_RuntimeContextTopology.InjectedDistanceModelDrivesRuntimeContext | resize | 8 | the injected distances reach the context through a device-backed context; the seam is topology assembly without devices, then small | tests/engine/execution/test_runtime_context.cpp; modules/engine/runtime_assembly/include/SushiRuntime/runtime_assembly/context_factory.hpp
Medium_RuntimeContextTopology.RuntimeContextRoutesQueuesViaSyntheticNUMA | rewrite | 1 | loop over synthetic nodes with EXPECT_NO_THROW in the body (rows sign); one row per node | tests/engine/execution/test_runtime_context.cpp
Medium_RuntimeContextTopology.GPUWithoutPCIAddressTakesFallbackNUMANode | resize | 8 | NUMAMapper on a hand-built node needs no device; the size label is wrong and the test can be small; its two ASSERT lines check the struct's defaults only as setup | tests/engine/execution/test_runtime_context.cpp
Medium_RuntimeContextOffloadSeam.ShutdownStopsCoordinatorBeforeEngine | resize | 8 | a real worker pool and threads, a 10 s wait, a device context; the seam is an injected pool executor, then a small test of the stop order | tests/engine/execution/test_runtime_context.cpp
Medium_RuntimeContextObservers.AnUngatedObserverAlwaysWantsTiming | resize | 8 | the observer registry lives inside RuntimeContext behind a device context; the seam is a registry extracted from RuntimeContext, then small | tests/engine/execution/test_runtime_context.cpp; modules/engine/execution/source/runtime_context.cpp
Medium_RuntimeContextObservers.AGatedObserverWantsTimingOnlyWhileItsGateIsOpen | split | 3 | two promises: a gated observer asks for timing only while its gate is open; an ungated observer keeps timing whatever the gate says; the gate is a global flag shared across tests | tests/engine/execution/test_runtime_context.cpp
Medium_RuntimeContextObservers.ADeliveryToAGatedObserverWaitsForItsGate | resize | 8 | delivery through a device-backed context; the same registry seam; the test's gate is a global flag shared with other tests (state left behind) | tests/engine/execution/test_runtime_context.cpp; modules/engine/execution/source/runtime_context.cpp
```

### `tests/engine/execution/test_runtime_settings.cpp`

```
Medium_RuntimeSettings.DefaultsAskForNothingExtra | rewrite | 1 | default values (forbidden kind); the documented defaults are a contract, so state them through the public interface that uses them | tests/engine/execution/test_runtime_settings.cpp
Medium_RuntimeSettings.ThePoolSettingsCarryThePoolFields | resize | 8 | pure field mapping with no device; the medium label is wrong, the test is small | tests/engine/execution/test_runtime_settings.cpp
```

### `tests/engine/execution/test_scheduler_execution_engine.cpp`

```
Large_AsyncExecutionTest.CPUGPUConcurrentVectorAdd | rewrite | 1 | loops with break in the body that decide the verdict (forbidden control flow); the name claims concurrency and the body checks only results | tests/engine/execution/test_scheduler_execution_engine.cpp
Large_AsyncExecutionTest.AsyncDispatchDoesNotBlock | rewrite | 1 | retry loop with an if in the body; the check is a wall-clock guess that a heavy kernel is still running (no deterministic oracle) | tests/engine/execution/test_scheduler_execution_engine.cpp
Medium_DevicePipelineTest.DiamondAllKernelNodes | rewrite | 1 | bare tolerance literals 1e-3f in three comparisons (forbidden kind); loops with break in the body | tests/engine/execution/test_scheduler_execution_engine.cpp
Medium_HeterogeneousPipelineTest.ManyChainsHostDeviceHost | rewrite | 1 | bare tolerance literal 1e-3f; loops with break in the body; a long setup builds 48 tasks (sign of a long test) | tests/engine/execution/test_scheduler_execution_engine.cpp
Medium_SyclExecutionTest.EndToEndVectorAddition | rewrite | 1 | loop with break in the body that decides the verdict (forbidden control flow) | tests/engine/execution/test_scheduler_execution_engine.cpp
Medium_SyclExecutionTest.MultiStageChain | rewrite | 1 | bare tolerance expression expected times 1e-6 in EXPECT_NEAR (forbidden kind) | tests/engine/execution/test_scheduler_execution_engine.cpp
Medium_ReplayHostWrite.AReplayReadsASharedWordTheHostWrote | merge | 4 | same promise as AReplayReadsAHostWordTheHostWrote with the other USM kind as input; merge into one rows test over the kinds, which also drops the device loop from the body | tests/engine/execution/test_scheduler_execution_engine.cpp
Medium_ReplayHostWrite.AReplayReadsAHostWordTheHostWrote | merge | 4 | same promise as AReplayReadsASharedWordTheHostWrote with the other USM kind as input; merge into one rows test over the kinds | tests/engine/execution/test_scheduler_execution_engine.cpp
Medium_TaskGraphTest.SequentialUSMDependency | resize | 8 | threads, a 5 ms sleep in the order recorder and a device context decide the order; the seam is a single-thread executor on a seeded schedule, then small | tests/engine/execution/test_scheduler_execution_engine.cpp; modules/engine/probe_tasks/include/SushiRuntime/probe_tasks/order_recorder.hpp
Medium_TaskGraphTest.IndependentRootsExecution | resize | 8 | threads and a device context; the name says concurrent, the body checks only completion; the same executor seam | tests/engine/execution/test_scheduler_execution_engine.cpp
Medium_TaskGraphTest.FanOutFanInDiamond | resize | 8 | threads, a 10 ms sleep in readers and a device context; the same executor seam | tests/engine/execution/test_scheduler_execution_engine.cpp
```

### `tests/engine/graph/test_chain_plan.cpp`

```
Small_ChainPlanShape.ADefaultPlanDoesNotChain | rewrite | 1 | default values (forbidden kind); state it through a plan classify_chain has not classified, or drop the default check | tests/engine/graph/test_chain_plan.cpp; modules/engine/graph/include/SushiRuntime/graph/chain_plan.hpp
Small_ChainPlanShape.APendingEventStartsWithUnknownWidths | rewrite | 1 | default widths (forbidden kind); the promise is the widths snapshotted at submit, which needs a submitted node | tests/engine/graph/test_chain_plan.cpp; modules/engine/graph/include/SushiRuntime/graph/chain_plan.hpp
```

### `tests/engine/graph/test_compiled_graph.cpp`

```
Small_NodeDispatchWidth.DefaultConstructedNodeReportsUnknown | delete | 1 | default value (forbidden kind); tested by tests/engine/task/test_node_widths.cpp::ABakedNodeReportsItsWidthTwice, which checks snapshot_widths of a default Node | tests/engine/graph/test_compiled_graph.cpp; tests/engine/task/test_node_widths.cpp
Small_NodeDispatchWidth.UnstampedNodeReportsUnknown | delete | 1 | default value through add_node (forbidden kind); the unknown default is checked in tests/engine/task/test_node_widths.cpp | tests/engine/graph/test_compiled_graph.cpp; tests/engine/task/test_node_widths.cpp
Small_NodeDispatchWidth.BakedWidthAndNameStampTheNodeTheyAreSetOn | keep | 9 | a width and a name set after add land on the node added last; its configuration half overlaps AReturnedNodeTakesItsRegistrationAndConfiguration in test_task_graph.cpp, which should lose that half | tests/engine/graph/test_compiled_graph.cpp; tests/engine/graph/test_task_graph.cpp; modules/engine/graph/include/SushiRuntime/graph/task_graph.hpp
Small_NodeDispatchWidth.BakedWidthStampsOnlyTheNodeItIsSetOn | keep | 9 | a width set on the second node leaves the first unknown, checked on the compiled plan | tests/engine/graph/test_compiled_graph.cpp; modules/engine/graph/include/SushiRuntime/graph/task_graph.hpp
Small_NodeDispatchWidth.BakedNodeReportsOneWidthTwice | delete | 4 | same promise as tests/engine/task/test_node_widths.cpp::ABakedNodeReportsItsWidthTwice; the unit snapshot_widths lives in the task module, so that copy stays | tests/engine/graph/test_compiled_graph.cpp; tests/engine/task/test_node_widths.cpp
Small_NodeDispatchWidth.DynamicNodeSeparatesCapacityFromLiveCount | delete | 4 | same promise as tests/engine/task/test_node_widths.cpp::ALateCountSetsTheLiveWidth; that copy stays | tests/engine/graph/test_compiled_graph.cpp; tests/engine/task/test_node_widths.cpp
Small_NodeDispatchWidth.UseBakedFallsBackToTheBakedWidth | keep | 9 | K_USE_BAKED makes the live width fall back to the baked width; test_late_bindings checks only the sentinel value | tests/engine/graph/test_compiled_graph.cpp; tests/engine/task/test_node_widths.cpp
Small_NodeDispatchWidth.OversizedWidthReportsUnknownRatherThanTruncating | delete | 4 | same promise as tests/engine/task/test_node_widths.cpp::AWidthBeyondThirtyTwoBitsIsUnknown; that copy stays | tests/engine/graph/test_compiled_graph.cpp; tests/engine/task/test_node_widths.cpp
Small_CompiledGraph.CapturesOneTableRowPerRegistrationIndex | keep | 9 | one row per registration index with the node name and operation | tests/engine/graph/test_compiled_graph.cpp; modules/engine/graph/include/SushiRuntime/graph/chain_plan.hpp
Small_CompiledGraph.ASharedIndexKeepsItsMultiplicity | keep | 9 | nodes sharing an index make one row that counts them, and the ledger sizes from the same count | tests/engine/graph/test_compiled_graph.cpp
Small_CompiledGraph.CarriesTheCostOfTheFirstNode | keep | 9 | the row carries the cost its first node stated, zero included, and none when unstated | tests/engine/graph/test_compiled_graph.cpp
Small_CompiledGraph.AnIndexNoNodeRegisteredUnderHasNoRow | split | 3 | two promises: an index with no node has no row; the sentinel node is not a row or a node of the caller | tests/engine/graph/test_compiled_graph.cpp
Small_CompiledGraph.ANodeWithNoDeviceWorkIsHostNative | keep | 9 | host-native marking for a node without device work, and not for a kernel or a host body that submits device work | tests/engine/graph/test_compiled_graph.cpp
Small_CompiledGraph.AnEmptyPlanBuildsAnEmptyTable | keep | 9 | an empty plan captures an empty table, not none | tests/engine/graph/test_compiled_graph.cpp
Small_CompiledGraph.ACaptureAfterTheFirstLeavesTheEarlierTableIntact | keep | 9 | a second capture leaves the table an observer holds intact | tests/engine/graph/test_compiled_graph.cpp
```

### `tests/engine/graph/test_graph_log_category.cpp`

```
Small_GraphLogCategory.TheCategoryIsNamedAfterTheModule | delete | 1 | static_assert with SUCCEED() (forbidden kinds); a log category name is a constant | tests/engine/graph/test_graph_log_category.cpp
```

### `tests/engine/graph/test_task_graph.cpp`

```
Medium_TaskGraph.EveryAddReturnsTheNodeItAdded | split | 3 | several promises across seven calls: each returns its node; io registration; late bindings; execution mode per task kind; offload state from set_live_io; native node has no device work. Once split, the small size applies, since TaskGraph takes no context | tests/engine/graph/test_task_graph.cpp; modules/engine/graph/include/SushiRuntime/graph/task_graph.hpp; modules/engine/task/include/SushiRuntime/task/node.hpp
Medium_TaskGraph.AReturnedNodeTakesItsRegistrationAndConfiguration | split | 3 | two promises: a reserved registration is stamped on the node; configuration set through the returned reference lands on the node last added | tests/engine/graph/test_task_graph.cpp; modules/engine/graph/include/SushiRuntime/graph/task_graph.hpp
Medium_TaskGraphDeathTest.ARegistrationTheGraphNeverIssuedStops | keep | 9 | death test: add stops on a registration never issued, and the last node stays null after it; death tests fork, which the page does not size, so the medium label is kept here | tests/engine/graph/test_task_graph.cpp; modules/engine/graph/include/SushiRuntime/graph/task_graph.hpp
Medium_TaskGraphDeathTest.EveryAddOverloadStopsOnAnUnissuedRegistration | keep | 9 | death test: each add overload stops on an unissued registration; one promise over five entry points | tests/engine/graph/test_task_graph.cpp; modules/engine/graph/include/SushiRuntime/graph/task_graph.hpp
Medium_TaskGraph.AReservedRegistrationIsAcceptedTwice | split | 3 | two promises: a reserved registration may be used by two nodes; an index a node drew may be shared by a later node | tests/engine/graph/test_task_graph.cpp; modules/engine/graph/include/SushiRuntime/graph/task_graph.hpp
Medium_TaskGraph.TheCompiledPlanOwnsTheReturnedNodes | resize | 8 | compile() takes no context and runs no device code, so the test is small; the size label is wrong | tests/engine/graph/test_task_graph.cpp; modules/engine/graph/include/SushiRuntime/graph/task_graph.hpp
Medium_TaskGraph.CompileWithNoRootReturnsInvalidGraphNamingANode | rewrite | 5 | the cycle is built by add_successor and in_degree on Node, which are internal edge state; the public add calls cannot make an edge back, so a public way to state a dependency is an owner decision | tests/engine/graph/test_task_graph.cpp; modules/engine/task/include/SushiRuntime/task/node.hpp; modules/engine/graph/include/SushiRuntime/graph/task_graph.hpp
Medium_TaskGraph.CompileWithACycleBehindARootReturnsInvalidGraphNamingANode | rewrite | 5 | same internal edge hooks (add_successor, in_degree) build the cycle; same owner decision | tests/engine/graph/test_task_graph.cpp; modules/engine/task/include/SushiRuntime/task/node.hpp; modules/engine/graph/include/SushiRuntime/graph/task_graph.hpp
Medium_TaskGraph.AGraphCompilesAfterAFailedCompile | rewrite | 5 | the failing compile is built through the same internal edge hooks; same owner decision | tests/engine/graph/test_task_graph.cpp; modules/engine/task/include/SushiRuntime/task/node.hpp; modules/engine/graph/include/SushiRuntime/graph/task_graph.hpp
```

### `tests/engine/probe_tasks/test_atomic_counter_task.cpp`

```
Medium_AtomicCounterTask.EachRunAddsOne | rewrite | 1 | asserts getters and constants (execution_mode, is_splittable, split) (forbidden kind); keep the counting promise alone; a default sycl::queue is built only to pass a host task that ignores it | tests/engine/probe_tasks/test_atomic_counter_task.cpp; modules/engine/probe_tasks/include/SushiRuntime/probe_tasks/atomic_counter_task.hpp
```

### `tests/engine/probe_tasks/test_order_recorder.cpp`

```
Medium_OrderRecorder.AppendsItsIdWhenCalled | resize | 8 | host task that uses no device, but a default sycl::queue is constructed to pass it; the seam is a queue value a host task may take without a device (owner's decision on the TaskWork queue argument) | tests/engine/probe_tasks/test_order_recorder.cpp; modules/engine/probe_tasks/include/SushiRuntime/probe_tasks/order_recorder.hpp
```

### `tests/engine/probe_tasks/test_priority_tracking_task.cpp`

```
Medium_PriorityTrackingTask.RecordsItsPriorityWhenItRuns | rewrite | 1 | asserts the getter priority() and constant mode and split flags (forbidden kind); keep the order of records only | tests/engine/probe_tasks/test_priority_tracking_task.cpp; modules/engine/probe_tasks/include/SushiRuntime/probe_tasks/priority_tracking_task.hpp
```

### `tests/engine/region_boundary/test_boundary_access.cpp`

```
Small_BoundaryAccess.AReadPastItsPredecessorsWriteKeepsAnEntryPin | keep | 9 | a read reaching past its predecessor's write keeps an entry pin with the read bytes | tests/engine/region_boundary/test_boundary_access.cpp; modules/engine/graph/include/SushiRuntime/graph/task_graph.hpp
Small_BoundaryAccess.AReadInsideItsPredecessorsWriteHasNoEntryPin | keep | 9 | a read inside a predecessor's write has no entry pin | tests/engine/region_boundary/test_boundary_access.cpp; modules/engine/graph/include/SushiRuntime/graph/task_graph.hpp
Small_BoundaryAccess.AWriteBehindAReadingPredecessorKeepsItsWritePin | keep | 9 | a write behind a reader keeps its write pin | tests/engine/region_boundary/test_boundary_access.cpp; modules/engine/graph/include/SushiRuntime/graph/task_graph.hpp
Small_BoundaryAccess.OnlyAContainingSuccessorWriteShadowsAnExit | keep | 9 | only a successor write that contains an access shadows the exit; the two graphs are the two sides of one rule | tests/engine/region_boundary/test_boundary_access.cpp; modules/engine/graph/include/SushiRuntime/graph/task_graph.hpp
```

### `tests/engine/runtime_assembly/test_context_factory.cpp`

```
Medium_ContextFactory.BuildsAContextWithADevice | keep | 9 | boundary: device discovery yields a context with at least one device | tests/engine/runtime_assembly/test_context_factory.cpp; modules/engine/runtime_assembly/include/SushiRuntime/runtime_assembly/context_factory.hpp
```

### `tests/engine/runtime_assembly/test_scheduler_factory.cpp`

```
Medium_SchedulerFactory.TheSchedulerRunsWhatItIsGiven | rewrite | 1 | sleep-polling wait to synchronise with a 30 s deadline (forbidden kind); wait for a counter through a clock the test advances, or a join on the scheduler | tests/engine/runtime_assembly/test_scheduler_factory.cpp; modules/engine/runtime_assembly/include/SushiRuntime/runtime_assembly/context_factory.hpp
```

### `tests/engine/scheduler/test_dispatch_record.cpp`

```
Small_DispatchRecord.NothingClaimsWithoutProfiling | keep | 9 | dispatch_record: a dispatch claims its pairing only under profiling, for a kernel or for a host body of a node that is not the sentinel | tests/engine/scheduler/test_dispatch_record.cpp; modules/engine/scheduler/include/SushiRuntime/scheduler/dispatch_record.hpp
Small_DispatchRecord.AHostBodyClaimsOnlyForARealNode | rewrite | 7 | several results of different calls in one body (kernel, host body with a node, with nullptr, with the sentinel); rewrite as one row table over the three inputs of dispatch_record: a dispatch claims its pairing only under profiling | tests/engine/scheduler/test_dispatch_record.cpp; modules/engine/scheduler/include/SushiRuntime/scheduler/dispatch_record.hpp
Small_DispatchRecord.AnEmptyHostBodySubmittedNothing | rewrite | 7 | three calls with different results in one body; rewrite as rows over the inputs of dispatch_record: a submission counts as device work when it is a kernel, hands over an event, or returns an incomplete or timestamped event | tests/engine/scheduler/test_dispatch_record.cpp; modules/engine/scheduler/include/SushiRuntime/scheduler/dispatch_record.hpp
Small_DispatchRecord.AnInlineHostNodeIsReady | keep | 9 | dispatch_record: an inline host node is pushed ready, holding its task and no event | tests/engine/scheduler/test_dispatch_record.cpp; modules/engine/scheduler/include/SushiRuntime/scheduler/dispatch_record.hpp; modules/engine/scheduler/include/SushiRuntime/scheduler/event_polling.hpp
Small_DispatchRecord.ASubmissionThatReachedNoDeviceIsReadyAndUnprofiled | split | 3 | the name joins three results that can break apart: ready with no event; the step, device index and handed-over events carried; no submitting thread when unprofiled; split into one test each | tests/engine/scheduler/test_dispatch_record.cpp; modules/engine/scheduler/include/SushiRuntime/scheduler/dispatch_record.hpp; modules/engine/scheduler/include/SushiRuntime/scheduler/event_polling.hpp
Small_DispatchRecord.ASubmissionThatReachedADeviceWaitsOnItsEvent | keep | 9 | dispatch_record: a submission that reached no device is pushed ready, and one that reached a device waits on its event | tests/engine/scheduler/test_dispatch_record.cpp; modules/engine/scheduler/include/SushiRuntime/scheduler/dispatch_record.hpp; modules/engine/scheduler/include/SushiRuntime/scheduler/event_polling.hpp
Medium_DispatchRecord.ACompleteProfiledEventStillCountsAsSubmitted | rewrite | 8 | the test makes its own sycl::queue, which medium forbids; the queue must come from the module's fixture base. The seam: submitted_device_work takes an event, so the complete event needs no device in the logic part | tests/engine/scheduler/test_dispatch_record.cpp; tests/engine/scheduler/README.md; modules/engine/scheduler/include/SushiRuntime/scheduler/dispatch_record.hpp
```

### `tests/engine/scheduler/test_event_polling.cpp`

```
Small_EventPolling.PushNeverDropsBeyondInlineCapacity | rewrite | 1 | control flow: a push loop in the body that only builds the input; rewrite with a named builder, keeping the order check: event_polling: ActiveEventList keeps every pushed event, in push order, across inline and spill storage | tests/engine/scheduler/test_event_polling.cpp; modules/engine/scheduler/include/SushiRuntime/scheduler/event_polling.hpp
Small_EventPolling.RemoveAtDrainsEveryEventExactlyOnce | rewrite | 1 | control flow: a push loop and a drain loop in the body; rewrite with a named builder and a named drain helper: event_polling: ActiveEventList drains to empty holding each pushed event exactly once | tests/engine/scheduler/test_event_polling.cpp; modules/engine/scheduler/include/SushiRuntime/scheduler/event_polling.hpp
Small_EventPolling.RemoveAcrossInlineSpillBoundary | rewrite | 1 | control flow: a push loop in the body that only builds the input; rewrite with a named builder: event_polling: remove_at moves the tail entry into the freed slot, also across the inline and spill boundary | tests/engine/scheduler/test_event_polling.cpp; modules/engine/scheduler/include/SushiRuntime/scheduler/event_polling.hpp
Small_EventPolling.RemoveAtEdgeCases | split | 3 | two promises: remove_at out of range is a no-op; removing the last entry moves nothing; split into one test each | tests/engine/scheduler/test_event_polling.cpp; modules/engine/scheduler/include/SushiRuntime/scheduler/event_polling.hpp
Small_EventPolling.KeepsTheFirstFailureAndLogsTheSecond | split | 3 | the name joins two results that can break apart: keep_first_failure keeps the first; it logs the second at Warn. Split into the keep promise and the log promise. The log part needs a seam, since LogCapture installs a process-wide sink (question 8) | tests/engine/scheduler/test_event_polling.cpp; modules/engine/scheduler/include/SushiRuntime/scheduler/event_polling.hpp; modules/foundation/test_runner/include/SushiRuntime/test_runner/log_capture.hpp
Small_EventPolling.EveryLaterFailureIsLoggedAndTheFirstStays | split | 3 | the name joins keeping the first and logging each later one; the logging half repeats the log promise of KeepsTheFirstFailureAndLogsTheSecond, so after the split it merges into those rows | tests/engine/scheduler/test_event_polling.cpp; modules/engine/scheduler/include/SushiRuntime/scheduler/event_polling.hpp; modules/foundation/test_runner/include/SushiRuntime/test_runner/log_capture.hpp
Medium_EventPolling.CountsAStallDrive | rewrite | 1 | control flow: a while loop with a poll bound and a spin on the stall count in the body; NoSuccessors is a stand-in written by the test for the local queue type poll_events takes, and the rewrite passes a real queue type: event_polling: a poll that stalls waits on its oldest event once and counts the stall | tests/engine/scheduler/test_event_polling.cpp; modules/engine/scheduler/include/SushiRuntime/scheduler/event_polling.hpp
Medium_EventPolling.AnEventThatCompletesIsNotAStallDrive | rewrite | 1 | NoSuccessors is a test-written stand-in for the local queue type; the rewrite uses a real queue type. The test needs no device or thread, so it is small: event_polling: a poll that finds progress counts no stall drive | tests/engine/scheduler/test_event_polling.cpp; modules/engine/scheduler/include/SushiRuntime/scheduler/event_polling.hpp
```

### `tests/engine/scheduler/test_failure_naming.cpp`

```
Small_FailureNaming.ATaskIsDescribedByItsName | rewrite | 1 | NamedTask is a test-written subclass that stands in for the repository's abstract Task; rewrite with RangeTask, which takes a name (range_task.hpp, constructor name argument): failure_naming: describe_task names a task by its name | tests/engine/scheduler/test_failure_naming.cpp; modules/engine/scheduler/include/SushiRuntime/scheduler/failure_naming.hpp; modules/engine/scheduler/include/SushiRuntime/scheduler/range_task.hpp
Small_FailureNaming.ANodeIsDescribedByNameAndIndex | keep | 9 | failure_naming: describe_task names a task by its name, and a node by its name and registration index | tests/engine/scheduler/test_failure_naming.cpp; modules/engine/scheduler/include/SushiRuntime/scheduler/failure_naming.hpp
Small_FailureNaming.AFailureKeepsItsCodeAndNamesTheTask | keep | 9 | failure_naming: name_failure keeps the cause's code and puts the task before the cause | tests/engine/scheduler/test_failure_naming.cpp; modules/engine/scheduler/include/SushiRuntime/scheduler/failure_naming.hpp
```

### `tests/engine/scheduler/test_offload_gate.cpp`

```
Small_OffloadGate.AnOffloadableNodeIsOfferedAndCounted | rewrite | 1 | AcceptingCoordinator is a test-written stand-in for OffloadCoordinator, an interface the repository owns (modules/engine/task/include/SushiRuntime/task/offload_coordinator.hpp). The port has no in-memory adapter in its module; the rewrite needs one there: offload_gate: an offloadable node is offered to the installed coordinator, and counted once accepted | tests/engine/scheduler/test_offload_gate.cpp; modules/engine/scheduler/include/SushiRuntime/scheduler/offload_gate.hpp; modules/engine/task/include/SushiRuntime/task/offload_coordinator.hpp
Small_OffloadGate.TheGateOffersNothingItMustNot | rewrite | 1 | AcceptingCoordinator stand-in for OffloadCoordinator, with no in-memory adapter in the task module; the test also joins four promises, which the rewrite splits: offload_gate: the gate offers nothing with no coordinator, for a non-node, a non-offloadable node or an aborting run | tests/engine/scheduler/test_offload_gate.cpp; modules/engine/scheduler/include/SushiRuntime/scheduler/offload_gate.hpp; modules/engine/task/include/SushiRuntime/task/offload_coordinator.hpp
Small_OffloadGate.PrepareReachesTheCoordinator | rewrite | 1 | AcceptingCoordinator stand-in for OffloadCoordinator, with no in-memory adapter in the task module; the test also passes a reinterpret_cast address as a PlacementRules, a fabricated value that a real rules object should replace: offload_gate: prepare reaches an installed coordinator with the rules it was given | tests/engine/scheduler/test_offload_gate.cpp; modules/engine/scheduler/include/SushiRuntime/scheduler/offload_gate.hpp; modules/engine/task/include/SushiRuntime/task/offload_coordinator.hpp
```

### `tests/engine/scheduler/test_quiescence_latch.cpp`

```
Medium_QuiescenceLatch.AnEmptyLatchDoesNotWait | rewrite | 1 | forbidden kind: no assertion, only SUCCEED(); no other test pins the empty-latch promise. Rewrite with a flag set before wait() and checked after it, in a small test since no thread is needed: quiescence_latch: an empty latch's wait returns at once | tests/engine/scheduler/test_quiescence_latch.cpp; modules/engine/scheduler/include/SushiRuntime/scheduler/quiescence_latch.hpp
Medium_QuiescenceLatch.TheWaiterWakesAfterTheLastRetire | rewrite | 1 | control flow: a for loop in the worker lambda, which the straight-line rule forbids; rewrite as three explicit retire steps. The thread hand-over is a boundary, so medium stays: quiescence_latch: a waiter wakes only after the last of the counted units retires | tests/engine/scheduler/test_quiescence_latch.cpp; modules/engine/scheduler/include/SushiRuntime/scheduler/quiescence_latch.hpp
```

### `tests/engine/scheduler/test_range_task.cpp`

```
Medium_RangeTaskTest.SplittableOnlyAboveGrain | merge | 4 | same promise as Medium_RangeTaskTest.IsSplittableAgreesWithSplitAroundTheThreshold, which checks the same threshold with other inputs in the same file; merge into its rows, which are small: range_task: a range splits only when the half it keeps reaches the grain of 1024 | tests/engine/scheduler/test_range_task.cpp; modules/engine/scheduler/include/SushiRuntime/scheduler/range_task.hpp
Medium_RangeTaskTest.SplitHandsOffDistinctChunksUntilExhausted | rewrite | 1 | control flow: a while loop with ASSERT inside, in the body; rewrite with a named builder that collects the chunks. The body reaches no device, so it is small: range_task: split hands out a distinct chunk each time until the owner is below the grain | tests/engine/scheduler/test_range_task.cpp; modules/engine/scheduler/include/SushiRuntime/scheduler/range_task.hpp
Medium_RangeTaskTest.IsSplittableAgreesWithSplitAroundTheThreshold | rewrite | 7 | a sign points to rows: loops over sizes and offsets in the body. Rewrite as one TEST_P over the (offset, size, expected) rows. No device is used, so the rows are small: range_task: is_splittable and split agree on both sides of the threshold, from any offset | tests/engine/scheduler/test_range_task.cpp; modules/engine/scheduler/include/SushiRuntime/scheduler/range_task.hpp
Medium_RangeTaskTest.SplitPartsCoverTheRangeExactlyOnce | rewrite | 1 | control flow: a for loop and a while loop in the body that build the parts; rewrite with a named builder. The test also makes a default sycl::queue in its body, which medium forbids. Split the coverage check (small, over split()) from the execute check (medium, through the queue): range_task: the parts of a split cover the range once | tests/engine/scheduler/test_range_task.cpp; modules/engine/scheduler/include/SushiRuntime/scheduler/range_task.hpp
Medium_RangeTaskDeathTest.AReversedRangeStopsAtConstruction | keep | 9 | range_task: a reversed range stops the process at construction. A death test forks a child process, so medium is its smallest size | tests/engine/scheduler/test_range_task.cpp; modules/engine/scheduler/include/SushiRuntime/scheduler/range_task.hpp
Medium_RangeTaskTest.AnEmptyRangeIsAcceptedAndDoesNotSplit | resize | 8 | range_task: an empty range is accepted and does not split. The decision is on values, with no device or thread, so the test belongs at Small_RangeTask; the public code (range_task.hpp, can_split and constructor) takes plain sizes | tests/engine/scheduler/test_range_task.cpp; modules/engine/scheduler/include/SushiRuntime/scheduler/range_task.hpp
```

### `tests/engine/scheduler/test_rebalancer_interfaces.cpp`

```
Small_RebalancerInterfaces.AStrategyMovesWorkThroughTheMigrator | delete | 1 | forbidden kinds: stand-ins for TelemetryProvider and TaskMigrator, and EvenOutStrategy, a strategy written by the test, which tests the test's own class. The promise is carried by the real ThermalProxyStrategy in tests/engine/scheduler/test_thermal_proxy_strategy.cpp::RescuesThrottledWorker | tests/engine/scheduler/test_rebalancer_interfaces.cpp; tests/engine/scheduler/test_thermal_proxy_strategy.cpp; modules/engine/scheduler/include/SushiRuntime/scheduler/rebalancer_interfaces.hpp; modules/engine/scheduler/include/SushiRuntime/scheduler/thermal_proxy_strategy.hpp
```

### `tests/engine/scheduler/test_run_outcome.cpp`

```
Small_RunOutcome.TheFirstFailureWins | split | 3 | two promises: the first failure is kept and aborts the run; clear_failure resets the failure and the abort | tests/engine/scheduler/test_run_outcome.cpp; modules/engine/scheduler/include/SushiRuntime/scheduler/run_outcome.hpp
Small_RunOutcome.CancellationAbortsWithoutAFailure | split | 3 | two promises joined by "and": cancel aborts without a failure; clear_cancellation lifts the abort | tests/engine/scheduler/test_run_outcome.cpp; modules/engine/scheduler/include/SushiRuntime/scheduler/run_outcome.hpp
```

### `tests/engine/scheduler/test_scheduler_log_category.cpp`

```
Small_SchedulerLogCategory.TheCategoryIsNamedAfterTheModule | delete | 1 | forbidden kinds: a static_assert run as a test with SUCCEED(), and a log category, which is trivial code. The module name is checked through a real record in tests/engine/scheduler/test_event_polling.cpp::Small_EventPolling.KeepsTheFirstFailureAndLogsTheSecond | tests/engine/scheduler/test_scheduler_log_category.cpp; tests/engine/scheduler/test_event_polling.cpp; modules/engine/scheduler/include/SushiRuntime/scheduler/scheduler_log_category.hpp
```

### `tests/engine/scheduler/test_task_scheduler.cpp`

```
Medium_NodePlacement.RegionlessNodesRunOnDeviceZeroAndAPlacedNodeOnItsDevice | rewrite | 1 | the fixture's stand-ins: SingleNodeTopology for TopologyProvider, which has no in-memory adapter (modules/platform/topology/include/SushiRuntime/topology/topology_provider.hpp), and SingleNodeDistance for NUMADistanceModel, which MatrixDistanceModel (modules/platform/topology/include/SushiRuntime/topology/matrix_distance_model.hpp) can replace. The fixture also makes device queues per test, which medium forbids; a host DeviceQueueSet does not exist yet, so the placement logic cannot be small without it: task_scheduler: a node with no region runs on entry 0, and a node placed on an entry runs on that entry | tests/engine/scheduler/test_task_scheduler.cpp; modules/platform/topology/include/SushiRuntime/topology/topology_provider.hpp; modules/platform/topology/include/SushiRuntime/topology/matrix_distance_model.hpp; modules/platform/device/include/SushiRuntime/device/device_queue_set.hpp; modules/engine/scheduler/include/SushiRuntime/scheduler/task_scheduler.hpp
Medium_NodePlacement.ANodeKeepsItsDeviceOnEveryStep | rewrite | 1 | the same fixture stand-ins as above; control flow: a step loop with EXPECT in the body: task_scheduler: a node keeps the entry it was first dispatched to on every later step | tests/engine/scheduler/test_task_scheduler.cpp; modules/platform/topology/include/SushiRuntime/topology/topology_provider.hpp; modules/platform/device/include/SushiRuntime/device/device_queue_set.hpp; modules/engine/scheduler/include/SushiRuntime/scheduler/task_scheduler.hpp
Medium_NodePlacement.AStatedPlacementInTheOwnersContextOutranksTheOwner | rewrite | 1 | the same fixture stand-ins; the device queues are made per test. The promise is logic on the owner and the stated entry; the rewrite uses an in-memory topology adapter and a host queue set: task_scheduler: a stated placement in the owner's context outranks the owner | tests/engine/scheduler/test_task_scheduler.cpp; modules/platform/topology/include/SushiRuntime/topology/topology_provider.hpp; modules/platform/device/include/SushiRuntime/device/device_queue_set.hpp; modules/engine/scheduler/include/SushiRuntime/scheduler/task_scheduler.hpp
Medium_NodePlacement.AnUnownedRegionLeavesTheStatedPlacementInForce | rewrite | 1 | the same fixture stand-ins and per-test device queues; the rewrite needs the in-memory adapters named above: task_scheduler: a node with no region runs on entry 0, and a node placed on an entry runs on that entry | tests/engine/scheduler/test_task_scheduler.cpp; modules/platform/topology/include/SushiRuntime/topology/topology_provider.hpp; modules/platform/device/include/SushiRuntime/device/device_queue_set.hpp
Medium_NodePlacement.APlacementPastTheDeviceSetFailsTheRun | rewrite | 1 | the same fixture stand-ins and per-test device queues; the message checks are good oracles and stay in the rewrite: task_scheduler: a placement past the device set fails the run with InvalidGraph, naming the node, the index and the device count | tests/engine/scheduler/test_task_scheduler.cpp; modules/platform/topology/include/SushiRuntime/topology/topology_provider.hpp; modules/platform/device/include/SushiRuntime/device/device_queue_set.hpp; modules/engine/scheduler/include/SushiRuntime/scheduler/task_scheduler.hpp
Medium_OffloadSeam.AbortedRunOffersNoNode | rewrite | 1 | CountingCoordinator is a test-written spy for OffloadCoordinator, which has no in-memory adapter in its module; the fixture also makes the default device queues per test: task_scheduler: a cancelled or failed run offers no node to the offload coordinator | tests/engine/scheduler/test_task_scheduler.cpp; modules/engine/task/include/SushiRuntime/task/offload_coordinator.hpp; modules/engine/scheduler/include/SushiRuntime/scheduler/task_scheduler.hpp
Medium_OffloadSeam.SkippedNodeNotReportedAsCompleted | rewrite | 1 | the same spy and per-test device queues: task_scheduler: a node that a run skipped, or whose work threw, is not reported as completed | tests/engine/scheduler/test_task_scheduler.cpp; modules/engine/task/include/SushiRuntime/task/offload_coordinator.hpp; modules/engine/scheduler/include/SushiRuntime/scheduler/task_scheduler.hpp
Medium_OffloadSeam.OnlyAcceptedOffersAreCounted | rewrite | 1 | the same spy and per-test device queues: task_scheduler: the offloaded count rises only for offers the coordinator accepts | tests/engine/scheduler/test_task_scheduler.cpp; modules/engine/task/include/SushiRuntime/task/offload_coordinator.hpp; modules/engine/scheduler/include/SushiRuntime/scheduler/task_scheduler.hpp
Medium_SchedulerIntegration.ExecutesExactlySubmitted | rewrite | 1 | forbidden kind: wait_for polls with sleep_for to synchronise. Wait on wait_quiescent() instead. The promise is only checked here (CleanLifecycleRound is deleted), so it is kept: task_scheduler: the scheduler runs exactly the tasks submitted | tests/engine/scheduler/test_task_scheduler.cpp; modules/engine/scheduler/include/SushiRuntime/scheduler/task_scheduler.hpp
Medium_SchedulerIntegration.PriorityBypass | rewrite | 1 | control flow: a while loop, an if and a for loop in the body, and a sleep_for poll. The window check depends on the order real threads finish in, so it is not deterministic: rewrite through the single-threaded executor seam (Architecture rule 6) so the order replays from a seed: task_scheduler: a late high-priority task completes within the first window of the pool | tests/engine/scheduler/test_task_scheduler.cpp; modules/engine/scheduler/include/SushiRuntime/scheduler/task_scheduler.hpp
Medium_SchedulerIntegration.CleanLifecycleRound | delete | 1 | forbidden kind: sleep_for poll to synchronise. The no-throw stop is in Medium_TaskScheduler.AbruptStopDrains, and the exact count is in Medium_SchedulerIntegration.ExecutesExactlySubmitted | tests/engine/scheduler/test_task_scheduler.cpp; modules/engine/scheduler/include/SushiRuntime/scheduler/task_scheduler.hpp
Medium_NumaStealTest.LocalVictimsPrecedeRemote | rewrite | 1 | control flow: an if that skips and a for loop with ASSERT and EXPECT in the body. The topology is read from the real machine (HwlocTopologyProvider through RemappingTopologyProvider), and the worker count is machine-dependent. Rewrite on a synthetic topology provider, which makes it small: task_scheduler: same-node steal victims precede remote victims | tests/engine/scheduler/test_task_scheduler.cpp; modules/engine/scheduler/include/SushiRuntime/scheduler/task_scheduler.hpp; modules/platform/topology/include/SushiRuntime/topology/topology_provider.hpp
Medium_NumaStealTest.FlatTopologyKeepsAllVictims | rewrite | 1 | control flow: an if that skips and a for loop with ASSERT in the body; the test also reads the real machine topology and checks three properties in one body (no self, no duplicate, in range). Rewrite as a small test on a synthetic one-node topology: task_scheduler: a flat topology gives each worker every other worker as a steal victim, once | tests/engine/scheduler/test_task_scheduler.cpp; modules/engine/scheduler/include/SushiRuntime/scheduler/task_scheduler.hpp
Medium_TaskScheduler.AbruptStopDrains | rewrite | 2 | the test asserts only that stop() does not throw; it reads no executed count, so a dropped task passes. Its promise has no other test, because ExecutesExactlySubmitted counts before stop. Add the executed count after stop: task_scheduler: stop right after submit drains every submitted task, without deadlock | tests/engine/scheduler/test_task_scheduler.cpp; modules/engine/scheduler/include/SushiRuntime/scheduler/task_scheduler.hpp
Medium_TaskScheduler.LogsAClampedWorkerCount | rewrite | 1 | stand-ins in SchedulerParts: NoCoreMapTopology for TopologyProvider and OneNodeDistance for NUMADistanceModel (MatrixDistanceModel is the real replacement). The clamp is decided on values and logged to the process-wide sink, so it is small once the clamp is a value function and the sink is injected (question 8): task_scheduler: a worker count past the pool maximum is cut to the maximum, with a warning that names both | tests/engine/scheduler/test_task_scheduler.cpp; modules/engine/scheduler/source/task_scheduler.cpp; modules/platform/topology/include/SushiRuntime/topology/topology_provider.hpp; modules/platform/topology/include/SushiRuntime/topology/matrix_distance_model.hpp; modules/foundation/test_runner/include/SushiRuntime/test_runner/log_capture.hpp
Medium_TaskScheduler.AWorkerCountInsideTheLimitsLogsNoWarning | rewrite | 1 | the same SchedulerParts stand-ins and the process-wide sink; the negative case belongs in the same small rows as LogsAClampedWorkerCount: task_scheduler: a worker count inside the limits logs no count warning | tests/engine/scheduler/test_task_scheduler.cpp; modules/engine/scheduler/source/task_scheduler.cpp; modules/platform/topology/include/SushiRuntime/topology/topology_provider.hpp; modules/foundation/test_runner/include/SushiRuntime/test_runner/log_capture.hpp
Medium_TaskScheduler.LogsAWorkerWhoseCoreHasNoNumaNode | rewrite | 1 | the same SchedulerParts stand-ins; control flow: a for loop and an if in the body; two pinned workers make the count machine-dependent, since the clamp of a pinned range applies on a machine with fewer cores: task_scheduler: a pinned worker whose core has no NUMA node maps to node 0 and logs a debug line | tests/engine/scheduler/test_task_scheduler.cpp; modules/engine/scheduler/source/task_scheduler.cpp; modules/platform/topology/include/SushiRuntime/topology/topology_provider.hpp; modules/foundation/test_runner/include/SushiRuntime/test_runner/log_capture.hpp
Medium_TaskScheduler.AFloatingPoolLogsNoCoreMapLine | rewrite | 1 | the same SchedulerParts stand-ins; control flow: a for loop with EXPECT in the body, which passes when nothing is captured. The rewrite must check the sink saw the constructor's own record before it asserts absence: task_scheduler: a floating pool logs no core-map line | tests/engine/scheduler/test_task_scheduler.cpp; modules/engine/scheduler/source/task_scheduler.cpp; modules/foundation/test_runner/include/SushiRuntime/test_runner/log_capture.hpp
Medium_TaskSchedulerCancel.LogsTheTasksASkippedRunLeftUnrun | rewrite | 1 | the same SchedulerParts stand-ins and the process-wide sink; control flow: a for loop and an if in the body, with a bool flag where the name says "one INFO". The rewrite asserts exactly one record: task_scheduler: a cancelled run logs one INFO that names the number of tasks it skipped | tests/engine/scheduler/test_task_scheduler.cpp; modules/engine/scheduler/source/task_scheduler.cpp; modules/foundation/test_runner/include/SushiRuntime/test_runner/log_capture.hpp
Medium_TaskSchedulerCancel.ARunNobodyCancelledLogsNoSkipLine | rewrite | 1 | the same SchedulerParts stand-ins; control flow: a for loop with EXPECT in the body, which passes when nothing is captured. The rewrite checks the sink saw the run's own records first: task_scheduler: a run nobody cancelled logs no skip line | tests/engine/scheduler/test_task_scheduler.cpp; modules/engine/scheduler/source/task_scheduler.cpp; modules/foundation/test_runner/include/SushiRuntime/test_runner/log_capture.hpp
```

### `tests/engine/scheduler/test_thermal_proxy_strategy.cpp`

```
Small_ThermalRebalanceTest.RescuesThrottledWorker | rewrite | 1 | FakeTelemetry and RecordingMigrator are test-written stand-ins for TelemetryProvider and TaskMigrator, which the repository owns. The module has no in-memory adapter for either (TaskScheduler is the only implementation), so the rewrite needs one in modules/engine/scheduler beside rebalancer_interfaces.hpp: thermal_proxy_strategy: a worker with a deep queue that completes at most 5 tasks is migrated half the gap to the shortest queue | tests/engine/scheduler/test_thermal_proxy_strategy.cpp; modules/engine/scheduler/include/SushiRuntime/scheduler/rebalancer_interfaces.hpp; modules/engine/scheduler/include/SushiRuntime/scheduler/thermal_proxy_strategy.hpp
Small_ThermalRebalanceTest.IgnoresFastWorkerWithLongQueue | rewrite | 1 | the same stand-ins and missing in-memory TelemetryProvider and TaskMigrator adapters: thermal_proxy_strategy: a deep queue that drains faster than 5 tasks is not migrated | tests/engine/scheduler/test_thermal_proxy_strategy.cpp; modules/engine/scheduler/include/SushiRuntime/scheduler/rebalancer_interfaces.hpp; modules/engine/scheduler/include/SushiRuntime/scheduler/thermal_proxy_strategy.hpp
Small_ThermalRebalanceTest.NoMigrationWhenBalanced | rewrite | 1 | the same stand-ins and missing adapters: thermal_proxy_strategy: balanced or shallow queues trigger no migration | tests/engine/scheduler/test_thermal_proxy_strategy.cpp; modules/engine/scheduler/include/SushiRuntime/scheduler/rebalancer_interfaces.hpp; modules/engine/scheduler/include/SushiRuntime/scheduler/thermal_proxy_strategy.hpp
Small_ThermalRebalanceTest.SingleWorkerIsNoOp | rewrite | 1 | the same stand-ins and missing adapters: thermal_proxy_strategy: a single worker triggers no migration | tests/engine/scheduler/test_thermal_proxy_strategy.cpp; modules/engine/scheduler/include/SushiRuntime/scheduler/rebalancer_interfaces.hpp; modules/engine/scheduler/include/SushiRuntime/scheduler/thermal_proxy_strategy.hpp
```

### `tests/engine/scheduler/test_worker_pool_settings.cpp`

```
Medium_WorkerPoolSettings.DefaultsAskForOneWorkerPerCore | delete | 1 | forbidden kind: a test of a default value of a plain struct, which is trivial code. The test states no behaviour; the behaviour of a count of 0 is added below under task_scheduler. tests/engine/execution/test_runtime_settings.cpp checks the defaults of RuntimeSettings, not of WorkerPoolSettings | tests/engine/scheduler/test_worker_pool_settings.cpp; modules/engine/scheduler/include/SushiRuntime/scheduler/worker_pool_settings.hpp; tests/engine/execution/test_runtime_settings.cpp
```

### `tests/engine/scheduler/test_workload_coordinator.cpp`

```
Medium_WorkloadCoordinatorTest.HeartbeatInvokesStrategy | rewrite | 1 | stand-ins: FakeTelemetry, FakeMigrator, and CountingStrategy, a spy for RebalanceStrategy. Forbidden sleeps: wait_until_at_least polls with sleep_for, and a 50 ms sleep proves no evaluation after stop. WorkloadCoordinator takes no clock (its constructor takes telemetry, migrator and strategy only), so the seam is a clock the test advances: workload_coordinator: the heartbeat evaluates the strategy repeatedly, and stop joins the loop | tests/engine/scheduler/test_workload_coordinator.cpp; modules/engine/scheduler/include/SushiRuntime/scheduler/workload_coordinator.hpp; modules/engine/scheduler/include/SushiRuntime/scheduler/rebalancer_interfaces.hpp
Medium_WorkloadCoordinatorTest.StrategyDrivesMigration | rewrite | 1 | the same stand-ins and sleep-based wait; the clock seam is needed here too: workload_coordinator: a migration the strategy requests reaches the migrator | tests/engine/scheduler/test_workload_coordinator.cpp; modules/engine/scheduler/include/SushiRuntime/scheduler/workload_coordinator.hpp; modules/engine/scheduler/include/SushiRuntime/scheduler/rebalancer_interfaces.hpp
Medium_WorkloadCoordinatorTest.SetStrategySwapsAtRuntime | rewrite | 1 | the same stand-ins, a sleep-based wait, and ASSERT on the wait: workload_coordinator: set_strategy redirects evaluations to the new strategy | tests/engine/scheduler/test_workload_coordinator.cpp; modules/engine/scheduler/include/SushiRuntime/scheduler/workload_coordinator.hpp; modules/engine/scheduler/include/SushiRuntime/scheduler/rebalancer_interfaces.hpp
Medium_WorkloadCoordinatorTest.DoubleStartIsIdempotent | rewrite | 2 | the same stand-ins. The test checks no thread count and wraps stop() in EXPECT_NO_THROW, so a second loop would pass. Its promise has no other test, so it is rewritten to count loops through the clock seam: workload_coordinator: a second start leaves one loop running | tests/engine/scheduler/test_workload_coordinator.cpp; modules/engine/scheduler/include/SushiRuntime/scheduler/workload_coordinator.hpp
```

### `tests/engine/task/test_dispatch_events.cpp`

```
Small_DispatchEventScope.CollectsOnlyWhileTheScopeIsOpen | keep | 9 | DispatchEventScope: an event handed over in an open scope is collected, one outside is not; the slot is the calling thread's own, so small sees it | tests/engine/task/test_dispatch_events.cpp; modules/engine/task/include/SushiRuntime/task/dispatch_events.hpp; modules/engine/task/source/dispatch_events.cpp
Small_DispatchEventScope.NestedScopesRestoreTheOuterOne | keep | 9 | DispatchEventScope: nested scopes collect their own events and restore the outer one | tests/engine/task/test_dispatch_events.cpp; modules/engine/task/include/SushiRuntime/task/dispatch_events.hpp; modules/engine/task/source/dispatch_events.cpp
Small_DispatchEventScope.AScopeBelongsToItsThread | resize | 8 | DispatchEventScope: only events handed over on the scope's own thread are collected; a second thread is needed to see it, so the smallest size is medium: relabel Medium_DispatchEventScope, no seam needed | tests/engine/task/test_dispatch_events.cpp; modules/engine/task/include/SushiRuntime/task/dispatch_events.hpp; modules/engine/task/source/dispatch_events.cpp
Small_DispatchEventScope.HandingOverWithNoScopeDoesNothing | delete | 1 | forbidden kind: EXPECT_NO_THROW around a call that cannot throw with no scope open; its promise (nothing is collected with no scope) is checked by Small_DispatchEventScope.CollectsOnlyWhileTheScopeIsOpen | tests/engine/task/test_dispatch_events.cpp; modules/engine/task/include/SushiRuntime/task/dispatch_events.hpp
```

### `tests/engine/task/test_late_bindings.cpp`

```
Small_LateBindings.DefaultsUseTheBakedWidth | delete | 1 | forbidden kinds: default values and a static_assert run as a test; its promise (default count uses the baked width) is checked by Small_NodeWidths.ALateCountSetsTheLiveWidth, which is split | tests/engine/task/test_late_bindings.cpp; tests/engine/task/test_node_widths.cpp; modules/engine/task/include/SushiRuntime/task/late_bindings.hpp
```

### `tests/engine/task/test_node.cpp`

```
Small_Node.ASuccessorIsReadyAfterItsLastPredecessor | keep | 9 | Node: a successor becomes ready when its last predecessor completes | tests/engine/task/test_node.cpp; modules/engine/task/include/SushiRuntime/task/node.hpp; modules/engine/task/include/SushiRuntime/task/ready_sink.hpp
Small_Node.SuccessorsPastTheInlineCapacityAreKept | rewrite | 1 | forbidden kind: control flow in the body (one loop builds the successors, another asserts each count); it is the only test of successors past K_MAX_INLINE_SUCCESSORS; rewrite with a builder named for what it builds | tests/engine/task/test_node.cpp; modules/engine/task/include/SushiRuntime/task/node.hpp
Small_Node.ArmingAddsTheIntraAndBoundaryCounts | keep | 9 | Node: arm_in_degree sets in_degree to intra plus boundary; the region fields are public members of the struct | tests/engine/task/test_node.cpp; modules/engine/task/include/SushiRuntime/task/node.hpp; modules/engine/task/include/SushiRuntime/task/region_link.hpp
Small_Node.PlacementAndOffloadStateStartEmpty | rewrite | 1 | forbidden kind: constant checks (is_graph_node and is_splittable return constants); rewrite keeps the offload() memoisation, the set_live_io storage and the placement half, which stays here because Medium_NodePlacementState.ANodeStatesNoPlacementUntilPlaceOn is deleted at question 4 | tests/engine/task/test_node.cpp; tests/engine/task/test_node_placement.cpp; modules/engine/task/include/SushiRuntime/task/node.hpp; modules/engine/task/include/SushiRuntime/task/offload_state.hpp
```

### `tests/engine/task/test_node_placement.cpp`

```
Medium_NodePlacementState.ANodeStatesNoPlacementUntilPlaceOn | delete | 4 | Node: placement is unstated until place_on; the same promise is checked by Small_Node.PlacementAndOffloadStateStartEmpty in tests/engine/task/test_node.cpp, the Node's own file; this copy is the larger one and uses no device | tests/engine/task/test_node_placement.cpp; tests/engine/task/test_node.cpp; modules/engine/task/include/SushiRuntime/task/node.hpp
Medium_NodePlacementStateDeathTest.PlaceOnStopsOnAnIndexItCannotHold | keep | 9 | Node: place_on aborts, naming the index, for an index at or past K_NO_PLACEMENT; a child process is the smallest that sees an abort; the earlier placement stays. tests/engine/task/README.md lines 19 to 21 say it throws, which the code does not do | tests/engine/task/test_node_placement.cpp; tests/engine/task/README.md; modules/engine/task/include/SushiRuntime/task/node.hpp; modules/foundation/core/include/SushiRuntime/core/assert.hpp
Medium_NodePlacementRule.ANodeWithNoRegionRunsOnItsStatedDeviceElseOnTheDefault | split | 3 | two promises: a node with no owned region runs on device 0 when none is stated; a stated device wins with no region | tests/engine/task/test_node_placement.cpp; modules/engine/task/include/SushiRuntime/task/node_placement.hpp; modules/engine/task/source/node_placement.cpp
Medium_NodePlacementRule.TheFirstWriteRegionOutranksTheFirstReadRegion | split | 3 | two promises: a node whose only region is a read runs on that owner; the owner of the first write outranks the owner of the first read | tests/engine/task/test_node_placement.cpp; modules/engine/task/include/SushiRuntime/task/node_placement.hpp; modules/engine/task/source/node_placement.cpp
Medium_NodePlacementRule.AnOwnedRegionPlacesANodeThatStatesNothing | rewrite | 8 | placement_of: an owned region places a node that states nothing; the size is right, but the fixture constructor makes two queues, which medium forbids in a fixture; rewrite on the module's one-per-process device base | tests/engine/task/test_node_placement.cpp; tests/engine/task/README.md; modules/engine/task/include/SushiRuntime/task/node_placement.hpp; modules/engine/task/source/node_placement.cpp
Medium_NodePlacementRule.AStatedDeviceInTheOwnersContextOutranksTheOwner | rewrite | 8 | placement_of: a stated device in the owner's context outranks the owner; the size is right, but the fixture makes the queues; rewrite on the module's one-per-process device base | tests/engine/task/test_node_placement.cpp; tests/engine/task/README.md; modules/engine/task/include/SushiRuntime/task/node_placement.hpp; modules/engine/task/source/node_placement.cpp
Medium_NodePlacementContexts.AStatedDeviceInAnotherContextIsRefused | split | 3 | two promises: a stated device in another SYCL context is refused with InvalidGraph naming the owner; stating the owner itself is accepted | tests/engine/task/test_node_placement.cpp; modules/engine/task/include/SushiRuntime/task/node_placement.hpp; modules/engine/task/source/node_placement.cpp
Medium_NodePlacementContexts.AStatedIndexPastTheDevicesIsLeftToTheDispatch | rewrite | 8 | placement_of: a stated index past the devices is returned as stated; the size is right, but the helper makes queues in the test body, which medium forbids; rewrite on the module's device base | tests/engine/task/test_node_placement.cpp; tests/engine/task/README.md; modules/engine/task/source/node_placement.cpp
Medium_NodePlacementRule.AnUnownedRegionLeavesTheStatedPlacementInForce | split | 3 | two promises: an unowned region with nothing stated runs on device 0; a stated device stands for an unowned region | tests/engine/task/test_node_placement.cpp; modules/engine/task/include/SushiRuntime/task/node_placement.hpp; modules/engine/task/source/node_placement.cpp
```

### `tests/engine/task/test_node_profiling.cpp`

```
Small_NodeProfiling.CountersStartAtZeroAndAccumulate | delete | 1 | forbidden kinds: default values; the accumulate line tests std::atomic arithmetic, not NodeProfiling, which is a struct of three atomics with no method | tests/engine/task/test_node_profiling.cpp; modules/engine/task/include/SushiRuntime/task/node_profiling.hpp
```

### `tests/engine/task/test_node_widths.cpp`

```
Small_NodeWidths.ABakedNodeReportsItsWidthTwice | split | 3 | two promises: a node with a baked width and no bindings reports it as launched and live; a node with no baked width reports unknown | tests/engine/task/test_node_widths.cpp; modules/engine/task/include/SushiRuntime/task/node_widths.hpp; modules/engine/task/source/node_widths.cpp; modules/engine/task/include/SushiRuntime/task/node.hpp
Small_NodeWidths.ALateCountSetsTheLiveWidth | split | 3 | two promises: default bindings report the baked width as live; a late count sets live and leaves launched alone | tests/engine/task/test_node_widths.cpp; modules/engine/task/include/SushiRuntime/task/node_widths.hpp; modules/engine/task/source/node_widths.cpp; modules/engine/task/include/SushiRuntime/task/late_bindings.hpp
Small_NodeWidths.AWidthBeyondThirtyTwoBitsIsUnknown | keep | 9 | snapshot_widths: a width past 32 bits is reported as unknown | tests/engine/task/test_node_widths.cpp; modules/engine/task/include/SushiRuntime/task/node_widths.hpp; modules/engine/task/source/node_widths.cpp
```

### `tests/engine/task/test_offload_coordinator.cpp`

```
Small_OffloadCoordinator.EveryMemberReachesTheImplementation | rewrite | 1 | forbidden kind: a recorder the test writes for OffloadCoordinator, a repository port; the rewrite needs an in-memory OffloadCoordinator in modules/engine/task, and a grep of modules/ finds none; the test also casts a shared token's address to an incomplete type | tests/engine/task/test_offload_coordinator.cpp; modules/engine/task/include/SushiRuntime/task/offload_coordinator.hpp; modules/engine/task/include/SushiRuntime/task/offload_sink.hpp
```

### `tests/engine/task/test_offload_sink.cpp`

```
Small_OffloadSink.BothCallsReachTheSink | rewrite | 1 | forbidden kind: a recorder the test writes for OffloadSink; the class is a copy of the one in test_offload_coordinator.cpp; the rewrite needs the in-memory OffloadSink adapter in modules/engine/task | tests/engine/task/test_offload_sink.cpp; tests/engine/task/test_offload_coordinator.cpp; modules/engine/task/include/SushiRuntime/task/offload_sink.hpp
Small_OffloadSink.FailReachesTheSinkWithTheError | rewrite | 1 | forbidden kind: the same recorder; the rewrite needs the in-memory OffloadSink adapter named above | tests/engine/task/test_offload_sink.cpp; tests/engine/task/test_offload_coordinator.cpp; modules/engine/task/include/SushiRuntime/task/offload_sink.hpp
```

### `tests/engine/task/test_offload_state.cpp`

```
Small_OffloadState.DefaultsHoldNothingAndThePayloadIsOwned | rewrite | 1 | forbidden kinds: default values checked (offloadable, attempts, lists) and a stub, CountingPayload, of the repository's OffloadPayload; the ownership promise stays, with a payload type the task module owns (none ships; OffloadHandles in modules/extension/offload_coordination is one, found by grep, but an engine test cannot reach it) | tests/engine/task/test_offload_state.cpp; modules/engine/task/include/SushiRuntime/task/offload_state.hpp; modules/extension/offload_coordination/include/SushiRuntime/offload_coordination/offload_handles.hpp (grep)
```

### `tests/engine/task/test_ready_sink.cpp`

```
Small_ReadySink.IsAnOrderedListOfTasks | delete | 1 | forbidden kind: a static_assert run as a test, comparing an alias with itself; the rest checks std::vector; ReadySink states no promise of its own | tests/engine/task/test_ready_sink.cpp; modules/engine/task/include/SushiRuntime/task/ready_sink.hpp
```

### `tests/engine/task/test_region_link.cpp`

```
Small_RegionLink.ClearDropsTheBoundaryOnly | keep | 9 | RegionLink::clear drops the boundary layer and keeps the intra count | tests/engine/task/test_region_link.cpp; modules/engine/task/include/SushiRuntime/task/region_link.hpp
```

### `tests/engine/task/test_small_event_vector.cpp`

```
Small_SmallEventVector.ADefaultVectorIsEmpty | delete | 1 | forbidden kind: default value; states no promise beyond the constructor default | tests/engine/task/test_small_event_vector.cpp; modules/engine/task/include/SushiRuntime/task/small_event_vector.hpp (grep)
Small_SmallEventVector.ACopyHoldsTheSameEventsInlineAndOverflow | keep | 9 | SmallEventVector: a copy holds the same events, inline and past K_STATIC_CAP | tests/engine/task/test_small_event_vector.cpp; modules/engine/task/include/SushiRuntime/task/small_event_vector.hpp (grep)
Small_SmallEventVector.AMoveHoldsTheEventsAndEmptiesTheSource | keep | 9 | SmallEventVector: a move holds the events and empties the source; dynamic_data is a public member of the struct, so its emptiness is part of the public promise | tests/engine/task/test_small_event_vector.cpp; modules/engine/task/include/SushiRuntime/task/small_event_vector.hpp (grep)
Small_SmallEventVector.ACopyAssignmentReplacesEveryPriorEvent | merge | 4 | merge with Small_SmallEventVector.AssignmentFromAVectorShrinksPastTheOldContent: the same promise, assignment replaces prior events, with the other input kind (a SmallEventVector, not a std::vector); the merged test takes rows over both sources | tests/engine/task/test_small_event_vector.cpp; modules/engine/task/include/SushiRuntime/task/small_event_vector.hpp (grep)
Small_SmallEventVector.AMoveAssignmentReplacesEveryPriorEventAndEmptiesTheSource | keep | 9 | SmallEventVector: move assignment replaces prior events and leaves the source empty; the emptied source is part of one outcome, as with a refusal that leaves nothing behind | tests/engine/task/test_small_event_vector.cpp; modules/engine/task/include/SushiRuntime/task/small_event_vector.hpp (grep)
Small_SmallEventVector.AssignmentFromAVectorShrinksPastTheOldContent | merge | 4 | merge with Small_SmallEventVector.ACopyAssignmentReplacesEveryPriorEvent: the same promise with a std::vector source; one test with rows over both input kinds | tests/engine/task/test_small_event_vector.cpp; modules/engine/task/include/SushiRuntime/task/small_event_vector.hpp (grep)
Small_SmallEventVector.AssignmentPastTheInlineCapacityKeepsEveryElementInOrder | rewrite | 1 | forbidden kind: a for loop in the test body; to_vector equality already checks size and order; rewrite drops the loop, or gives iteration order its own row | tests/engine/task/test_small_event_vector.cpp; modules/engine/task/include/SushiRuntime/task/small_event_vector.hpp (grep)
Small_SmallEventVector.ClearEmptiesTheVectorAndItAcceptsNewEvents | split | 3 | two promises: clear empties the vector; a cleared vector accepts new events | tests/engine/task/test_small_event_vector.cpp; modules/engine/task/include/SushiRuntime/task/small_event_vector.hpp (grep)
Small_SmallEventVector.SelfAssignmentKeepsEveryEvent | split | 3 | two promises: self copy assignment keeps every event; self move assignment keeps every event | tests/engine/task/test_small_event_vector.cpp; modules/engine/task/include/SushiRuntime/task/small_event_vector.hpp (grep)
Small_SmallEventVector.AssignmentFromItsOwnOverflowKeepsThatList | keep | 9 | SmallEventVector: assignment from its own dynamic_data keeps that list, an aliasing case | tests/engine/task/test_small_event_vector.cpp; modules/engine/task/include/SushiRuntime/task/small_event_vector.hpp (grep)
```

### `tests/engine/task/test_task.cpp`

```
Small_Task.ASubclassInheritsTheDefaults | rewrite | 1 | forbidden kinds: constant checks (name "unnamed_task", execution mode, priority, submits_device_work) and a stub subclass, PlainTask; keep only the on_complete check (a task with no successor readies nothing), on the concrete Node instead of the stub | tests/engine/task/test_task.cpp; modules/engine/task/include/SushiRuntime/task/node.hpp; modules/engine/task/include/SushiRuntime/task/task.hpp (grep)
```

### `tests/engine/task/test_task_submission.cpp`

```
Medium_TaskSubmission.AHostTaskGetsItsDependenciesAndHandsItsEventOver | rewrite | 1 | forbidden kind: RecordingTask, a stand-in the test writes for the repository's Task interface, counting runs; the queue is also made in the body, which medium forbids; rewrite with a real host task on the module's device base | tests/engine/task/test_task_submission.cpp; modules/engine/task/include/SushiRuntime/task/task_submission.hpp; modules/engine/task/source/task_submission.cpp
Medium_TaskSubmission.ANullSinkCollectsNothing | rewrite | 1 | forbidden kind: RecordingTask stand-in; rewrite with a real task on the module's device base | tests/engine/task/test_task_submission.cpp; modules/engine/task/include/SushiRuntime/task/task_submission.hpp; modules/engine/task/source/task_submission.cpp
Medium_TaskSubmission.AKernelTaskReturnsAnEvent | rewrite | 1 | forbidden kind: RecordingTask stand-in; the queue is made in the body; rewrite with a real kernel task on the module's device base | tests/engine/task/test_task_submission.cpp; modules/engine/task/include/SushiRuntime/task/task_submission.hpp; modules/engine/task/source/task_submission.cpp
Medium_TaskSubmission.TheDiscardingPathRunsEitherKind | rewrite | 1 | forbidden kind: RecordingTask stand-in; rewrite with real tasks on the module's device base | tests/engine/task/test_task_submission.cpp; modules/engine/task/include/SushiRuntime/task/task_submission.hpp; modules/engine/task/source/task_submission.cpp
```

### `tests/engine/task/test_task_types.cpp`

```
Small_TaskTypes.AParameterSlotReturnsTheValueStoredInIt | keep | 9 | TaskMetadata: a parameter slot returns the typed value stored in it, first and last of K_MAX_PARAMS | tests/engine/task/test_task_types.cpp; modules/engine/task/include/SushiRuntime/task/task_types.hpp
```

### `tests/engine/task/test_task_work.cpp`

```
Medium_TaskWork.HostWorkRunsOnAQueue | delete | 2 | HostWork is a std::function alias, so the test checks the standard library's forwarding; the host-task promise is held by the host path of submit_task, which Medium_TaskSubmission.AHostTaskGetsItsDependenciesAndHandsItsEventOver checks | tests/engine/task/test_task_work.cpp; modules/engine/task/include/SushiRuntime/task/task_work.hpp; modules/engine/task/source/task_submission.cpp
Medium_TaskWork.TaskWorkRunsInsideASubmit | delete | 2 | TaskWork is a std::function alias; a command group running inside a submit is held by the kernel path of submit_task, which Medium_TaskSubmission.AKernelTaskReturnsAnEvent checks | tests/engine/task/test_task_work.cpp; modules/engine/task/include/SushiRuntime/task/task_work.hpp; modules/engine/task/source/task_submission.cpp
```

### `tests/engine/work_stealing/test_work_stealing_deque.cpp`

```
Medium_WorkStealingDeque.PushBeyondCapacityParksOverflow | rewrite | 1 | forbidden kind: a loop in the body builds the input; it is the only test of the overflow count; rewrite with a builder named for what it builds, at small size, since no thread, device or file is reached | tests/engine/work_stealing/test_work_stealing_deque.cpp; modules/engine/work_stealing/include/SushiRuntime/work_stealing/work_stealing_deque.hpp
Medium_WorkStealingDeque.PopDrainsOverflowExactlyOnce | delete | 1 | forbidden kinds: loops in the body (one builds, one drains); the promise (each pushed task returns once past capacity) is tested in the same module by Medium_WorkStealingDeque.NoLossUnderConcurrentSteal, which includes the owner's pop; the single-thread form is added below | tests/engine/work_stealing/test_work_stealing_deque.cpp; modules/engine/work_stealing/include/SushiRuntime/work_stealing/work_stealing_deque.hpp
Medium_WorkStealingDeque.NoLossUnderConcurrentSteal | rewrite | 1 | forbidden kind: control flow in the body (loops, branches and the thread spawn); it is the only test of exactly-once under concurrent steal; rewrite with the owner and stealer loops in named helpers; the threads keep it medium | tests/engine/work_stealing/test_work_stealing_deque.cpp; modules/engine/work_stealing/include/SushiRuntime/work_stealing/work_stealing_deque.hpp
```

### `tests/extension/buffer_exchange/test_buffer_exchange.cpp`

```
Medium_BufferExchangeTest.ChunkedRoundTrip | rewrite | 1 | forbidden kind: a for loop over the boundary sizes and a reassembly loop in the body; rows of a TEST_P over K_BOUNDARY_SIZES; the promise stays, and the size is small since the registry and exchange touch only host memory | tests/extension/buffer_exchange/test_buffer_exchange.cpp; modules/extension/buffer_exchange/include/SushiRuntime/buffer_exchange/buffer_exchange.hpp; modules/extension/buffer_exchange/include/SushiRuntime/buffer_exchange/buffer_registry.hpp; modules/extension/buffer_exchange/source/buffer_exchange.cpp (grep)
Medium_BufferExchangeTest.EveryStreamEndsWithALastChunk | rewrite | 1 | forbidden kind: a for loop over sizes and a for loop over frames in the body; rows of a TEST_P over K_BOUNDARY_SIZES | tests/extension/buffer_exchange/test_buffer_exchange.cpp; modules/extension/buffer_exchange/include/SushiRuntime/buffer_exchange/buffer_exchange.hpp; modules/extension/buffer_exchange/include/SushiRuntime/buffer_exchange/buffer_registry.hpp
Medium_BufferExchangeTest.ForgedVersionFenced | rewrite | 7 | the promise (a chunk for a version other than the current writes nothing) is one promise, but three calls with three inputs (ahead, behind, bumped in flight) carry it; those are rows of one promise, which is a sign other than size | tests/extension/buffer_exchange/test_buffer_exchange.cpp; modules/extension/buffer_exchange/include/SushiRuntime/buffer_exchange/buffer_exchange.hpp
Medium_BufferExchangeTest.GapDiscardsTransfer | split | 3 | two promises: a gap discards the transfer and refuses later pieces; after the gap the transfer restarts from piece 0 and completes | tests/extension/buffer_exchange/test_buffer_exchange.cpp; modules/extension/buffer_exchange/include/SushiRuntime/buffer_exchange/buffer_exchange.hpp
Medium_BufferExchangeTest.DuplicateChunkDiscardsTransfer | resize | 8 | BufferExchange: a duplicate chunk discards the transfer; the logic touches host memory only (no thread, clock, file or process-wide instance in buffer_exchange.hpp, buffer_registry.hpp, or buffer_exchange.cpp by grep), so the smallest size is small: relabel Small_ | tests/extension/buffer_exchange/test_buffer_exchange.cpp; modules/extension/buffer_exchange/include/SushiRuntime/buffer_exchange/buffer_exchange.hpp; modules/extension/buffer_exchange/include/SushiRuntime/buffer_exchange/buffer_registry.hpp; modules/extension/buffer_exchange/source/buffer_exchange.cpp (grep)
Medium_BufferExchangeTest.ReleaseDuringStreamAborts | split | 3 | two promises: releasing a buffer during a stream aborts the stream with Gone; a pull of an unknown buffer is refused with Gone | tests/extension/buffer_exchange/test_buffer_exchange.cpp; modules/extension/buffer_exchange/include/SushiRuntime/buffer_exchange/buffer_exchange.hpp; modules/extension/buffer_exchange/include/SushiRuntime/buffer_exchange/buffer_registry.hpp
Medium_BufferExchangeTest.StaleVersionAbortsThePull | split | 3 | two promises: a bump during a stream aborts it with Stale; a pull naming a version already left gets the abort alone | tests/extension/buffer_exchange/test_buffer_exchange.cpp; modules/extension/buffer_exchange/include/SushiRuntime/buffer_exchange/buffer_exchange.hpp; modules/extension/buffer_exchange/include/SushiRuntime/buffer_exchange/buffer_registry.hpp
Medium_BufferExchangeTest.MalformedChunkIsRefusedBeforeAnyWrite | rewrite | 7 | the promise (a malformed chunk is refused before any write) holds for four inputs, which are rows of one promise; the sign is several calls with different inputs, not a size sign | tests/extension/buffer_exchange/test_buffer_exchange.cpp; modules/extension/buffer_exchange/include/SushiRuntime/buffer_exchange/buffer_exchange.hpp
Medium_BufferExchangeTest.ForgottenTaskStartsOver | split | 3 | two promises: forgetting a task discards its transfer, so the next piece is a gap; a continuation from another connection is refused | tests/extension/buffer_exchange/test_buffer_exchange.cpp; modules/extension/buffer_exchange/include/SushiRuntime/buffer_exchange/buffer_exchange.hpp
```

### `tests/extension/buffer_exchange/test_buffer_registry.cpp`

```
Medium_BufferRegistryTest.RegisteringASharedAddressKeepsItsHandle | split | 3 | two promises: an address already shared keeps its handle; distinct addresses get distinct ids | tests/extension/buffer_exchange/test_buffer_registry.cpp; modules/extension/buffer_exchange/include/SushiRuntime/buffer_exchange/buffer_registry.hpp
Medium_BufferRegistryTest.BumpRaisesTheVersionAndSkipsAGoneID | split | 3 | two promises: bump raises the version of live handles and leaves a gone one alone; bump_addresses raises the version of a shared address | tests/extension/buffer_exchange/test_buffer_registry.cpp; modules/extension/buffer_exchange/include/SushiRuntime/buffer_exchange/buffer_registry.hpp
Medium_BufferRegistryTest.ResolveAndRefreshFollowTheCurrentVersion | split | 3 | two promises: resolve maps addresses and refuses an unknown one; refresh follows the current version and refuses a gone id | tests/extension/buffer_exchange/test_buffer_registry.cpp; modules/extension/buffer_exchange/include/SushiRuntime/buffer_exchange/buffer_registry.hpp
Medium_BufferRegistryTest.ReadAtAnOffsetCopiesExactlyTheChunk | split | 3 | two promises: read copies exactly the requested chunk; read refuses an out-of-range, stale or gone request and copies nothing | tests/extension/buffer_exchange/test_buffer_registry.cpp; modules/extension/buffer_exchange/include/SushiRuntime/buffer_exchange/buffer_registry.hpp
Medium_BufferRegistryTest.WriteAtAnOldVersionIsRefused | split | 3 | two promises: a write at a stale version is refused and writes nothing; an in-range write at the current version lands in place | tests/extension/buffer_exchange/test_buffer_registry.cpp; modules/extension/buffer_exchange/include/SushiRuntime/buffer_exchange/buffer_registry.hpp
Medium_BufferRegistryTest.UnregisterReturnsTheID | split | 3 | three promises: unregister returns the id once; reads and writes after it are Gone; an address shared again gets a new id | tests/extension/buffer_exchange/test_buffer_registry.cpp; modules/extension/buffer_exchange/include/SushiRuntime/buffer_exchange/buffer_registry.hpp
```

### `tests/extension/cluster_load/test_cluster_statistics.cpp`

```
Medium_ClusterStatistics.StartAtZero | delete | 1 | forbidden kind: default values; states no promise beyond the defaults | tests/extension/cluster_load/test_cluster_statistics.cpp; modules/extension/cluster_load/include/SushiRuntime/cluster_load/cluster_statistics.hpp
```

### `tests/extension/cluster_load/test_offload_policy.cpp`

```
Medium_OffloadPolicy.PolicyKeepsTinyPayloadLocal | resize | 8 | LeastLoadedOffloadPolicy: a payload below the byte gate stays local; the policy reads values only (no clock, file, thread or global in offload_policy.cpp), so the smallest size is small: relabel Small_ | tests/extension/cluster_load/test_offload_policy.cpp; modules/extension/cluster_load/include/SushiRuntime/cluster_load/offload_policy.hpp; modules/extension/cluster_load/source/offload_policy.cpp
Medium_OffloadPolicy.PolicyPicksLowerRttWorker | resize | 8 | LeastLoadedOffloadPolicy: the lower round trip wins at equal load; pure value logic, so small: relabel Small_ | tests/extension/cluster_load/test_offload_policy.cpp; modules/extension/cluster_load/include/SushiRuntime/cluster_load/offload_policy.hpp; modules/extension/cluster_load/source/offload_policy.cpp
Medium_OffloadPolicy.PolicyPrefersLessLoadedWhenEquallyNear | resize | 8 | LeastLoadedOffloadPolicy: the less loaded worker wins at equal round trip; pure value logic, so small: relabel Small_ | tests/extension/cluster_load/test_offload_policy.cpp; modules/extension/cluster_load/include/SushiRuntime/cluster_load/offload_policy.hpp; modules/extension/cluster_load/source/offload_policy.cpp
Medium_OffloadPolicy.PolicyRejectsInsufficientHeadroom | resize | 8 | LeastLoadedOffloadPolicy: a worker with less free memory than the payload is not picked; pure value logic, so small: relabel Small_ | tests/extension/cluster_load/test_offload_policy.cpp; modules/extension/cluster_load/include/SushiRuntime/cluster_load/offload_policy.hpp; modules/extension/cluster_load/source/offload_policy.cpp
Medium_OffloadPolicy.PolicyFallsBackLocalWhenNoLiveWorker | resize | 8 | LeastLoadedOffloadPolicy: with no live worker nothing is picked; pure value logic, so small: relabel Small_ | tests/extension/cluster_load/test_offload_policy.cpp; modules/extension/cluster_load/include/SushiRuntime/cluster_load/offload_policy.hpp; modules/extension/cluster_load/source/offload_policy.cpp
Medium_OffloadPolicy.PolicyTreatsZeroFreeMemAsUnknown | resize | 8 | LeastLoadedOffloadPolicy: zero free memory counts as unknown and keeps the worker; pure value logic, so small: relabel Small_ | tests/extension/cluster_load/test_offload_policy.cpp; modules/extension/cluster_load/include/SushiRuntime/cluster_load/offload_policy.hpp; modules/extension/cluster_load/source/offload_policy.cpp
```

### `tests/extension/cluster_load/test_placement_decision.cpp`

```
Small_PlacementDecision.DefaultIsAWorkerOnDeviceZeroWithNoId | delete | 1 | forbidden kind: default values; the header holds no decision logic, so the default states no promise beyond its value | tests/extension/cluster_load/test_placement_decision.cpp; modules/extension/cluster_load/include/SushiRuntime/cluster_load/placement_decision.hpp
Small_PlacementDecision.WaitAndFailCarryTheirFacts | delete | 1 | forbidden kind: a getter-style round trip of the fields a variant alternative carries; no logic in the module reads them, and std::variant is the standard library's | tests/extension/cluster_load/test_placement_decision.cpp; modules/extension/cluster_load/include/SushiRuntime/cluster_load/placement_decision.hpp
```

### `tests/extension/dispatch_deduplication/test_dispatch_deduplicator.cpp`

```
Medium_DispatchDeduplicatorTest.DuplicateDispatchRunsOnce | split | 3 | two promises: a remembered id is refused on repeat; the capacity bound forgets the oldest id | tests/extension/dispatch_deduplication/test_dispatch_deduplicator.cpp; modules/extension/dispatch_deduplication/include/SushiRuntime/dispatch_deduplication/dispatch_deduplicator.hpp; modules/extension/dispatch_deduplication/source/dispatch_deduplicator.cpp
Medium_DispatchDeduplicatorTest.ClearForgetsEveryId | resize | 8 | DispatchDeduplicator: clear forgets every id; the mutex is the object's own and no thread is reached, so the smallest size is small: relabel Small_ | tests/extension/dispatch_deduplication/test_dispatch_deduplicator.cpp; modules/extension/dispatch_deduplication/include/SushiRuntime/dispatch_deduplication/dispatch_deduplicator.hpp; modules/extension/dispatch_deduplication/source/dispatch_deduplicator.cpp
```

### `tests/extension/distributed/benchmark_cluster.cpp`

```
Performance_DistributedOffloadBench.OffloadRoundTripThroughput | rewrite | 7 | the name is a measurement, not a promise (Throughput), a sign that goes to question 7; the suite prefix Performance_ opens neither a size nor Benchmark_, which tools/testing/check_test_sizes.py (rule_unsized_cpp_suite) flags; the test asserts correct output and offloading, the timing is a benchmark that belongs under `<cli> bench` (testing, Tests, benchmarks, probes and programs); rewrite as a sized test named for the promise (an offloaded batch returns correct outputs) with the timing moved out | tests/extension/distributed/benchmark_cluster.cpp; tools/testing/check_test_sizes.py
Performance_DistributedOffloadBench.RoundTripByBufferSize | rewrite | 7 | the name is a measurement, not a promise (sign to question 7); a loop over transports and sizes in the body (rows); the Performance_ prefix is flagged by check_test_sizes.py as above; the correctness and offloading checks become rows of a sized test; the timing moves to `<cli> bench` | tests/extension/distributed/benchmark_cluster.cpp; tools/testing/check_test_sizes.py
```

### `tests/extension/distributed/test_buffer_view.cpp`

```
Medium_BufferView.AsTypesTheAddress | split | 3 | BufferView: as<T> types the address; a default view names nothing; two results joined by "and", each part small | tests/extension/distributed/test_buffer_view.cpp; modules/extension/distributed/include/SushiRuntime/distributed/buffer_view.hpp (grep only)
```

### `tests/extension/distributed/test_cluster.cpp`

```
Medium_Cluster.AMasterWithNoWorkerReportsNone | split | 3 | attach-then-report and detach-then-report are two actions; Runtime::create made in the body | tests/extension/distributed/test_cluster.cpp; modules/extension/distributed/include/SushiRuntime/distributed/cluster.hpp (grep only)
Medium_Cluster.ASharedAddressGetsAHandle | split | 3 | handle and version bump are one promise; the unknown-address lookup is another; assertions on different calls | tests/extension/distributed/test_cluster.cpp; modules/extension/distributed/include/SushiRuntime/distributed/cluster.hpp (grep only)
Medium_Cluster.ALivenessTimeoutBelowTheMinimumIsRefused | resize | 8 | refusal decided on a value by the check in cluster.cpp; small once the check takes a value without a Runtime; open Q4: a probable duplicate at tests/extension/distributed/test_distributed.cpp:2885 (RefusesATimeoutBelowOneMillisecond) was not read | tests/extension/distributed/test_cluster.cpp; modules/extension/distributed/include/SushiRuntime/distributed/cluster.hpp (grep only); modules/extension/distributed/source/cluster.cpp (grep only)
```

### `tests/extension/distributed/test_distributed.cpp`

```
Medium_DistributedE2ETest.OffloadVectorAddRoundTrip | rewrite | 8 | size right; body makes its own Runtime and SYCL queue, which medium forbids (testing Sizes, Running fast 3); promise: a shared-input vector add offloaded runs once on the worker and its output returns whole | tests/extension/distributed/test_distributed.cpp; modules/extension/distributed/include/SushiRuntime/distributed/cluster.hpp
Medium_DistributedE2ETest.FanOutAcrossTwoWorkers | rewrite | 8 | size right; body makes its own Runtime and queue; promise: two independent offloadable tasks each run on a worker of a two-worker cluster | tests/extension/distributed/test_distributed.cpp; modules/extension/distributed/include/SushiRuntime/distributed/cluster.hpp
Medium_DistributedE2ETest.AutoVersionBumpAcrossLocalWriteThenOffloadedRead | merge | 4 | merge with Medium_DistributedE2ETest.WorkerPullsTheNewVersionAfterALocalWrite: both assert that a local write raises the version and the next offloaded read sees the new values; the pair becomes rows of one promise | tests/extension/distributed/test_distributed.cpp
Medium_DistributedE2ETest.WorkerPullsTheNewVersionAfterALocalWrite | merge | 4 | merge with Medium_DistributedE2ETest.TwoLocalWritesBeforeADispatchCostOnePull: same promise (a local write, then the worker reads the new version) with one write against two writes; rows of one table | tests/extension/distributed/test_distributed.cpp
Medium_DistributedE2ETest.TwoLocalWritesBeforeADispatchCostOnePull | merge | 4 | merge with Medium_DistributedE2ETest.WorkerPullsTheNewVersionAfterALocalWrite: same promise, two-write row of the same table | tests/extension/distributed/test_distributed.cpp
Medium_DistributedE2ETest.OffloadOverTCPLoopback | rewrite | 8 | size right; body makes its own Runtime and queue; promise: the vector-add offload over TCP loopback returns the output and counts bytes both ways (the transport differs from the in-process round trip) | tests/extension/distributed/test_distributed.cpp; modules/extension/distributed/include/SushiRuntime/distributed/cluster.hpp
Medium_DistributedE2ETest.WorkerDeathFallsBackToLocal | rewrite | 1 | ScriptedWorker is a test-written stand-in for the worker side of TransportEvents, an interface the repository owns; the rewrite uses a real LocalWorker and cuts its link with InProcessFabric::set_partitioned, the in-memory adapter; promise: a task whose worker falls silent is taken back and finishes locally | tests/extension/distributed/test_distributed.cpp; modules/extension/distributed/include/SushiRuntime/distributed/in_process_transport.hpp; modules/extension/transport/include/SushiRuntime/transport/transport.hpp
Medium_DistributedReliabilityTest.StaleWritebackIsFenced | rewrite | 1 | ScriptedWorker stand-in; eventually() polls a counter under a wall-clock deadline, a wait to synchronise (testing Sizes, forbidden kinds); rewrite drives the in-memory worker endpoint from InProcessTransport::make_worker and its send_stream; promise: a chunk of an old version is refused and a current chunk is taken | tests/extension/distributed/test_distributed.cpp; modules/extension/distributed/include/SushiRuntime/distributed/in_process_transport.hpp; modules/extension/transport/include/SushiRuntime/transport/transport.hpp
Medium_DistributedE2ETest.PolicySeesWorkerFactsAcrossReconnect | rewrite | 1 | ScriptedWorker stand-in; FactsRecordingPolicy is a recorder written for the OffloadPolicy port, a spy; eventually() polls; rewrite reads the facts through ClusterStatistics::workers instead of a spy; promise: a policy reads a worker's facts, and the new ones after a reconnect | tests/extension/distributed/test_distributed.cpp; modules/extension/distributed/include/SushiRuntime/distributed/cluster.hpp
Medium_DistributedPlacementTest.RulePinsANodeToTheWorkerItNames | rewrite | 1 | ScriptedWorker stand-in inside expect_pinned_to_second_worker; rewrite uses a real LocalWorker on InProcessFabric; promise: a rule pins a class and key to the machine part of its target | tests/extension/distributed/test_distributed.cpp; modules/extension/distributed/include/SushiRuntime/distributed/in_process_transport.hpp
Medium_DistributedPlacementTest.RulePinsANodeToTheWorkerItsLabelNames | rewrite | 1 | ScriptedWorker stand-in as above; the label-target promise has no other test, so it stays, as a row beside RulePinsANodeToTheWorkerItNames; promise: a rule pins a class and key to the worker its machine_label names | tests/extension/distributed/test_distributed.cpp
Medium_DistributedPlacementTest.WaitHoldsTheNodeUntilTheWorkerRegisters | rewrite | 1 | ScriptedWorker stand-in; eventually() polls cluster statistics under a wall-clock deadline; promise: Wait holds a node while its worker is absent and sends it on registration | tests/extension/distributed/test_distributed.cpp
Medium_DistributedPlacementTest.WaitRunsTheNodeLocallyOnceTheBoundPasses | resize | 8 | the bound is a wall-clock decision, reached through a runtime and the real steady clock; the wait takes no clock the test advances (testing Architecture for small tests, 7); seam: an injected clock for the bound; promise: Wait runs the node on the master once its bound passes, and never before | tests/extension/distributed/test_distributed.cpp; modules/extension/distributed/include/SushiRuntime/distributed/cluster.hpp
Medium_DistributedPlacementTest.FailEndsTheRunNamingTheRuleAndTheTarget | resize | 8 | the Fail decision and its message are values decided by the rule and the worker set, reached through a runtime and a cluster; seam: the placement decision as a function of values; promise: Fail ends the run with InvalidState naming the rule and the target, and runs nothing | tests/extension/distributed/test_distributed.cpp; modules/extension/distributed/include/SushiRuntime/distributed/cluster.hpp
Medium_DistributedPlacementTest.FailEndsTheRunOfANodeThatCannotTravel | rewrite | 1 | ScriptedWorker stand-in inside run_untagged_pinned_node, which also installs a sink on the process-wide logger (a process-wide instance, question 8 kind); promise: an untagged node a rule sends to a live worker fails the run with InvalidArgument naming the missing operation | tests/extension/distributed/test_distributed.cpp
Medium_DistributedPlacementTest.LocalRunsANodeThatCannotTravelAndWarnsOnce | rewrite | 1 | ScriptedWorker stand-in; the one WARN is read from the process-wide logger through a sink the test installs; rewrite takes the WARN through an in-memory log sink passed to the runtime (a seam) and the in-memory worker endpoint; promise: Local runs the node on the master and logs one WARN naming rule, node and reason | tests/extension/distributed/test_distributed.cpp
Medium_DistributedPlacementTest.RuleDeadlineTakesASilentTaskBackSooner | delete | 1 | ScriptedWorker stand-in, and the promise (a rule deadline reaps a silent task) is tested elsewhere in this file by Medium_DistributedPlacementTest.Regression_ARuleDeadlineReapsBeforeTheMonitorPeriod, which asserts the same reap and the period bound | tests/extension/distributed/test_distributed.cpp
Medium_DistributedPlacementTest.Regression_ARuleDeadlineReapsBeforeTheMonitorPeriod | rewrite | 1 | ScriptedWorker stand-in; while loops with yield poll the counters in the body (control flow, wall-clock wait); rewrite keeps the regression, with an injected clock for the monitor period; promise: a 50 ms rule deadline reaps the silent task before the monitor's 250 ms period ends | tests/extension/distributed/test_distributed.cpp
Medium_DistributedPlacementTest.BusyTargetRunsBothNodesOneAfterTheOther | rewrite | 1 | ScriptedWorker stand-in; eventually() polls under a wall-clock deadline; promise: a node whose target is busy is held for the target's own slot and runs after the first node | tests/extension/distributed/test_distributed.cpp
Medium_DistributedPlacementTest.UnmatchedNodeIsPlacedByThePolicy | rewrite | 1 | ScriptedWorker stand-in and FactsRecordingPolicy spy; rewrite observes the policy's choice through ClusterStatistics; promise: a rule set that matches no node leaves the placement to the policy, on device 0 | tests/extension/distributed/test_distributed.cpp; modules/extension/distributed/include/SushiRuntime/distributed/cluster.hpp
Medium_DistributedPlacementTest.Regression_ADeviceOnlyRuleTheMasterLacksOffloads | rewrite | 1 | ScriptedWorker stand-in; body makes a CPU-only Runtime, which medium forbids; promise: a device-only GPU rule on a CPU-only master offloads the node to the GPU of a worker | tests/extension/distributed/test_distributed.cpp
Medium_DistributedPlacementTest.Regression_ARegionsNodeInAScopeReachesTheNamedWorker | rewrite | 1 | ScriptedWorker stand-in; promise: a node recorded in a placement scope of a region reaches the worker its rule names | tests/extension/distributed/test_distributed.cpp
Medium_DistributedReliabilityTest.LostBufferPushTimesOutInsteadOfHanging | rewrite | 8 | size right; body makes its own Runtime and queue; the fault is set on InProcessFabric, the in-memory seam already used; promise: a task whose input chunks are all lost is redispatched and finishes locally, and the run returns; the name says time out, the body asserts redispatch | tests/extension/distributed/test_distributed.cpp; modules/extension/distributed/include/SushiRuntime/distributed/in_process_transport.hpp
Medium_DistributedE2ETest.FluentClusterOffload | rewrite | 8 | size right; body makes its own Runtime; promise: the fluent surface builds and runs an offloaded task on a real worker | tests/extension/distributed/test_distributed.cpp
Medium_DistributedE2ETest.FluentDistributedStateParity | rewrite | 8 | size right; helper evolve_distributed_state makes a Runtime on each of its two calls; the two calls are rows of one promise; promise: a double-buffered State offloads and matches the reference for an even and an odd step count | tests/extension/distributed/test_distributed.cpp
Medium_DistributedE2ETest.ClusterDetachAndDisplacement | resize | 8 | install, detach and displacement of the master are bookkeeping decided on the context's offload slot, reached through a runtime; seam: the slot logic as a function of values; promise: detach removes the installed master, a later attach displaces it, and a displaced handle's detach removes nothing | tests/extension/distributed/test_distributed.cpp; modules/extension/distributed/include/SushiRuntime/distributed/cluster.hpp
Medium_DistributedE2ETest.ClusterHandleOutlivesRuntime | rewrite | 2 | with no worker, its assertions (alive_workers 0, offloaded 0) hold whether or not detach() touched freed memory, so it cannot fail except under AddressSanitizer; no other test holds the promise; rewrite to make the post-runtime detach observable, or run it under AddressSanitizer as its promise needs; promise: a handle outlives its runtime and its detach then does nothing | tests/extension/distributed/test_distributed.cpp; modules/extension/distributed/include/SushiRuntime/distributed/cluster.hpp
Medium_DistributedE2ETest.StopResubmitsPendingNodes | rewrite | 1 | ScriptedWorker stand-in (Accept, holds the task); body makes its own Runtime and queue; promise: stopping the master returns a task still out on a worker to the scheduler | tests/extension/distributed/test_distributed.cpp
Medium_DistributedE2ETest.TryOffloadAfterStopRunsLocally | rewrite | 8 | size right; body makes its own Runtime and queue; promise: a stopped master declines every offload and the graph runs locally | tests/extension/distributed/test_distributed.cpp
Medium_DistributedE2ETest.DynamicStateBumpsTheLiveBufferAfterSwap | resize | 8 | the version bump of a local write is bookkeeping on addresses decided on values, reached through a runtime and a state; seam: the bump as a function of a handle table; promise: each local step bumps the buffer it wrote, front and back alike | tests/extension/distributed/test_distributed.cpp; modules/extension/distributed/include/SushiRuntime/distributed/cluster.hpp
Medium_DistributedE2ETest.AttachStartFailureLeavesNothingInstalled | rewrite | 1 | StubTransport is a test-written stand-in for the Transport port, which the repository owns; the in-memory adapter InProcessTransport has no start refusal, so the rewrite needs an option on it; promise: a transport that fails to start makes attach return its refusal and install nothing | tests/extension/distributed/test_distributed.cpp; modules/extension/transport/include/SushiRuntime/transport/transport.hpp; modules/extension/distributed/include/SushiRuntime/distributed/in_process_transport.hpp
Medium_DistributedE2ETest.AttemptCapDoesNotOutliveTheRun | rewrite | 1 | ScriptedWorker stand-in (Fail) and a loop over two rounds in the body; promise: the attempt cap of one run does not keep a node local in the next run | tests/extension/distributed/test_distributed.cpp
Medium_DistributedE2ETest.RefusesEmptySecretOnNetworkExposure | rewrite | 1 | StubTransport stand-in reporting Network exposure; the in-memory adapter reports Loopback and has no exposure option, so the rewrite needs one; the second half (a secret lets attach succeed) is a second promise and becomes its own test; promise: attach refuses an empty secret on a Network transport and starts nothing | tests/extension/distributed/test_distributed.cpp; modules/extension/transport/include/SushiRuntime/transport/transport.hpp
Medium_DistributedE2ETest.RegisteredTaskRunsWithAndWithoutAWorker | rewrite | 8 | size right; helper run_registered_sum makes a Runtime on each of its two calls; promise: a task recorded with no lambda gives the same result with and without a worker | tests/extension/distributed/test_distributed.cpp
Medium_DistributedE2ETest.AttachOnAStartedTransportLeavesTheFirstMasterServing | split | 3 | two promises: a second attach on a started transport throws and installs nothing; the first master still serves a worker afterwards; the body builds two Runtimes | tests/extension/distributed/test_distributed.cpp; modules/extension/distributed/include/SushiRuntime/distributed/cluster.hpp
Medium_DistributedE2ETest.DetachedClusterReportsNoWorkers | rewrite | 8 | size right; body makes its own Runtime; promise: a detached handle reports no worker, no kernel, no reached count and empty statistics | tests/extension/distributed/test_distributed.cpp; modules/extension/distributed/include/SushiRuntime/distributed/cluster.hpp
Medium_DistributedE2ETest.AttachIsRefusedAndDetachDeferredDuringARun | rewrite | 1 | ScriptedWorker stand-in and StubTransport stand-in; promise: attach is refused while a run is in flight with InvalidState, and detach leaves the master installed until the run returns | tests/extension/distributed/test_distributed.cpp
Medium_DistributedE2ETest.MovedFromHandleStillNamesTheMaster | resize | 8 | move and copy semantics of the handle are decided on the handle's state, reached through a runtime; seam: a handle state that needs no device; promise: a moved-from handle names no master, and the handle moved to names the master its source named | tests/extension/distributed/test_distributed.cpp; modules/extension/distributed/include/SushiRuntime/distributed/cluster.hpp
Medium_DistributedE2ETest.RefusesATimeoutBelowOneMillisecond | rewrite | 1 | StubTransport stand-in in refusal_of and in the deadline half; the liveness setter half (liveness_timeout 0 refused, K_MINIMUM_TIMEOUT accepted) is the same promise as tests/extension/distributed/test_cluster.cpp::Medium_Cluster.ALivenessTimeoutBelowTheMinimumIsRefused, which the module keeps, so the rewrite drops that half; the attach refusals for liveness and handshake remain as their own promise; promise: attach refuses a zero liveness or handshake timeout and names it | tests/extension/distributed/test_distributed.cpp; tests/extension/distributed/test_cluster.cpp; modules/extension/distributed/include/SushiRuntime/distributed/cluster.hpp
Medium_DistributedE2EDeathTest.ShareStopsOnADeviceResidentHandle | resize | 8 | the refusal is a value check on the residency in Cluster::share_handle (modules/extension/distributed/source/cluster.cpp, lines 348 to 355), reached through a device buffer; seam: the residency check as a function of values; the death test stays a separate process, since the check stops the process; promise: share stops on a Device-residency buffer and on a state of it | tests/extension/distributed/test_distributed.cpp; modules/extension/distributed/include/SushiRuntime/distributed/cluster.hpp; modules/extension/distributed/source/cluster.cpp
Medium_DistributedE2ETest.ShareRegistersAHostAccessibleHandle | split | 3 | three promises: a buffer and a state each get their own handle; an empty handle is skipped; a Host-residency buffer is registered | tests/extension/distributed/test_distributed.cpp; modules/extension/distributed/include/SushiRuntime/distributed/cluster.hpp
Medium_DistributedE2EDeathTest.WorkerAgentSealsItsRegistry | split | 3 | two promises: the agent seals the registry it is built with; a registration after the seal stops the process; the second is already held by tests/extension/distributed/test_kernel_registry.cpp::Medium_KernelRegistryDeathTest.SealedRegistryStopsARegistration, so the death check leaves this file | tests/extension/distributed/test_distributed.cpp; tests/extension/distributed/test_kernel_registry.cpp
Medium_DistributedJepsenTest.DeterminismUnderRandomFaults | rewrite | 7 | the body loops over eight seeds (a loop in the body is a sign, Rows); rewrite as a parametrised test over the seeds; promise: an offload graph under seeded message faults ends with the outputs of a clean run | tests/extension/distributed/test_distributed.cpp
Medium_DistributedJepsenTest.RegistrationSurvivesMessageLoss | merge | 4 | merge with Medium_DistributedJepsenTest.RegistrationSurvivesALostWelcome: same promise (a lost handshake frame does not keep a worker out), other lost frame types; rows of one table | tests/extension/distributed/test_distributed.cpp
Medium_DistributedJepsenTest.RegistrationSurvivesALostWelcome | merge | 4 | merge with Medium_DistributedJepsenTest.RegistrationSurvivesMessageLoss: same promise, the WELCOME row of that table | tests/extension/distributed/test_distributed.cpp
Medium_DistributedTcpFaultsTest.StrayConnectionTakesNoSlot | rewrite | 8 | size right; fixture SetUp and start_worker make Runtimes, which medium forbids and Running fast 3 bars from fixtures; promise: a connection that never says HELLO is closed and takes no worker slot | tests/extension/distributed/test_distributed.cpp
Medium_DistributedTcpFaultsTest.GarbageBytesClosedMasterLives | rewrite | 1 | malformed_peers_reach() polls with sleep_for under a deadline, a sleep to synchronise; promise: bytes that are no frame close their connection and the master keeps serving | tests/extension/distributed/test_distributed.cpp
Medium_DistributedTcpFaultsTest.V1FrameRejected | rewrite | 1 | malformed_peers_reach() polls with sleep_for; promise: a version-1 frame is refused as malformed and the master keeps serving | tests/extension/distributed/test_distributed.cpp
Medium_DistributedTcpFaultsTest.OversizedLengthClosesPeerNotProcess | rewrite | 1 | malformed_peers_reach() polls with sleep_for; promise: a header that announces more than the frame limit closes the peer and not the process | tests/extension/distributed/test_distributed.cpp
Medium_DistributedTcpFaultsTest.WrongSecretRejected | split | 3 | two promises: a worker with another secret ends RejectedAuthentication; a raw peer with a wrong proof is closed and counted as rejected_auth | tests/extension/distributed/test_distributed.cpp
Medium_DistributedTcpFaultsTest.VersionMismatchLoggedOncePerConnection | rewrite | 7 | the body loops over connections (Rows sign); the test also installs a sink on the process-wide logger (count_version_errors), a question 8 kind whose seam is an injected log sink; promise: a peer of another protocol version gets one ERROR line per connection | tests/extension/distributed/test_distributed.cpp
Medium_DistributedTcpFaultsTest.MasterKilledWorkerSurvivesAndReconnects | rewrite | 1 | eventually() polls the reconnect counter with sleep_for; promise: a worker outlives its master and registers with the one that follows | tests/extension/distributed/test_distributed.cpp
Medium_DistributedTcpFaultsTest.WorkerCutMidChunkTaskFinishesLocally | rewrite | 8 | size right; fixture SetUp and start_worker make Runtimes; promise: a worker link cut mid-chunk leaves the task to finish locally, with the counters to show it | tests/extension/distributed/test_distributed.cpp
Medium_DistributedTcpFaultsTest.StalledLinkReapedThenWorkerRejoins | rewrite | 1 | three eventually() polls with sleep_for on link and reconnect state; promise: a worker behind a silent link is reaped and registers again after the link passes | tests/extension/distributed/test_distributed.cpp
Medium_DistributedTcpFaultsTest.LinkStalledMidTransferLostAtLivenessClose | rewrite | 1 | two eventually() polls with sleep_for; promise: a link stalled mid-transfer is lost when liveness closes it, not when the write idle timeout ends, and the task finishes locally | tests/extension/distributed/test_distributed.cpp
Medium_DistributedTcpFaultsTest.ThrottledTransferKeepsWorkerAlive | rewrite | 8 | size right; fixture SetUp and start_worker make Runtimes; promise: a transfer that takes longer than the liveness timeout keeps the worker alive; the elapsed check is a lower bound only | tests/extension/distributed/test_distributed.cpp
Medium_DistributedTcpFaultsTest.ManyChunkBufferCrossesIntact | rewrite | 8 | size right; fixture SetUp makes Runtimes; promise: a 1 MiB buffer crosses both ways whole, as 256 chunks of the minimum size | tests/extension/distributed/test_distributed.cpp
Medium_DistributedTcpFaultsTest.DispatchReturnsWhilePeerStalled | rewrite | 1 | eventually() polls with sleep_for; promise: a dispatch to a silent link returns and a local node runs beside it | tests/extension/distributed/test_distributed.cpp
```

### `tests/extension/distributed/test_distributed_log_category.cpp`

```
Small_DistributedLogCategory.TheCategoryIsNamedAfterTheModule | delete | 1 | forbidden: static_assert run as a test ending in SUCCEED(); a trivial constant that states no defect | tests/extension/distributed/test_distributed_log_category.cpp
```

### `tests/extension/distributed/test_in_process_transport.cpp`

```
Medium_InProcessTransport.InProcessTransportDeliversBothDirections | delete | 1 | forbidden: RecordingEvents is a sink the test writes for TransportEvents; the promise is held by Medium_TransportConformanceTest.DeliversInOrderPerPeer over InProcessRig, the contract copy kept at question 4 | tests/extension/distributed/test_in_process_transport.cpp; tests/extension/distributed/test_tcp_transport.cpp; modules/extension/distributed/include/SushiRuntime/distributed/in_process_transport.hpp (grep only)
```

### `tests/extension/distributed/test_kernel_registry.cpp`

```
Medium_KernelRegistryDeathTest.SealedRegistryStopsARegistration | split | 3 | refusal after seal and kept kernels are two promises; seal is also called twice | tests/extension/distributed/test_kernel_registry.cpp
Medium_KernelRegistryDeathTest.EmptyKernelStopsTheRegistration | keep | 9 | promise row: an empty kernel stops registration, sealed or not; death test spawns a child, so medium is right | tests/extension/distributed/test_kernel_registry.cpp
```

### `tests/extension/distributed/test_registered_task/test_nested_run_probe.cpp`

```
Medium_NestedRunProbe.WorkerKernelRunningAGraphOfItsOwnRuntimeIsRefused | split | 3 | two promises: a kernel on a worker that runs a graph of its own runtime is refused with InvalidState and nothing runs (no deadlock); the runtime is usable afterwards; the body makes two runtimes | tests/extension/distributed/test_registered_task/test_nested_run_probe.cpp; modules/extension/distributed/include/SushiRuntime/distributed/cluster.hpp
Medium_NestedRunProbe.WorkerKernelRunsAGraphOfASecondRuntime | rewrite | 8 | size right; the body makes the worker runtime, the second runtime and the master runtime (medium forbids runtimes in the body); promise: a worker kernel runs a graph of a second runtime to its end | tests/extension/distributed/test_registered_task/test_nested_run_probe.cpp; modules/extension/distributed/include/SushiRuntime/distributed/cluster.hpp
Medium_NestedRunProbe.MasterFallbackRunningAGraphOfItsOwnRuntimeIsRefused | split | 3 | two promises: a local fallback on the master that runs a graph of its own runtime is refused with InvalidState and the outer run ends; the runtime is usable afterwards; the body makes two runtimes | tests/extension/distributed/test_registered_task/test_nested_run_probe.cpp
Medium_NestedRunProbe.MasterFallbackRunsAGraphOfASecondRuntime | rewrite | 8 | size right; the body makes two runtimes (medium forbids runtimes in the body); promise: a local fallback on the master runs a graph of a second runtime to its end | tests/extension/distributed/test_registered_task/test_nested_run_probe.cpp
```

### `tests/extension/distributed/test_registered_task/test_registered_task.cpp`

```
Medium_RegisteredTask.TheKernelRunsLocally | rewrite | 8 | size right (kernel launch checked against host values), but Runtime::create and the buffers are made in the body; Running fast rule 3 needs the module device base, and no testing::Environment exists under tests/ or modules/ | tests/extension/distributed/test_registered_task/test_registered_task.cpp
```

### `tests/extension/distributed/test_shared_secret.cpp`

```
Medium_SharedSecretTest.FromFileRefusesAShortFile | rewrite | 8 | file boundary is right at medium, but the scratch file is written to the working directory, not a per-test temporary folder | tests/extension/distributed/test_shared_secret.cpp
Medium_SharedSecretTest.FromFileAcceptsThirtyTwoBytes | rewrite | 8 | same working-directory write; promise row kept | tests/extension/distributed/test_shared_secret.cpp
Medium_SharedSecretTest.FromFileMissingNamesThePath | keep | 9 | file boundary at medium; writes nothing; names the path | tests/extension/distributed/test_shared_secret.cpp
Medium_SharedSecretTest.FromEnvironmentUnsetIsEmpty | keep | 9 | environment read is outside small's list, so medium is right; the variable name is one nobody sets | tests/extension/distributed/test_shared_secret.cpp
Medium_SharedSecretTest.MoveEmptiesTheSourceAndCloneKeepsIt | split | 3 | move and clone are two promises, each on its own result | tests/extension/distributed/test_shared_secret.cpp
Medium_SharedSecretTest.FromBytesRefusesAShortSecret | resize | 8 | in-memory input decided on values; small; shares the minimum promise with FromFileRefusesAShortFile | tests/extension/distributed/test_shared_secret.cpp
```

### `tests/extension/distributed/test_tcp_transport.cpp`

```
Medium_TransportConformanceTest.DeliversInOrderPerPeer | rewrite | 1 | forbidden: EventRecorder is a sink the fixture writes for TransportEvents; rename to TransportContract and move it into the transport module's test file, with an in-memory event sink there | tests/extension/distributed/test_tcp_transport.cpp; modules/extension/distributed/include/SushiRuntime/distributed/in_process_transport.hpp (grep only)
Medium_TransportConformanceTest.PeerLostOnceAfterLastFrame | rewrite | 1 | forbidden: same EventRecorder sink; several promises to split after the rewrite | tests/extension/distributed/test_tcp_transport.cpp
Medium_TransportConformanceTest.NoCallbackAfterStop | rewrite | 1 | forbidden: same EventRecorder sink | tests/extension/distributed/test_tcp_transport.cpp
Medium_TransportConformanceTest.SendFromCallbackAllowed | rewrite | 1 | forbidden: same EventRecorder sink and reaction | tests/extension/distributed/test_tcp_transport.cpp
Medium_TransportConformanceTest.SendAfterStopReturnsStopped | rewrite | 1 | forbidden: same EventRecorder sink and ScriptedSource stand-in; several statuses | tests/extension/distributed/test_tcp_transport.cpp
Medium_TransportConformanceTest.ControlFrameOvertakesStream | rewrite | 1 | forbidden: same EventRecorder sink and a GatedSource stand-in FrameSource | tests/extension/distributed/test_tcp_transport.cpp
```

### `tests/extension/distributed/test_unsent_frame_log.cpp`

```
Small_UnsentFrameLog.AQueuedFrameLogsNothing | rewrite | 1 | forbidden: Distributed::Detail::log_unsent_frame, and a LogCapture installed on the process-wide logger; only test of the promise; rewrite against a seam that takes the sink | tests/extension/distributed/test_unsent_frame_log.cpp; modules/extension/distributed/include/SushiRuntime/distributed/unsent_frame_log.hpp (grep only)
Small_UnsentFrameLog.ARefusedFrameLogsOneDebugLine | rewrite | 1 | forbidden: same Detail:: call and process-wide sink; only test of the promise | tests/extension/distributed/test_unsent_frame_log.cpp; modules/extension/distributed/include/SushiRuntime/distributed/unsent_frame_log.hpp (grep only)
```

### `tests/extension/distributed/test_worker_agent.cpp`

```
Medium_WorkerBufferCacheTest.ReleaseEvicts | rewrite | 1 | AgentRig's FakeTransport is a test-written recorder and stand-in for the Transport port the repository owns; eventually() polls with sleep_for; the rig makes a Runtime in its member initialiser (medium forbids); rewrite drives the agent through the in-memory adapter InProcessTransport and reads the statistics; promise: BUFFER_RELEASE frees an unpinned buffer, and a stream keeps its own copy of the output | tests/extension/distributed/test_worker_agent.cpp; modules/extension/transport/include/SushiRuntime/transport/transport.hpp; modules/extension/distributed/include/SushiRuntime/distributed/in_process_transport.hpp
Medium_WorkerBufferCacheTest.BudgetEvictsLeastRecentlyUsedUnpinned | rewrite | 1 | FakeTransport stand-in as above; rig-owned Runtime; promise: a full cache evicts the buffer used longest ago and keeps the one read | tests/extension/distributed/test_worker_agent.cpp; modules/extension/transport/include/SushiRuntime/transport/transport.hpp; modules/extension/distributed/include/SushiRuntime/distributed/in_process_transport.hpp
Medium_WorkerBufferCacheTest.PinnedEntryIsNotEvicted | rewrite | 1 | FakeTransport stand-in as above; promise: a buffer a batch holds pinned is never evicted to make room | tests/extension/distributed/test_worker_agent.cpp; modules/extension/transport/include/SushiRuntime/transport/transport.hpp; modules/extension/distributed/include/SushiRuntime/distributed/in_process_transport.hpp
Medium_WorkerBufferCacheTest.GapInAPulledBufferDiscardsIt | rewrite | 1 | FakeTransport stand-in as above; the name holds two promises (a gap discards, a whole pull is kept), which the rewrite splits; promise: a pulled buffer with a gap is discarded and a whole pull is kept | tests/extension/distributed/test_worker_agent.cpp; modules/extension/transport/include/SushiRuntime/transport/transport.hpp
Medium_DispatchExecutorTest.OutputAllocationFailureFailsTheTask | rewrite | 1 | FakeTransport stand-in as above; promise: an output the cache cannot hold fails its task with OutputAllocationFailed, and the next task runs | tests/extension/distributed/test_worker_agent.cpp; modules/extension/transport/include/SushiRuntime/transport/transport.hpp
Medium_DispatchExecutorTest.KernelSeesBufferSizes | rewrite | 1 | FakeTransport stand-in as above; the name holds two promises (the kernel sees sizes, the output leaves in chunks), which the rewrite splits; promise: a kernel sees each buffer's size | tests/extension/distributed/test_worker_agent.cpp; modules/extension/transport/include/SushiRuntime/transport/transport.hpp
Medium_DispatchExecutorTest.DispatchRunsOnTheDeviceItNames | rewrite | 1 | FakeTransport stand-in as above; skips when no second device shares the context; promise: a dispatch runs on the device index it names | tests/extension/distributed/test_worker_agent.cpp; modules/extension/transport/include/SushiRuntime/transport/transport.hpp
Medium_DispatchExecutorTest.DispatchNamingAMissingDeviceFailsTheTask | rewrite | 1 | FakeTransport stand-in as above; promise: a dispatch naming a device the worker lacks fails as KernelFailed and runs no kernel | tests/extension/distributed/test_worker_agent.cpp; modules/extension/transport/include/SushiRuntime/transport/transport.hpp
Medium_DispatchExecutorTest.DispatchOnADeviceOutsideTheCacheContextFailsTheTask | rewrite | 1 | FakeTransport stand-in as above; skips when no device lies outside the cache context; promise: a dispatch on a device outside device 0's context fails as KernelFailed | tests/extension/distributed/test_worker_agent.cpp; modules/extension/transport/include/SushiRuntime/transport/transport.hpp
Medium_DispatchExecutorTest.GenericLambdaRegistersAsAKernel | rewrite | 1 | FakeTransport stand-in as above; promise: a generic lambda registered with register_kernel runs as the kernel of its operation (no other test in the distributed folder names it) | tests/extension/distributed/test_worker_agent.cpp; modules/extension/transport/include/SushiRuntime/transport/transport.hpp
Medium_DispatchExecutorTest.KernelThatThrowsFailsTheTaskAndTheWorkerLives | rewrite | 1 | FakeTransport stand-in as above; promise: a kernel that throws fails its task as KernelFailed and the worker runs the next task | tests/extension/distributed/test_worker_agent.cpp; modules/extension/transport/include/SushiRuntime/transport/transport.hpp
Medium_DispatchExecutorTest.OutputStreamEndsWithTheResult | rewrite | 1 | FakeTransport stand-in as above; the body asserts five frames in order: accepted, three chunks, result; the stream and the cache release are two promises; promise: output chunks come before the TASK_RESULT that ends the stream | tests/extension/distributed/test_worker_agent.cpp; modules/extension/transport/include/SushiRuntime/transport/transport.hpp
Medium_DispatchExecutorTest.DispatchesOverDisjointBuffersAreInFlightTogether | rewrite | 1 | FakeTransport stand-in as above; the Rendezvous waits a bounded time for the partner kernel, a wall-clock wait to synchronise; promise: two dispatches over disjoint buffers have their kernels in flight together, which the rewrite shows with the executor the code is given (testing Running fast 6) | tests/extension/distributed/test_worker_agent.cpp; modules/extension/transport/include/SushiRuntime/transport/transport.hpp
Medium_DispatchExecutorTest.DispatchesWritingOneBufferRunOneAfterTheOther | rewrite | 1 | FakeTransport stand-in as above; the 250 ms rendezvous proves that no partner entered, a wall-clock wait to prove that something did not happen (forbidden kind); promise: two dispatches writing one buffer run one after the other, each sending its own bytes; the rewrite uses a seeded single-threaded executor (testing Running fast 6) | tests/extension/distributed/test_worker_agent.cpp; modules/extension/transport/include/SushiRuntime/transport/transport.hpp
Medium_DispatchExecutorTest.OneRunPullsAnInputTwoDispatchesReadOnce | rewrite | 1 | FakeTransport stand-in as above; promise: two dispatches of one run that read one input version pull it once | tests/extension/distributed/test_worker_agent.cpp; modules/extension/transport/include/SushiRuntime/transport/transport.hpp
Medium_DispatchExecutorTest.ADispatchReadingAQueuedOutputRunsAfterIt | rewrite | 1 | FakeTransport stand-in as above; promise: a dispatch that reads an output a queued dispatch makes waits for it on the worker and does not pull it from the master | tests/extension/distributed/test_worker_agent.cpp; modules/extension/transport/include/SushiRuntime/transport/transport.hpp
Medium_DispatchExecutorTest.DisconnectEndsAnOpenInputPull | rewrite | 1 | FakeTransport stand-in as above; eventually() polls with sleep_for; promise: losing the master ends a pull that has no idle timeout, and the next connection's task runs | tests/extension/distributed/test_worker_agent.cpp; modules/extension/transport/include/SushiRuntime/transport/transport.hpp
Medium_MasterLinkTest.HandshakeChecksTheMasterBeforeProving | rewrite | 1 | FakeTransport stand-in as above; eventually() polls with sleep_for; promise: the worker checks the master's proof before it sends its own PROOF | tests/extension/distributed/test_worker_agent.cpp; modules/extension/transport/include/SushiRuntime/transport/transport.hpp
Medium_MasterLinkTest.WrongMasterProofEndsTheLink | rewrite | 1 | FakeTransport stand-in as above; promise: a master that proves another key ends the link before the worker proves anything | tests/extension/distributed/test_worker_agent.cpp; modules/extension/transport/include/SushiRuntime/transport/transport.hpp
Medium_MasterLinkTest.EndedLinkStopsItsTransport | rewrite | 1 | FakeTransport stand-in as above; promise: an ended link stops its transport, so it connects no more | tests/extension/distributed/test_worker_agent.cpp; modules/extension/transport/include/SushiRuntime/transport/transport.hpp
Medium_MasterLinkTest.PingGetsNoPongBeforeRegistration | rewrite | 1 | FakeTransport stand-in as above; eventually() polls with sleep_for; promise: a worker answers PING only once WELCOME registered it | tests/extension/distributed/test_worker_agent.cpp; modules/extension/transport/include/SushiRuntime/transport/transport.hpp
Medium_MasterLinkTest.LateChallengeForAForgottenHelloIsIgnored | rewrite | 1 | FakeTransport stand-in as above; the 1 ms heartbeat and the five HELLO loop are a runtime wait, not a sign of the promise; promise: a CHALLENGE for a HELLO the link forgot is ignored, not taken as a wrong secret | tests/extension/distributed/test_worker_agent.cpp; modules/extension/transport/include/SushiRuntime/transport/transport.hpp
Medium_MasterLinkTest.IdleHeartbeatCarriesTheNextTaskID | rewrite | 1 | FakeTransport stand-in as above; promise: an idle worker's HEARTBEAT names the task id it waits for next | tests/extension/distributed/test_worker_agent.cpp; modules/extension/transport/include/SushiRuntime/transport/transport.hpp
Medium_MasterLinkTest.SilentMasterIsLost | rewrite | 1 | FakeTransport stand-in as above; the 40 ms liveness depends on the real clock; promise: a master silent past its liveness timeout is closed, and a lost master does not end a link that may reconnect | tests/extension/distributed/test_worker_agent.cpp; modules/extension/transport/include/SushiRuntime/transport/transport.hpp
Medium_MasterLinkTest.NewSessionClearsTheCache | rewrite | 1 | FakeTransport stand-in as above; the name holds two promises (a new session clears the cache, a reconnect does not), which the rewrite splits; promise: a new master session clears the cache and the ids seen | tests/extension/distributed/test_worker_agent.cpp; modules/extension/transport/include/SushiRuntime/transport/transport.hpp
Medium_MasterLinkTest.LossAfterGoodbyeIsTheWorkersOwn | rewrite | 1 | FakeTransport stand-in as above; promise: the worker's own close after GOODBYE ends it as Stopped, not MasterLost | tests/extension/distributed/test_worker_agent.cpp; modules/extension/transport/include/SushiRuntime/transport/transport.hpp
Medium_WorkerAgentTest.Regression_ANameAbove255BytesIsRefusedBeforeConnecting | rewrite | 1 | FakeTransport stand-in; the body makes a Runtime (medium forbids); promise: a worker name HELLO cannot carry is refused with InvalidArgument before the transport starts. Same promise family as tests/extension/distributed/test_worker_options_parser.cpp::Medium_WorkerOptionsParserTest.NameAbove255BytesIsRefused, which checks the parser, a different unit | tests/extension/distributed/test_worker_agent.cpp; tests/extension/distributed/test_worker_options_parser.cpp; modules/extension/transport/include/SushiRuntime/transport/transport.hpp
Medium_WorkerAgentTest.Regression_ALabelValueAbove255BytesIsRefusedBeforeConnecting | rewrite | 1 | FakeTransport stand-in; the body makes a Runtime (medium forbids); promise: a label value HELLO cannot carry is refused, naming the label, before the transport starts. Parser twin: tests/extension/distributed/test_worker_options_parser.cpp::Medium_WorkerOptionsParserTest.ALabelAbove255BytesIsRefused, a different unit | tests/extension/distributed/test_worker_agent.cpp; tests/extension/distributed/test_worker_options_parser.cpp; modules/extension/transport/include/SushiRuntime/transport/transport.hpp
```

### `tests/extension/distributed/test_worker_main.cpp`

```
Medium_WorkerMainTest.UnreadableSecretFileExitsOne | resize | 8 | captures the process stderr, a process-wide reach; needs an output seam; the file lookup at medium is right | tests/extension/distributed/test_worker_main.cpp
Medium_WorkerMainTest.ShortSecretFileExitsOne | resize | 8 | process stderr capture needs the seam; the scratch file is in the working directory and needs a per-test folder | tests/extension/distributed/test_worker_main.cpp
Medium_WorkerMainTest.UnknownLogLevelExitsOne | resize | 8 | process stderr capture needs the seam | tests/extension/distributed/test_worker_main.cpp
Medium_WorkerMainTest.BadFlagExitsOne | resize | 8 | process stderr capture needs the seam; the parser message is also checked in test_worker_options_parser.cpp | tests/extension/distributed/test_worker_main.cpp
Medium_WorkerMainTest.VersionPrintsTheProtocolAndExitsZero | resize | 8 | process stdout capture needs the seam | tests/extension/distributed/test_worker_main.cpp
Medium_WorkerMainTest.SignalStopsWithExitZero | rewrite | 1 | forbidden: FakeMaster is a stand-in for the repository's own master on TransportEvents; also raises SIGINT in the process, a process-wide reach | tests/extension/distributed/test_worker_main.cpp
Medium_WorkerMainTest.AuthenticationRejectExitsThree | rewrite | 1 | forbidden: FakeMaster stand-in | tests/extension/distributed/test_worker_main.cpp
Medium_WorkerMainTest.VersionRejectExitsFour | rewrite | 1 | forbidden: FakeMaster stand-in | tests/extension/distributed/test_worker_main.cpp
Medium_WorkerMainTest.MasterNotReachedExitsTwo | keep | 9 | promise row: unreachable master exits 2; a worker thread against a port nobody listens on is a boundary; the port is fixed at 1 | tests/extension/distributed/test_worker_main.cpp
Medium_WorkerMainTest.MasterLostWithoutReconnectExitsFive | rewrite | 1 | forbidden: FakeMaster stand-in | tests/extension/distributed/test_worker_main.cpp
Medium_WorkerMainTest.RunWorkerOverTCPReturnsTheCodeOfItsEnd | delete | 1 | forbidden: FakeMaster stand-in; the promise (run_worker returns the end code, MasterLost) is the one MasterLostWithoutReconnectExitsFive states | tests/extension/distributed/test_worker_main.cpp
```

### `tests/extension/distributed/test_worker_options_parser.cpp`

```
Medium_WorkerOptionsParserTest.MasterAloneGivesTheDefaults | rewrite | 1 | forbidden: checks default values, the trivial kind; the promise that defaults apply is kept as rows of what each default means | tests/extension/distributed/test_worker_options_parser.cpp
Medium_WorkerOptionsParserTest.EveryFlagIsRead | resize | 8 | pure parser over an injected reader; small | tests/extension/distributed/test_worker_options_parser.cpp
Medium_WorkerOptionsParserTest.EnvironmentFillsWhatNoFlagGives | resize | 8 | pure parser over an injected reader; small | tests/extension/distributed/test_worker_options_parser.cpp
Medium_WorkerOptionsParserTest.FlagBeatsEnvironmentBeatsDefault | resize | 8 | pure parser over an injected reader; small | tests/extension/distributed/test_worker_options_parser.cpp
Medium_WorkerOptionsParserTest.EmptyVariableCountsAsUnset | resize | 8 | pure parser over an injected reader; small | tests/extension/distributed/test_worker_options_parser.cpp
Medium_WorkerOptionsParserTest.ProcessEnvironmentOverloadReadsTheFlags | keep | 9 | the process-environment overload is the one door that reads the real environment, so medium is right | tests/extension/distributed/test_worker_options_parser.cpp
Medium_WorkerOptionsParserTest.HelpListsEveryFlagAndVariable | rewrite | 7 | loop over a word list in the body; rows or a helper named expect_* | tests/extension/distributed/test_worker_options_parser.cpp
Medium_WorkerOptionsParserTest.VersionNeedsNoMaster | resize | 8 | pure; small | tests/extension/distributed/test_worker_options_parser.cpp
Medium_WorkerOptionsParserTest.EachRefusalNamesWhatWasWrong | resize | 8 | row table over a pure parser; small | tests/extension/distributed/test_worker_options_parser.cpp
Medium_WorkerOptionsParserTest.NameAbove255BytesIsRefused | resize | 8 | pure; small | tests/extension/distributed/test_worker_options_parser.cpp
Medium_WorkerOptionsParserTest.LabelsComeFromTheVariableAndRepeatedFlags | resize | 8 | pure; small | tests/extension/distributed/test_worker_options_parser.cpp
Medium_WorkerOptionsParserTest.TheVariableHoldsPairsSeparatedByCommas | resize | 8 | pure; small | tests/extension/distributed/test_worker_options_parser.cpp
Medium_WorkerOptionsParserTest.NoLabelGivesAnEmptyList | resize | 8 | pure; small | tests/extension/distributed/test_worker_options_parser.cpp
Medium_WorkerOptionsParserTest.ALabelThatBreaksARuleIsRefused | resize | 8 | row table over a pure parser; small | tests/extension/distributed/test_worker_options_parser.cpp
Medium_WorkerOptionsParserTest.ALabelAbove255BytesIsRefused | resize | 8 | pure; small | tests/extension/distributed/test_worker_options_parser.cpp
```

### `tests/extension/frame_transport/test_frame_sending.cpp`

```
Medium_FrameSending.AMessageLeavesAsOneFrameOfItsType | rewrite | 1 | forbidden: LastFrameTransport is a stand-in the test writes for Transport; the in-memory Transport adapter, InProcessTransport, lives in the distributed module, not the transport module; only test of the send_message promise | tests/extension/frame_transport/test_frame_sending.cpp; modules/extension/frame_transport/include/SushiRuntime/frame_transport/frame_sending.hpp (grep only)
```

### `tests/extension/frame_transport/test_outbox.cpp`

```
Medium_OutboxTest.ControlComesBeforeStream | rewrite | 1 | forbidden: ScriptedSource is a stand-in FrameSource the test writes; grep finds no in-memory FrameSource adapter in modules/; only test of control-before-stream; several promises, to split after the rewrite | tests/extension/frame_transport/test_outbox.cpp
Medium_OutboxTest.StreamsTakeTurns | rewrite | 1 | forbidden: same ScriptedSource stand-in; only test of the round-robin promise | tests/extension/frame_transport/test_outbox.cpp
Medium_OutboxTest.QueueFullAtCapacity | split | 3 | capacity refusal with release on pop, and the payload bound (TooLong), are separate promises | tests/extension/frame_transport/test_outbox.cpp
Medium_OutboxTest.CloseKeepsControlAndDropsStreams | rewrite | 1 | forbidden: same ScriptedSource stand-in; several promises (keeps control, drops streams, refuses pushes) | tests/extension/frame_transport/test_outbox.cpp
```

### `tests/extension/link_security/test_handshake.cpp`

```
Medium_HandshakeTest.ProofsMatchAnIndependentImplementation | resize | 8 | oracle is Python hmac output, kept; pure values, so small | tests/extension/link_security/test_handshake.cpp; modules/extension/link_security/include/SushiRuntime/link_security/handshake.hpp (grep only)
Medium_HandshakeTest.ProofsDifferByRole | resize | 8 | one promise on pure values; small | tests/extension/link_security/test_handshake.cpp; modules/extension/link_security/include/SushiRuntime/link_security/handshake.hpp (grep only)
Medium_HandshakeTest.WrongSecretFails | split | 3 | refusal under a wrong or no secret, and acceptance of an empty-secret proof, are separate promises; the role loop in the body needs rows | tests/extension/link_security/test_handshake.cpp
Medium_HandshakeTest.FreshNonceDefeatsReplay | resize | 8 | one promise over three nonce variants, to be rows; pure, so small | tests/extension/link_security/test_handshake.cpp; modules/extension/link_security/include/SushiRuntime/link_security/handshake.hpp (grep only)
Medium_HandshakeTest.PlatformNonceSourceDoesNotRepeat | keep | 9 | make_nonce is the platform adapter, so medium is right; the all-zero check states a second promise the name does not carry | tests/extension/link_security/test_handshake.cpp; modules/extension/link_security/include/SushiRuntime/link_security/handshake.hpp (grep only)
```

### `tests/extension/link_security/test_hmac_sha256.cpp`

```
Medium_HmacSha256Test.HmacRfc4231Vectors | rewrite | 7 | six inputs of one promise asserted inline: rows; pure, so small | tests/extension/link_security/test_hmac_sha256.cpp
Medium_HmacSha256Test.EqualInConstantTimeSeesEveryByte | rewrite | 7 | loop over the bytes in the body: a builder or rows; pure, so small | tests/extension/link_security/test_hmac_sha256.cpp
```

### `tests/extension/link_security/test_sha256.cpp`

```
Medium_Sha256Test.Sha256Fips180Vectors | rewrite | 7 | five inputs of one promise asserted inline: rows; pure, so small | tests/extension/link_security/test_sha256.cpp
Medium_Sha256Test.PiecewiseUpdateMatchesOneShot | split | 3 | piecewise equals one-shot; update after finish() equals a fresh hash (finish resets): two promises; the input loop also needs a builder | tests/extension/link_security/test_sha256.cpp
```

### `tests/extension/network_socket/test_network_socket_log_category.cpp`

```
Small_NetworkSocketLogCategory.TheCategoryIsNamedAfterTheModule | delete | 1 | forbidden: static_assert run as a test ending in SUCCEED(); a trivial constant that states no defect | tests/extension/network_socket/test_network_socket_log_category.cpp
```

### `tests/extension/network_socket/test_socket.cpp`

```
Medium_SocketTest.ExactReadAndFullWriteRoundTrip | split | 3 | exact big read and full write; reply round trip; peer_endpoint: separate results of separate calls | tests/extension/network_socket/test_socket.cpp
Medium_SocketTest.IdleReadAndIdleAcceptTimeOut | split | 3 | idle accept and idle read are two calls, two promises | tests/extension/network_socket/test_socket.cpp
Medium_SocketTest.ShutdownEndsAWaitingRead | keep | 9 | promise row: shutdown from another thread ends a waiting read with Closed; socket and thread are the boundary, so medium is right | tests/extension/network_socket/test_socket.cpp
Medium_SocketTest.WriteToClosedPeerReturnsError | split | 3 | a write into a closed peer returns Closed with its OS cause; a later write has no OS error: two results of two calls; a write loop also sits in the body | tests/extension/network_socket/test_socket.cpp
Medium_SocketTest.BindInUseNamesPortAndCause | keep | 9 | one refused call; status, operation, cause and message all describe that one refusal | tests/extension/network_socket/test_socket.cpp
Medium_SocketTest.LocalhostResolves | split | 3 | resolve_addresses gives the IPv4 loopback; connect by name reaches the listener; a membership loop sits in the body | tests/extension/network_socket/test_socket.cpp
Medium_SocketTest.Ipv6LoopbackConnects | split | 3 | IPv6 connect, peer name and byte transfer are separate calls and results | tests/extension/network_socket/test_socket.cpp
```

### `tests/extension/network_socket/test_socket_option_report.cpp`

```
Medium_Socket.LogsAFailedNoDelayOption | rewrite | 1 | forbidden: calls the Detail:: report function directly and installs LogCapture on the process-wide logger; only test of the promise | tests/extension/network_socket/test_socket_option_report.cpp; modules/extension/network_socket/include/SushiRuntime/network_socket/socket_option_report.hpp (grep only)
```

### `tests/extension/offload_coordination/test_held_nodes.cpp`

```
Small_HeldNodes.FirstOfferIsKeptUntilForgotten | keep | 9 | HeldNodes: first offer kept until forget() | tests/extension/offload_coordination/test_held_nodes.cpp; modules/extension/offload_coordination/include/SushiRuntime/offload_coordination/held_nodes.hpp; modules/extension/offload_coordination/source/held_nodes.cpp
Small_HeldNodes.TakeExpiredReturnsOnlyTheEndedWaits | split | 3 | three promises: take_expired() returns the ended waits; earliest() follows the remaining holds; take_all() returns the rest and empties the table; one test each | tests/extension/offload_coordination/test_held_nodes.cpp; modules/extension/offload_coordination/include/SushiRuntime/offload_coordination/held_nodes.hpp
Small_HeldNodes.CloseRefusesAHoldAndDrainEmptiesTheTable | split | 3 | two promises joined by "And": close() refuses later holds; drain() returns the held nodes and forgets their first offers; one test each | tests/extension/offload_coordination/test_held_nodes.cpp; modules/extension/offload_coordination/include/SushiRuntime/offload_coordination/held_nodes.hpp
Small_HeldNodes.HoldAfterAReleaseIsStale | keep | 9 | HeldNodes: hold decided before a release returns Stale | tests/extension/offload_coordination/test_held_nodes.cpp; modules/extension/offload_coordination/include/SushiRuntime/offload_coordination/held_nodes.hpp
```

### `tests/extension/offload_coordination/test_offload_coordination_log_category.cpp`

```
Small_OffloadCoordinationLogCategory.TheCategoryIsNamedAfterTheModule | delete | 1 | forbidden kind: a test of a log category, with SUCCEED() and static_assert run as the test; the name of a log category states no promise | tests/extension/offload_coordination/test_offload_coordination_log_category.cpp; tests/extension/process_environment/test_process_environment_log_category.cpp
```

### `tests/extension/offload_coordination/test_offload_dispatcher.cpp`

```
Medium_OffloadDispatcherTest.AttemptCapDeclinesWithoutClearingOffloadable | rewrite | 1 | fixture stand-in RecordingTransport for the Transport port and FirstWorkerPolicy for OffloadPolicy; rewrite needs an in-memory Transport adapter in modules/extension/transport (none exists; InProcessTransport sits in the distributed module) and an in-memory OffloadPolicy adapter in modules/extension/cluster_load (none exists) | tests/extension/offload_coordination/test_offload_dispatcher.cpp; tests/extension/offload_coordination/test_worker_directory.cpp; modules/extension/transport/include/SushiRuntime/transport/transport.hpp; modules/extension/cluster_load/include/SushiRuntime/cluster_load/offload_policy.hpp; modules/engine/task/include/SushiRuntime/task/offload_sink.hpp; modules/foundation/test_runner/include/SushiRuntime/test_runner/log_capture.hpp
Medium_OffloadDispatcherTest.RefusedSendUndoesThePendingEntry | rewrite | 1 | fixture stand-ins RecordingTransport (answers QueueFull) and FirstWorkerPolicy; needs the in-memory Transport adapter that answers a set status, in modules/extension/transport (none exists) | tests/extension/offload_coordination/test_offload_dispatcher.cpp; modules/extension/transport/include/SushiRuntime/transport/transport.hpp; modules/extension/cluster_load/include/SushiRuntime/cluster_load/offload_policy.hpp
Medium_OffloadDispatcherTest.StoppedCoordinatorDeclines | rewrite | 1 | fixture stand-ins RecordingTransport and FirstWorkerPolicy; the test also asserts the policy's call count, a protocol detail; needs in-memory Transport and OffloadPolicy adapters (none exist) | tests/extension/offload_coordination/test_offload_dispatcher.cpp; modules/extension/transport/include/SushiRuntime/transport/transport.hpp; modules/extension/cluster_load/include/SushiRuntime/cluster_load/offload_policy.hpp
Medium_OffloadDispatcherTest.DynamicNodeResolvesItsLiveLists | rewrite | 1 | fixture stand-ins RecordingTransport and FirstWorkerPolicy; needs in-memory Transport and OffloadPolicy adapters (none exist) | tests/extension/offload_coordination/test_offload_dispatcher.cpp; modules/extension/transport/include/SushiRuntime/transport/transport.hpp; modules/extension/cluster_load/include/SushiRuntime/cluster_load/offload_policy.hpp
Medium_OffloadDispatcherTest.PrepareWarnsOnceForLocalOnlyOps | rewrite | 1 | a lambda recorder installed as the process-wide log sink, and the fixture stand-ins; needs an in-memory log sink in the logger's own module (LogCapture lives in test_runner, not there) plus in-memory Transport and OffloadPolicy adapters | tests/extension/offload_coordination/test_offload_dispatcher.cpp; modules/foundation/test_runner/include/SushiRuntime/test_runner/log_capture.hpp; modules/extension/transport/include/SushiRuntime/transport/transport.hpp; modules/extension/cluster_load/include/SushiRuntime/cluster_load/offload_policy.hpp
Medium_OffloadDispatcherTest.LocalNodeBumpsOnlyItsSharedWrites | rewrite | 1 | fixture stand-ins RecordingTransport and FirstWorkerPolicy; needs in-memory Transport and OffloadPolicy adapters (none exist) | tests/extension/offload_coordination/test_offload_dispatcher.cpp; modules/extension/transport/include/SushiRuntime/transport/transport.hpp; modules/extension/cluster_load/include/SushiRuntime/cluster_load/offload_policy.hpp
Medium_OffloadDispatcherTest.NodeThatWritesOneBufferTwiceStaysLocal | rewrite | 1 | a lambda recorder installed as the process-wide log sink, plus the fixture stand-ins; needs an in-memory log sink in the logger's module and in-memory Transport and OffloadPolicy adapters | tests/extension/offload_coordination/test_offload_dispatcher.cpp; modules/foundation/test_runner/include/SushiRuntime/test_runner/log_capture.hpp; modules/extension/transport/include/SushiRuntime/transport/transport.hpp; modules/extension/cluster_load/include/SushiRuntime/cluster_load/offload_policy.hpp
Medium_OffloadDispatcherTest.Regression_EachPlanPlacesByItsOwnRules | rewrite | 1 | control flow in the body (a loop over two rounds); the fixture stand-ins; rows replace the loop; needs in-memory Transport and OffloadPolicy adapters (none exist) | tests/extension/offload_coordination/test_offload_dispatcher.cpp; modules/extension/transport/include/SushiRuntime/transport/transport.hpp; modules/extension/cluster_load/include/SushiRuntime/cluster_load/offload_policy.hpp
Medium_OffloadDispatcherTest.FailRuleRefusesAnUntaggedNodeNamingTheRule | rewrite | 1 | RecordingSink stand-in for the OffloadSink port, plus the fixture stand-ins; needs an in-memory OffloadSink adapter in modules/engine/task (none exists; the engine tests define their own RecordingCoordinator) | tests/extension/offload_coordination/test_offload_dispatcher.cpp; modules/engine/task/include/SushiRuntime/task/offload_sink.hpp; modules/extension/transport/include/SushiRuntime/transport/transport.hpp; modules/extension/cluster_load/include/SushiRuntime/cluster_load/offload_policy.hpp
Medium_OffloadDispatcherTest.LocalRuleRunsNodesThatCannotTravelAndWarnsOnce | rewrite | 1 | RecordingSink and a lambda WARN capture on the process-wide log, control flow in the body (loops over rounds and nodes), plus the fixture stand-ins; the run-local promise is also in test_rule_placement.cpp, the WARN-once promise is only here | tests/extension/offload_coordination/test_offload_dispatcher.cpp; tests/extension/offload_coordination/test_rule_placement.cpp; modules/engine/task/include/SushiRuntime/task/offload_sink.hpp; modules/foundation/test_runner/include/SushiRuntime/test_runner/log_capture.hpp
Medium_OffloadDispatcherTest.RulePlacedNodeOutOfAttemptsFollowsItsPolicy | rewrite | 1 | RecordingSink and a WARN capture on the process-wide log, plus fixture stand-ins; two promises (Fail and Wait) to split in the rewrite; needs an in-memory OffloadSink adapter and a log sink adapter | tests/extension/offload_coordination/test_offload_dispatcher.cpp; modules/engine/task/include/SushiRuntime/task/offload_sink.hpp; modules/foundation/test_runner/include/SushiRuntime/test_runner/log_capture.hpp
Medium_OffloadDispatcherTest.NodeWithoutARuleOutOfAttemptsIsDeclined | rewrite | 1 | RecordingSink and a WARN capture on the process-wide log, plus fixture stand-ins; needs an in-memory OffloadSink adapter and a log sink adapter | tests/extension/offload_coordination/test_offload_dispatcher.cpp; modules/engine/task/include/SushiRuntime/task/offload_sink.hpp; modules/foundation/test_runner/include/SushiRuntime/test_runner/log_capture.hpp
```

### `tests/extension/offload_coordination/test_offload_handles.cpp`

```
Medium_OffloadHandles.InputsThenOutputs | resize | 8 | OffloadHandles: inputs first, outputs after; memory only, no clock, device, file or thread; small with no seam | tests/extension/offload_coordination/test_offload_handles.cpp; modules/extension/offload_coordination/include/SushiRuntime/offload_coordination/offload_handles.hpp
```

### `tests/extension/offload_coordination/test_rule_placement.cpp`

```
Small_RulePlacement.RuleForTheMasterRunsLocally | keep | 9 | RulePlacement: rule naming the master runs locally | tests/extension/offload_coordination/test_rule_placement.cpp; modules/extension/offload_coordination/include/SushiRuntime/offload_coordination/rule_placement.hpp; modules/extension/cluster_load/include/SushiRuntime/cluster_load/offload_policy.hpp
Small_RulePlacement.LocalRunsAnAbsentTargetOnTheMaster | keep | 9 | RulePlacement: Local runs an absent target on the master | tests/extension/offload_coordination/test_rule_placement.cpp; modules/extension/offload_coordination/include/SushiRuntime/offload_coordination/rule_placement.hpp
Small_RulePlacement.WaitEndsAtTheBoundFromTheFirstOffer | keep | 9 | RulePlacement: Wait ends at the bound from the first offer | tests/extension/offload_coordination/test_rule_placement.cpp; modules/extension/offload_coordination/include/SushiRuntime/offload_coordination/rule_placement.hpp
Small_RulePlacement.FailNamesTheRuleAndTheTarget | keep | 9 | RulePlacement: Fail names the rule or position and the target | tests/extension/offload_coordination/test_rule_placement.cpp; modules/extension/offload_coordination/include/SushiRuntime/offload_coordination/rule_placement.hpp
Small_RulePlacementDevice.Regression_ADeviceOnlyRuleTheMasterLacksIsNotTheMasters | keep | 9 | RulePlacement: device-only rule names the master only with that device | tests/extension/offload_coordination/test_rule_placement.cpp; modules/extension/offload_coordination/include/SushiRuntime/offload_coordination/rule_placement.hpp
Small_RulePlacement.Regression_AHugeWaitBoundWaitsUntilTheLatestTime | keep | 9 | RulePlacement: wait bound too large to add saturates | tests/extension/offload_coordination/test_rule_placement.cpp; modules/extension/offload_coordination/include/SushiRuntime/offload_coordination/rule_placement.hpp
Small_RulePlacement.FailRefusesANodeThatCannotTravel | keep | 9 | RulePlacement: decide_unreachable() with Fail gives InvalidArgument or InvalidState by cause | tests/extension/offload_coordination/test_rule_placement.cpp; modules/extension/offload_coordination/include/SushiRuntime/offload_coordination/rule_placement.hpp
Small_RulePlacement.LocalAndWaitRunANodeThatCannotTravelOnTheMaster | rewrite | 1 | control flow in the body: a loop over the actions Local and Wait; rows per action replace it; the Wait case is tested nowhere else in the module | tests/extension/offload_coordination/test_rule_placement.cpp; modules/extension/offload_coordination/include/SushiRuntime/offload_coordination/rule_placement.hpp
```

### `tests/extension/offload_coordination/test_task_description.cpp`

```
Medium_TaskDescription.TrailingZerosAreDropped | resize | 8 | TaskDescription: describe_task() trims trailing zero parameters; header-only pure function, no clock, device, file or thread; small | tests/extension/offload_coordination/test_task_description.cpp; modules/extension/offload_coordination/include/SushiRuntime/offload_coordination/task_description.hpp
Medium_TaskDescription.NoParameterSendsNone | merge | 4 | same promise as TrailingZerosAreDropped, with all-zero parameters as the other input; one row table takes both | tests/extension/offload_coordination/test_task_description.cpp; modules/extension/offload_coordination/include/SushiRuntime/offload_coordination/task_description.hpp
```

### `tests/extension/offload_coordination/test_task_resubmission.cpp`

```
Small_TaskResubmission.Regression_ARefusedReofferForgetsTheFirstOffer | rewrite | 1 | RefusingSink is a stand-in for the OffloadSink port; rewrite needs an in-memory OffloadSink adapter in modules/engine/task (none exists) | tests/extension/offload_coordination/test_task_resubmission.cpp; modules/engine/task/include/SushiRuntime/task/offload_sink.hpp
```

### `tests/extension/offload_coordination/test_worker_directory.cpp`

```
Medium_WorkerDirectory.UnregisteredPeerMessagesDropped | rewrite | 1 | fixture stand-in RecordingPolicy for OffloadPolicy, made by the fixture; a loop over message types in the body; needs an in-memory OffloadPolicy adapter in cluster_load (none exists; the real LeastLoadedOffloadPolicy can serve tests that assert no policy call) | tests/extension/offload_coordination/test_worker_directory.cpp; modules/extension/cluster_load/include/SushiRuntime/cluster_load/offload_policy.hpp
Medium_WorkerDirectory.HandshakeDeadlineClosesPeer | rewrite | 1 | fixture stand-in RecordingPolicy for OffloadPolicy; the test never calls the policy, the fixture's stub is the defect; the rewrite uses the real LeastLoadedOffloadPolicy | tests/extension/offload_coordination/test_worker_directory.cpp; modules/extension/cluster_load/include/SushiRuntime/cluster_load/offload_policy.hpp
Medium_WorkerDirectory.WaitForWorkersWakesOnRegistration | rewrite | 1 | fixture stand-in RecordingPolicy for OffloadPolicy; the thread test stays medium; wait_for_workers() takes a real timeout where the seam rule wants a clock the test advances, a question for the rewrite | tests/extension/offload_coordination/test_worker_directory.cpp; modules/extension/cluster_load/include/SushiRuntime/cluster_load/offload_policy.hpp
Medium_WorkerDirectory.WrongProofIsRejected | rewrite | 1 | fixture stand-in RecordingPolicy for OffloadPolicy; the test never calls the policy; the rewrite uses the real LeastLoadedOffloadPolicy | tests/extension/offload_coordination/test_worker_directory.cpp; modules/extension/cluster_load/include/SushiRuntime/cluster_load/offload_policy.hpp
Medium_WorkerDirectory.SecondHelloRestartsTheHandshake | rewrite | 1 | fixture stand-in RecordingPolicy for OffloadPolicy; the test never calls the policy; the rewrite uses the real LeastLoadedOffloadPolicy | tests/extension/offload_coordination/test_worker_directory.cpp; modules/extension/cluster_load/include/SushiRuntime/cluster_load/offload_policy.hpp
Medium_WorkerDirectory.SilentWorkerIsTakenOnce | rewrite | 1 | fixture stand-in RecordingPolicy, and the test asserts the ids the stub was offered, a call record; the rewrite uses the real policy and asserts the pick | tests/extension/offload_coordination/test_worker_directory.cpp; modules/extension/cluster_load/include/SushiRuntime/cluster_load/offload_policy.hpp
Medium_WorkerDirectory.PickSkipsWorkersAtTheirJobLimit | rewrite | 1 | fixture stand-in RecordingPolicy; asserts its call count and offered list; a loop-free body with several promises (limit, no policy call with no room, release, closed directory) to split in the rewrite | tests/extension/offload_coordination/test_worker_directory.cpp; modules/extension/cluster_load/include/SushiRuntime/cluster_load/offload_policy.hpp
Medium_WorkerDirectory.PickOffersOnlyWorkersWithTheKernel | rewrite | 1 | fixture stand-in RecordingPolicy; control flow in the body (a loop over four rounds); rows replace it | tests/extension/offload_coordination/test_worker_directory.cpp; modules/extension/cluster_load/include/SushiRuntime/cluster_load/offload_policy.hpp
Medium_WorkerDirectory.SnapshotReportsWhatTheWorkerSent | rewrite | 1 | fixture stand-in RecordingPolicy; the test calls pick() once, so the stub is the only defect; the rewrite uses the real policy | tests/extension/offload_coordination/test_worker_directory.cpp; modules/extension/cluster_load/include/SushiRuntime/cluster_load/offload_policy.hpp
Medium_WorkerDirectory.FactsFollowTheNameAndOnlyAProvenHelloReplacesThem | rewrite | 1 | fixture stand-in RecordingPolicy, which records facts the test reads back; a lambda with a branch in the body; several promises (facts from proven HELLO, unproven HELLO, replacement) to split | tests/extension/offload_coordination/test_worker_directory.cpp; modules/extension/cluster_load/include/SushiRuntime/cluster_load/offload_policy.hpp
Medium_WorkerDirectory.IdentityCarriesTheBuildOfTheLastHello | rewrite | 1 | fixture stand-in RecordingPolicy for OffloadPolicy; the test never calls the policy; the rewrite uses the real LeastLoadedOffloadPolicy | tests/extension/offload_coordination/test_worker_directory.cpp; modules/extension/cluster_load/include/SushiRuntime/cluster_load/offload_policy.hpp
Medium_WorkerDirectory.HelloFromARegisteredWorkerRegistersNothingTwice | rewrite | 1 | fixture stand-in RecordingPolicy for OffloadPolicy; the test calls pick() once, the rewrite uses the real policy | tests/extension/offload_coordination/test_worker_directory.cpp; modules/extension/cluster_load/include/SushiRuntime/cluster_load/offload_policy.hpp
Medium_WorkerDirectory.CloseRetiresEveryWorker | rewrite | 1 | fixture stand-in RecordingPolicy for OffloadPolicy; the test never calls the policy; the rewrite uses the real LeastLoadedOffloadPolicy | tests/extension/offload_coordination/test_worker_directory.cpp; modules/extension/cluster_load/include/SushiRuntime/cluster_load/offload_policy.hpp
```

### `tests/extension/pending_tasks/test_pending_tasks.cpp`

```
Medium_PendingTasks.TaskIDsRiseFromOne | resize | 8 | PendingTasks: ids start at 1 and rise; memory and a mutex only; small with no seam | tests/extension/pending_tasks/test_pending_tasks.cpp; modules/extension/pending_tasks/include/SushiRuntime/pending_tasks/pending_tasks.hpp; modules/extension/pending_tasks/source/pending_tasks.cpp
Medium_PendingTasks.InsertAfterCloseRefused | split | 3 | three promises: a duplicate id is refused; insert after close() is refused; the drain check duplicates DrainReturnsEveryNode and is dropped in the split | tests/extension/pending_tasks/test_pending_tasks.cpp; modules/extension/pending_tasks/include/SushiRuntime/pending_tasks/pending_tasks.hpp
Medium_PendingTasks.TakeIsExactlyOnce | split | 3 | two promises: take() returns the task with its outputs; a second take and the later removals find nothing | tests/extension/pending_tasks/test_pending_tasks.cpp; modules/extension/pending_tasks/include/SushiRuntime/pending_tasks/pending_tasks.hpp
Medium_PendingTasks.DrainReturnsEveryNode | rewrite | 1 | control flow in the body: a loop over drained results asserts each node; rows or per-id checks replace it; the promise is tested nowhere else in the module | tests/extension/pending_tasks/test_pending_tasks.cpp; modules/extension/pending_tasks/include/SushiRuntime/pending_tasks/pending_tasks.hpp
Medium_PendingTasks.LateResultLeavesInflightAlone | split | 3 | three promises: an unknown id takes nothing; a result for a returned task is refused; another worker's task is untouched | tests/extension/pending_tasks/test_pending_tasks.cpp; modules/extension/pending_tasks/include/SushiRuntime/pending_tasks/pending_tasks.hpp
Medium_PendingTasks.ForeignWorkerCannotTake | resize | 8 | PendingTasks: only the dispatched worker acts on a task; memory and a mutex only; small | tests/extension/pending_tasks/test_pending_tasks.cpp; modules/extension/pending_tasks/include/SushiRuntime/pending_tasks/pending_tasks.hpp
Medium_PendingTasks.ExpectsOutputOnlyAtTheDispatchedVersion | resize | 8 | PendingTasks: expects_output() true only at the dispatched version; memory only; small | tests/extension/pending_tasks/test_pending_tasks.cpp; modules/extension/pending_tasks/include/SushiRuntime/pending_tasks/pending_tasks.hpp
Medium_PendingTasks.UnacknowledgedTaskReapedWithDeadlineZero | split | 3 | two promises: an unacknowledged task is reaped at the acknowledge limit; a deadline of 0 bounds no accepted task | tests/extension/pending_tasks/test_pending_tasks.cpp; modules/extension/pending_tasks/include/SushiRuntime/pending_tasks/pending_tasks.hpp
Medium_PendingTasks.AcknowledgedTaskBelowOldestHeldReaped | split | 3 | three promises: two rows in a row of misses reap as LostResult; a heartbeat listing the task again restarts the count; a worker that sent no heartbeat loses nothing | tests/extension/pending_tasks/test_pending_tasks.cpp; modules/extension/pending_tasks/include/SushiRuntime/pending_tasks/pending_tasks.hpp
Medium_PendingTasks.HeartbeatLeavesAnUnacknowledgedTaskToItsOwnLimit | split | 3 | two checks: a heartbeat does not reap an unacknowledged task before its limit (kept); the reap at limit plus one repeats UnacknowledgedTaskReapedWithDeadlineZero (dropped) | tests/extension/pending_tasks/test_pending_tasks.cpp; modules/extension/pending_tasks/include/SushiRuntime/pending_tasks/pending_tasks.hpp
Medium_PendingTasks.ChunkRestartsTheDeadline | resize | 8 | PendingTasks: a chunk restarts the worker's deadlines; memory only; small | tests/extension/pending_tasks/test_pending_tasks.cpp; modules/extension/pending_tasks/include/SushiRuntime/pending_tasks/pending_tasks.hpp
Medium_PendingTasks.EarliestDeadlineFollowsTheOwnDeadlines | split | 3 | three promises: earliest_deadline() publishes only own deadlines; a chunk moves it and take_overdue() makes it exact; a take leaves none; the exact plus-one-tick check goes beyond the header's at-or-before promise, a question 5 point for the rewrite of the second part | tests/extension/pending_tasks/test_pending_tasks.cpp; modules/extension/pending_tasks/include/SushiRuntime/pending_tasks/pending_tasks.hpp; modules/extension/pending_tasks/source/pending_tasks.cpp
Medium_PendingTasks.OwnDeadlineReplacesTheClusterDeadline | rewrite | 1 | control flow in the body: a loop acknowledges three tasks; three explicit calls replace it; the promise is tested nowhere else in the module | tests/extension/pending_tasks/test_pending_tasks.cpp; modules/extension/pending_tasks/include/SushiRuntime/pending_tasks/pending_tasks.hpp
Medium_PendingTasks.TakeForWorkerForgetsItsChunkTime | resize | 8 | PendingTasks: take_for_worker() forgets the chunk time; memory and a mutex only; small | tests/extension/pending_tasks/test_pending_tasks.cpp; modules/extension/pending_tasks/include/SushiRuntime/pending_tasks/pending_tasks.hpp
Medium_PendingTasks.TwoRemovalsOfOneTaskHaveOneWinner | rewrite | 1 | control flow in the body: a loop over 200 rounds and a conditional in the winner count; the race promise is tested nowhere else in the module; the rewrite keeps the two threads and makes one round per row | tests/extension/pending_tasks/test_pending_tasks.cpp; modules/extension/pending_tasks/source/pending_tasks.cpp
```

### `tests/extension/process_environment/test_environment.cpp`

```
Medium_EnvironmentTest.ReadsASetVariableAndReportsAnUnsetOne | resize | 8 | read_environment(): the set and unset decision is logic over a lookup that reaches the process environment; the test depends on PATH being set; a lookup seam is built first, then the decision runs small, and one medium boundary test names no variable the machine may lack | tests/extension/process_environment/test_environment.cpp; modules/extension/process_environment/source/environment.cpp
```

### `tests/extension/process_environment/test_process_environment_log_category.cpp`

```
Small_ProcessEnvironmentLogCategory.TheCategoryIsNamedAfterTheModule | delete | 1 | forbidden kind: a test of a log category, with SUCCEED() and static_assert run as the test; the name of a log category states no promise | tests/extension/process_environment/test_process_environment_log_category.cpp; tests/extension/offload_coordination/test_offload_coordination_log_category.cpp
```

### `tests/extension/process_environment/test_stop_signal.cpp`

```
Medium_StopSignalTest.RequestSetsTheFlagAndANewObjectClearsIt | split | 3 | two promises joined by "And": request() sets the flag; a new object starts clear; the default constructor installs the real process handlers, so the rewrite uses the installer seam the second test already uses | tests/extension/process_environment/test_stop_signal.cpp; modules/extension/process_environment/include/SushiRuntime/process_environment/stop_signal.hpp; modules/extension/process_environment/source/stop_signal.cpp
Small_StopSignal.LogsAHandlerThatCouldNotBeInstalled | split | 3 | two promises: a refused installation is logged once per signal as a WARN naming it, through LogCapture on the process-wide logger (question 8 seam); the flag still works after a refusal | tests/extension/process_environment/test_stop_signal.cpp; modules/foundation/test_runner/include/SushiRuntime/test_runner/log_capture.hpp; modules/extension/process_environment/source/stop_signal.cpp
```

### `tests/extension/transport/test_buffer_handle.cpp`

```
Medium_BufferHandle.IDZeroIsInvalid | rewrite | 1 | forbidden kind: static_assert run as the test, and an equality of values that holds by construction; the valid() sentinel is the only test of valid() in the module; the rewrite is an EXPECT pair with the equality checks dropped | tests/extension/transport/test_buffer_handle.cpp; modules/extension/transport/include/SushiRuntime/transport/buffer_handle.hpp
Medium_BufferHandle.DefaultsNameNoBuffer | delete | 1 | forbidden kind: a test of trivial code, the default values of a struct; its sentinel check moves into the IDZeroIsInvalid rewrite | tests/extension/transport/test_buffer_handle.cpp; modules/extension/transport/include/SushiRuntime/transport/buffer_handle.hpp
```

### `tests/extension/transport/test_endpoint.cpp`

```
Medium_EndpointTest.ParseTable | resize | 8 | Endpoint: each accepted and refused text form, by status, host and port; a row table, and endpoint.cpp has no name lookup or clock, so parse is pure; small | tests/extension/transport/test_endpoint.cpp; modules/extension/transport/include/SushiRuntime/transport/endpoint.hpp; modules/extension/transport/source/endpoint.cpp
Medium_EndpointTest.ParseFailureSaysWhatWasWrongWithWhichText | resize | 8 | Endpoint: the refusal message quotes the text and names both forms; pure string work; small | tests/extension/transport/test_endpoint.cpp; modules/extension/transport/include/SushiRuntime/transport/endpoint.hpp; modules/extension/transport/source/endpoint.cpp
Medium_EndpointTest.FormatParsesBack | rewrite | 1 | control flow in the body: a loop over three endpoints with asserts inside; each endpoint becomes a row; the round trip is tested nowhere else in the module | tests/extension/transport/test_endpoint.cpp; modules/extension/transport/include/SushiRuntime/transport/endpoint.hpp
```

### `tests/extension/transport/test_message_type.cpp`

```
Medium_MessageType.SixteenTypesFromOne | rewrite | 1 | forbidden kind: three static_asserts run as the test, and constant checks; the is_message_type() range is the only test of that promise in the module; the rewrite keeps the EXPECT lines for bytes 0, 1, 16 and 17 | tests/extension/transport/test_message_type.cpp; modules/extension/transport/include/SushiRuntime/transport/message_type.hpp
Medium_MessageType.NamesAreTheWireNames | rewrite | 1 | forbidden kind: EXPECT_STREQ over a constant name table, which is trivial code; the UNKNOWN fallback for a byte outside the enumeration is the only test of that promise; the rewrite keeps that fallback | tests/extension/transport/test_message_type.cpp; modules/extension/transport/include/SushiRuntime/transport/message_type.hpp
```

### `tests/extension/transport/test_transport.cpp`

```
Medium_Transport.ADefaultResultIsOk | delete | 1 | forbidden kind: a test of a default value, trivial code; the is_ok() promise is a gap, listed under Adds | tests/extension/transport/test_transport.cpp; modules/extension/transport/include/SushiRuntime/transport/transport.hpp
Medium_Transport.PeersCompareByValue | delete | 1 | forbidden kind: comparisons of constants and of a value with itself, trivial code; states no promise beyond the compiler's equality | tests/extension/transport/test_transport.cpp; modules/extension/transport/include/SushiRuntime/transport/transport.hpp
```

### `tests/extension/wire/test_message_router.cpp`

```
Medium_WireFormat.RouterDeliversByType | resize | 8 | MessageRouter: route() delivers by type and reports Unhandled otherwise; memory only, no clock, device, file or thread; small | tests/extension/wire/test_message_router.cpp; modules/extension/wire/include/SushiRuntime/wire/message_router.hpp
```

### `tests/extension/wire/test_wire_codec.cpp`

```
Medium_WireCodec.SeededDispatchesSurviveAWholeFrame | rewrite | 1 | control flow in the body: a loop over 200 rounds and a loop over parameters; a seeded property with a bounded count replaces it, as "What a change carries" gives; the frame-level round trip of a random dispatch is tested nowhere else in the module | tests/extension/wire/test_wire_codec.cpp; modules/extension/wire/include/SushiRuntime/wire/wire_codec.hpp; modules/extension/wire/include/SushiRuntime/wire/wire_frame.hpp
Medium_WireCodecFuzz.TaskResultRefusesACountItCannotHold | merge | 4 | same promise as the list-count rows of ListCountBoundedByRemainingBytes and HelloListCountsAreBoundedByRemainingBytes, with a TaskResult count as the other input; one rows test takes all three | tests/extension/wire/test_wire_codec.cpp; tests/extension/wire/test_wire_messages.cpp; modules/extension/wire/include/SushiRuntime/wire/wire_codec.hpp
Medium_WireCodecFuzz.SeededMutationsDecodeOrFailInsideThePayload | rewrite | 1 | control flow in the body: nested loops over samples and rounds with an ASSERT inside; the helper sample_of asserts but is not named expect_ or assert_; a fuzz target with a small corpus replay replaces it, as "Proof of value" gives | tests/extension/wire/test_wire_codec.cpp; modules/extension/wire/include/SushiRuntime/wire/wire_messages.hpp; modules/extension/wire/include/SushiRuntime/wire/wire_codec.hpp
Medium_WireCodecFuzz.SeededHeadersPassOnlyInsideTheirRanges | rewrite | 1 | control flow in the body: a loop with branches and continue; a fuzz target with a small corpus replay replaces it; the range promise of the header is seen by no other test in the module at this breadth | tests/extension/wire/test_wire_codec.cpp; modules/extension/wire/include/SushiRuntime/wire/wire_frame.hpp
```

### `tests/extension/wire/test_wire_frame.cpp`

```
Medium_WireFormat.APeerThatAnnouncesVersionTwoIsRefusedWithProtocolMismatch | merge | 4 | same promise as APeerThatAnnouncesVersionThreeIsRefusedWithProtocolMismatch with version 2 as the input; the rows test replaces both; the decode check of version 2 also appears in RejectsOutOfRangeEnumAndBool | tests/extension/wire/test_wire_frame.cpp; tests/extension/wire/test_wire_messages.cpp; modules/extension/wire/include/SushiRuntime/wire/wire_frame.hpp
Medium_WireFormat.APeerThatAnnouncesVersionThreeIsRefusedWithProtocolMismatch | merge | 4 | same promise as APeerThatAnnouncesVersionTwoIsRefusedWithProtocolMismatch with version 3 as the input; the rows test replaces both | tests/extension/wire/test_wire_frame.cpp; modules/extension/wire/include/SushiRuntime/wire/wire_frame.hpp
Medium_WireFormat.FrameHeaderGolden | split | 3 | three golden promises: the 12-byte header encodes and decodes; the last-chunk flag decodes; the PING frame bytes match | tests/extension/wire/test_wire_frame.cpp; modules/extension/wire/include/SushiRuntime/wire/wire_frame.hpp; modules/extension/wire/include/SushiRuntime/wire/wire_messages.hpp
Medium_WireFormat.MagicReadsAsOversizedV1Length | rewrite | 1 | control flow in the body: a loop rebuilds a length from four bytes, a decoder inside the test; the 256 MiB cap of a version 1 reader is named only here, while K_MAX_FRAME_PAYLOAD in wire_frame.hpp is 1 MiB; the rewrite gives the expected value as a literal and names the source of the cap or drops the claim | tests/extension/wire/test_wire_frame.cpp; modules/extension/wire/include/SushiRuntime/wire/wire_frame.hpp
```

### `tests/extension/wire/test_wire_messages.cpp`

```
Medium_WireFormat.GoldenBytes_Hello | resize | 8 | Wire messages: HELLO encodes to and decodes from the written bytes; memory only; small | tests/extension/wire/test_wire_messages.cpp; modules/extension/wire/include/SushiRuntime/wire/wire_codec.hpp; modules/extension/wire/source/wire_messages.cpp
Medium_WireFormat.HelloWithTwoDevicesAndTwoLabelsDecodes | resize | 8 | Wire messages: HELLO devices and labels round-trip in order, empty strings included; memory only; small | tests/extension/wire/test_wire_messages.cpp; modules/extension/wire/include/SushiRuntime/wire/wire_codec.hpp; modules/extension/wire/source/wire_messages.cpp
Medium_WireFormat.GoldenBytes_Challenge | resize | 8 | Wire messages: CHALLENGE golden both ways; memory only; small | tests/extension/wire/test_wire_messages.cpp; modules/extension/wire/include/SushiRuntime/wire/wire_codec.hpp
Medium_WireFormat.GoldenBytes_Proof | resize | 8 | Wire messages: PROOF golden both ways; memory only; small | tests/extension/wire/test_wire_messages.cpp; modules/extension/wire/include/SushiRuntime/wire/wire_codec.hpp
Medium_WireFormat.GoldenBytes_Welcome | resize | 8 | Wire messages: WELCOME golden both ways; memory only; small | tests/extension/wire/test_wire_messages.cpp; modules/extension/wire/include/SushiRuntime/wire/wire_codec.hpp
Medium_WireFormat.GoldenBytes_Reject | resize | 8 | Wire messages: REJECT golden both ways; memory only; small | tests/extension/wire/test_wire_messages.cpp; modules/extension/wire/include/SushiRuntime/wire/wire_codec.hpp
Medium_WireFormat.GoldenBytes_Goodbye | resize | 8 | Wire messages: GOODBYE golden both ways; memory only; small | tests/extension/wire/test_wire_messages.cpp; modules/extension/wire/include/SushiRuntime/wire/wire_codec.hpp
Medium_WireFormat.GoldenBytes_TaskDispatch | resize | 8 | Wire messages: TASK_DISPATCH golden both ways, zero parameters off the wire; memory only; small | tests/extension/wire/test_wire_messages.cpp; modules/extension/wire/include/SushiRuntime/wire/wire_codec.hpp
Medium_WireFormat.GoldenBytes_TaskAccepted | resize | 8 | Wire messages: TASK_ACCEPTED golden both ways; memory only; small | tests/extension/wire/test_wire_messages.cpp; modules/extension/wire/include/SushiRuntime/wire/wire_codec.hpp
Medium_WireFormat.GoldenBytes_TaskResult | resize | 8 | Wire messages: TASK_RESULT golden both ways; memory only; small | tests/extension/wire/test_wire_messages.cpp; modules/extension/wire/include/SushiRuntime/wire/wire_codec.hpp
Medium_WireFormat.GoldenBytes_BufferRequest | resize | 8 | Wire messages: BUFFER_REQUEST golden both ways; memory only; small | tests/extension/wire/test_wire_messages.cpp; modules/extension/wire/include/SushiRuntime/wire/wire_codec.hpp
Medium_WireFormat.GoldenBytes_BufferChunk | resize | 8 | Wire messages: BUFFER_CHUNK golden both ways, data to the end; memory only; small | tests/extension/wire/test_wire_messages.cpp; modules/extension/wire/include/SushiRuntime/wire/wire_codec.hpp
Medium_WireFormat.GoldenBytes_BufferAbort | resize | 8 | Wire messages: BUFFER_ABORT golden both ways; memory only; small | tests/extension/wire/test_wire_messages.cpp; modules/extension/wire/include/SushiRuntime/wire/wire_codec.hpp
Medium_WireFormat.GoldenBytes_BufferRelease | resize | 8 | Wire messages: BUFFER_RELEASE golden both ways; memory only; small | tests/extension/wire/test_wire_messages.cpp; modules/extension/wire/include/SushiRuntime/wire/wire_codec.hpp
Medium_WireFormat.GoldenBytes_Ping | resize | 8 | Wire messages: PING golden both ways; memory only; small | tests/extension/wire/test_wire_messages.cpp; modules/extension/wire/include/SushiRuntime/wire/wire_codec.hpp
Medium_WireFormat.GoldenBytes_Pong | resize | 8 | Wire messages: PONG golden both ways; memory only; small | tests/extension/wire/test_wire_messages.cpp; modules/extension/wire/include/SushiRuntime/wire/wire_codec.hpp
Medium_WireFormat.GoldenBytes_Heartbeat | resize | 8 | Wire messages: HEARTBEAT golden both ways; memory only; small | tests/extension/wire/test_wire_messages.cpp; modules/extension/wire/include/SushiRuntime/wire/wire_codec.hpp
Medium_WireFormat.EveryTruncationFailsCleanly | rewrite | 1 | control flow in the body: nested loops over cases, payload prefixes and frame prefixes, with a continue; the prefixes are built inside the loops; rows per message type with a builder replace them; the prefix promise is tested nowhere else in the module | tests/extension/wire/test_wire_messages.cpp; modules/extension/wire/include/SushiRuntime/wire/wire_frame.hpp
Medium_WireFormat.RejectsOutOfRangeEnumAndBool | split | 3 | several promises: a bool outside 0 and 1 is refused; each enumeration outside its range is refused, per message; a header field outside its range is refused; the version 2 header check repeats the frame-header rows and is dropped | tests/extension/wire/test_wire_messages.cpp; tests/extension/wire/test_wire_frame.cpp; modules/extension/wire/include/SushiRuntime/wire/wire_codec.hpp; modules/extension/wire/include/SushiRuntime/wire/wire_frame.hpp
Medium_WireFormat.ListCountBoundedByRemainingBytes | merge | 4 | same promise as HelloListCountsAreBoundedByRemainingBytes and TaskResultRefusesACountItCannotHold, over Proof ops and TaskDispatch inputs; one rows test takes all three | tests/extension/wire/test_wire_messages.cpp; tests/extension/wire/test_wire_codec.cpp; modules/extension/wire/include/SushiRuntime/wire/wire_codec.hpp
Medium_WireFormat.HelloListCountsAreBoundedByRemainingBytes | merge | 4 | same promise as ListCountBoundedByRemainingBytes, over HELLO device and label counts; one rows test takes both | tests/extension/wire/test_wire_messages.cpp; modules/extension/wire/include/SushiRuntime/wire/wire_codec.hpp
Medium_WireFormat.HelloRefusesAnUnknownOperatingSystemAndDeviceKind | merge | 4 | same enumeration-range promise as RejectsOutOfRangeEnumAndBool, over HELLO operating system and device kind; the rows test takes both | tests/extension/wire/test_wire_messages.cpp; modules/extension/wire/include/SushiRuntime/wire/wire_codec.hpp
Medium_WireFormat.HelloRefusesAStringAbove255Bytes | merge | 4 | same string-length promise as StringLengthBoundedAt255, over a HELLO device name and label key; the rows test takes both | tests/extension/wire/test_wire_messages.cpp; modules/extension/wire/include/SushiRuntime/wire/wire_codec.hpp
Medium_WireFormat.RejectsTrailingBytes | resize | 8 | Wire messages: bytes after the last field are refused at that field's offset; memory only; small | tests/extension/wire/test_wire_messages.cpp; modules/extension/wire/include/SushiRuntime/wire/wire_codec.hpp
Medium_WireFormat.StringLengthBoundedAt255 | resize | 8 | Wire messages: a string above 255 bytes is refused on encode and decode, and the frame stays unchanged; this test owns the string rows; memory only; small | tests/extension/wire/test_wire_messages.cpp; modules/extension/wire/include/SushiRuntime/wire/wire_codec.hpp
```

### `tests/foundation/core/test_allocation.cpp`

```
Small_Allocation.ReleasesOnceOnDestruction | rewrite | 1 | a Detail:: symbol: the test names SushiRuntime::Detail::Allocation (lines 40-41) and the header keeps Allocation in Detail. Promise: a destroyed allocation releases its address once | tests/foundation/core/test_allocation.cpp; modules/foundation/core/include/SushiRuntime/core/allocation.hpp
Small_Allocation.MoveTransfersTheReleaseAndEmptiesTheSource | rewrite | 1 | a Detail:: symbol (see ReleasesOnceOnDestruction). The name joins two results with "and", a split candidate once the Detail question is ruled | tests/foundation/core/test_allocation.cpp; modules/foundation/core/include/SushiRuntime/core/allocation.hpp
Small_Allocation.MoveAssignmentReleasesTheOverwrittenAddressFirst | rewrite | 1 | a Detail:: symbol (see ReleasesOnceOnDestruction). Promise: move-assign releases the old address first | tests/foundation/core/test_allocation.cpp; modules/foundation/core/include/SushiRuntime/core/allocation.hpp
Small_Allocation.AnEmptyAllocationHasNoAddressOrOwner | rewrite | 1 | a Detail:: symbol (see ReleasesOnceOnDestruction). The name says "owner", the body checks only empty() and bytes() | tests/foundation/core/test_allocation.cpp; modules/foundation/core/include/SushiRuntime/core/allocation.hpp
Small_Allocation.ResetOnANonNullAddressWithNoOwnerReleasesNothing | rewrite | 1 | a Detail:: symbol, and EXPECT_NO_THROW around reset(), which is declared noexcept (allocation.hpp reset). Promise: reset on an ownerless allocation empties it and releases nothing | tests/foundation/core/test_allocation.cpp; modules/foundation/core/include/SushiRuntime/core/allocation.hpp
```

### `tests/foundation/core/test_assert.cpp`

```
Small_Assert.AssertStopsTheProcessAndNamesItsSite | resize | 8 | the process stops through the process-wide handler (assert.cpp installed_handler, fail_assertion ends in abort). Split the size: the message and site are small through a seam where fail_assertion takes the handler as an argument and returns; the stop stays one medium death test | tests/foundation/core/test_assert.cpp; modules/foundation/core/include/SushiRuntime/core/assert.hpp; modules/foundation/core/source/assert.cpp
Small_Assert.VerifyStopsTheProcessAndNamesItsSite | resize | 8 | same process-wide stop and seam as AssertStops | tests/foundation/core/test_assert.cpp; modules/foundation/core/include/SushiRuntime/core/assert.hpp; modules/foundation/core/source/assert.cpp
Small_Assert.FatalStopsTheProcessAndNamesItsSite | resize | 8 | same process-wide stop and seam as AssertStops | tests/foundation/core/test_assert.cpp; modules/foundation/core/include/SushiRuntime/core/assert.hpp; modules/foundation/core/source/assert.cpp
Small_Assert.ATrueConditionLetsTheProcessContinue | keep | 9 | the condition is evaluated once and the process continues | tests/foundation/core/test_assert.cpp; modules/foundation/core/include/SushiRuntime/core/assert.hpp
Small_Assert.AReplacedHandlerIsCalledBeforeTheProcessStops | resize | 8 | the handler is installed in a child process and the stop is real. Seam: the handler-argument form of fail_assertion lets the handler call be checked small; the stop stays medium | tests/foundation/core/test_assert.cpp; modules/foundation/core/include/SushiRuntime/core/assert.hpp; modules/foundation/core/source/assert.cpp
Small_Assert.SettingAHandlerReturnsTheOneItReplaces | resize | 8 | writes the process-wide installed_handler (assert.cpp line 50-53). Seam: a handler slot object that an instance owns, which the global wraps; the exchange rule is then tested small on that instance | tests/foundation/core/test_assert.cpp; modules/foundation/core/include/SushiRuntime/core/assert.hpp; modules/foundation/core/source/assert.cpp
```

### `tests/foundation/core/test_bit.cpp`

```
Small_Bit.PopcountCountsEverySetBit | keep | 9 | popcount_u64 counts set bits, including empty, full and mixed words | tests/foundation/core/test_bit.cpp; modules/foundation/core/include/SushiRuntime/core/bit.hpp
Small_Bit.CountrZeroFindsTheLowestSetBit | keep | 9 | countr_zero_u64 returns the lowest set bit, 64 for zero | tests/foundation/core/test_bit.cpp; modules/foundation/core/include/SushiRuntime/core/bit.hpp
```

### `tests/foundation/core/test_common.cpp`

```
Small_Common.ConstantsHoldTheirDocumentedValues | delete | 1 | forbidden kind: static_assert run as a test, then SUCCEED() with no runtime assertion; the body states constant values only. The static_asserts belong in common.hpp. Siblings use K_MAX_WORKER_THREADS as a symbol only (tests/engine/scheduler/test_task_scheduler.cpp 923-932), not its value | tests/foundation/core/test_common.cpp; modules/foundation/core/include/SushiRuntime/core/common.hpp; tests/engine/scheduler/test_task_scheduler.cpp
Small_Common.AlignmentsArePowersOfTwo | rewrite | 1 | forbidden kind: a test of a constant. The real promise (default alignment is a power of two, a cache-line multiple) has no other test: K_DEFAULT_ALIGNMENT is read by no test. Rewrite against the allocator that returns an address aligned to it | tests/foundation/core/test_common.cpp; modules/foundation/core/include/SushiRuntime/core/common.hpp; modules/foundation/core/include/SushiRuntime/core/allocation.hpp
```

### `tests/foundation/core/test_delegate.cpp`

```
Small_Delegate.EmptyIsFalsy | keep | 9 | an empty Delegate is falsy from default and from nullptr | tests/foundation/core/test_delegate.cpp; modules/foundation/core/include/SushiRuntime/core/delegate.hpp
Small_Delegate.InvokesAndReturns | keep | 9 | invocation returns the callable's result | tests/foundation/core/test_delegate.cpp; modules/foundation/core/include/SushiRuntime/core/delegate.hpp
Small_Delegate.ForwardsArguments | keep | 9 | arguments reach the callable | tests/foundation/core/test_delegate.cpp; modules/foundation/core/include/SushiRuntime/core/delegate.hpp
Small_Delegate.CapturesState | keep | 9 | captured state is held and read on each call | tests/foundation/core/test_delegate.cpp; modules/foundation/core/include/SushiRuntime/core/delegate.hpp
Small_Delegate.VoidReturn | keep | 9 | a void signature runs the callable and its effect is seen | tests/foundation/core/test_delegate.cpp; modules/foundation/core/include/SushiRuntime/core/delegate.hpp
Small_Delegate.VoidSignatureDiscardsResultOfValueCallable | keep | 9 | a value-returning callable under a void signature has its result dropped | tests/foundation/core/test_delegate.cpp; modules/foundation/core/include/SushiRuntime/core/delegate.hpp
Small_Delegate.MoveLeavesSourceEmpty | keep | 9 | a move empties the source and the target calls | tests/foundation/core/test_delegate.cpp; modules/foundation/core/include/SushiRuntime/core/delegate.hpp
Small_Delegate.MoveAssignReplacesTarget | keep | 9 | move-assign replaces the target and empties the source | tests/foundation/core/test_delegate.cpp; modules/foundation/core/include/SushiRuntime/core/delegate.hpp
Small_Delegate.DestroysTarget | keep | 9 | destroying the Delegate destroys the erased lambda and its shared_ptr copy | tests/foundation/core/test_delegate.cpp; modules/foundation/core/include/SushiRuntime/core/delegate.hpp
Small_Delegate.HoldsMoveOnlyTarget | keep | 9 | a move-only target is held and survives a move | tests/foundation/core/test_delegate.cpp; modules/foundation/core/include/SushiRuntime/core/delegate.hpp
Small_Delegate.HoldsNothrowMovableMoveOnlyTarget | merge | 4 | same promise as HoldsMoveOnlyTarget, same file, same capture. Its two static_asserts check the test's own lambda, not Delegate. Merge into HoldsMoveOnlyTarget | tests/foundation/core/test_delegate.cpp; modules/foundation/core/include/SushiRuntime/core/delegate.hpp
```

### `tests/foundation/core/test_determinism.cpp`

```
Small_Determinism.EnumeratorsHoldDistinctValues | keep | 9 | the four enumerators differ | tests/foundation/core/test_determinism.cpp; modules/foundation/core/include/SushiRuntime/core/determinism.hpp
Small_Determinism.UnstatedIsTheZeroValue | keep | 9 | the stored number 0 means Unstated; the number is a contract a file relies on, as ErrorCode numbers are | tests/foundation/core/test_determinism.cpp; modules/foundation/core/include/SushiRuntime/core/determinism.hpp
Small_Determinism.IsOneUnsignedByte | keep | 9 | width and underlying type are part of the layout promise | tests/foundation/core/test_determinism.cpp; modules/foundation/core/include/SushiRuntime/core/determinism.hpp
```

### `tests/foundation/core/test_error.cpp`

```
Small_Error.EveryEnumeratorKeepsItsNumber | keep | 9 | the stored numbers cross the library boundary; the test pins them | tests/foundation/core/test_error.cpp; modules/foundation/core/include/SushiRuntime/core/error.hpp
Small_Error.EveryEnumeratorHasItsOwnName | keep | 9 | one name per enumerator, each checked by one line | tests/foundation/core/test_error.cpp; modules/foundation/core/include/SushiRuntime/core/error.hpp
Small_Error.AValueOutsideTheEnumIsNamedUnknown | keep | 9 | out-of-range values are named Unknown | tests/foundation/core/test_error.cpp; modules/foundation/core/include/SushiRuntime/core/error.hpp
```

### `tests/foundation/core/test_error_translation.cpp`

```
Small_ErrorTranslation.AReturningFunctionGivesAValue | rewrite | 1 | a Detail:: symbol: Detail::translate_exceptions (lines 45, 72). translate_exceptions sits in namespace Detail in error_translation.hpp, the only door. Promise: a returning callable gives a success and runs once | tests/foundation/core/test_error_translation.cpp; modules/foundation/core/include/SushiRuntime/core/error_translation.hpp; tests/foundation/sycl/test_error_translation.cpp
Small_ErrorTranslation.AStandardExceptionKeepsItsText | rewrite | 1 | a Detail:: symbol (see AReturningFunctionGivesAValue). Promise: a std::exception keeps its text under the given code | tests/foundation/core/test_error_translation.cpp; modules/foundation/core/include/SushiRuntime/core/error_translation.hpp
Small_ErrorTranslation.BadAllocIsAllocationFailed | rewrite | 1 | a Detail:: symbol (see AReturningFunctionGivesAValue). Promise: std::bad_alloc becomes AllocationFailed | tests/foundation/core/test_error_translation.cpp; modules/foundation/core/include/SushiRuntime/core/error_translation.hpp
Small_ErrorTranslation.AnUnnamedThrowGetsTheFixedMessage | rewrite | 1 | a Detail:: symbol, including Detail::K_UNNAMED_EXCEPTION_MESSAGE (line 75). Promise: a non-exception throw gets the fixed message and KernelFailed | tests/foundation/core/test_error_translation.cpp; modules/foundation/core/include/SushiRuntime/core/error_translation.hpp
```

### `tests/foundation/core/test_export.cpp`

```
Small_Export.ShortSpellingExpandsLikeTheLongOne | keep | 9 | SR_API is an alias of SUSHIRUNTIME_API for consumers | tests/foundation/core/test_export.cpp; modules/foundation/core/include/SushiRuntime/core/export.hpp
Small_Export.AConsumerImportsTheSymbols | rewrite | 5 | the oracle is the spelling of the macro (dllimport or visibility text), not an exported symbol. Fails when the attribute is renamed while the promise (consumers link and call the symbols) holds. Rewrite as a link and call from a consumer, medium | tests/foundation/core/test_export.cpp; modules/foundation/core/include/SushiRuntime/core/export.hpp
```

### `tests/foundation/core/test_node_cost.cpp`

```
Small_NodeCost.NamesItsDtype | keep | 9 | each dtype name is written into records; nullptr for Unstated | tests/foundation/core/test_node_cost.cpp; modules/foundation/core/include/SushiRuntime/core/node_cost.hpp
Small_NodeCost.ADefaultCostHoldsZerosAndNoDtype | rewrite | 1 | forbidden kind: a default value. The zero fields are field initialisers and state nothing. The Unstated dtype is the real part and has no other test except NamesItsDtype. Rewrite to state only that Unstated is the default dtype | tests/foundation/core/test_node_cost.cpp; modules/foundation/core/include/SushiRuntime/core/node_cost.hpp
Small_NodeCost.EqualityComparesEveryField | keep | 9 | equality compares flops, bytes and dtype | tests/foundation/core/test_node_cost.cpp; modules/foundation/core/include/SushiRuntime/core/node_cost.hpp
Small_NodeCost.AnOptionalCostTellsAnUnstatedCostFromAStatedZero | delete | 2 | it tests std::optional, not NodeCost. NodeCost is used only for its default. Fails the same way with any type; it would not fail if NodeCost broke | tests/foundation/core/test_node_cost.cpp; modules/foundation/core/include/SushiRuntime/core/node_cost.hpp
```

### `tests/foundation/core/test_operation_id.cpp`

```
Small_OperationID.HashIsFNV1a | keep | 9 | FNV-1a offset basis and a published value ("a" = 0xE40C292C) | tests/foundation/core/test_operation_id.cpp; modules/foundation/core/include/SushiRuntime/core/operation_id.hpp
Small_OperationID.LiteralMatchesTheExplicitHash | keep | 9 | the _op literal equals the explicit hash | tests/foundation/core/test_operation_id.cpp; modules/foundation/core/include/SushiRuntime/core/operation_id.hpp
Small_OperationID.DefaultIsUnnamedAndOrderFollowsTheValue | split | 3 | the name joins two promises that break apart: the default names nothing (split into DefaultIsUnnamed), and ordering follows the value (split into OrderFollowsTheValue) | tests/foundation/core/test_operation_id.cpp; modules/foundation/core/include/SushiRuntime/core/operation_id.hpp
```

### `tests/foundation/core/test_placement_class.cpp`

```
Small_PlacementClass.ValueIsTheHashOfTheWholeName | keep | 9 | value is OperationID::hash of the whole name | tests/foundation/core/test_placement_class.cpp; modules/foundation/core/include/SushiRuntime/core/placement_class.hpp; modules/foundation/core/include/SushiRuntime/core/operation_id.hpp
Small_PlacementClass.ThreeSegmentNameHoldsTwoPrefixes | keep | 9 | prefixes of a three-segment name | tests/foundation/core/test_placement_class.cpp; modules/foundation/core/include/SushiRuntime/core/placement_class.hpp
Small_PlacementClass.OneSegmentNameHoldsNoPrefix | keep | 9 | a one-segment name has no prefix | tests/foundation/core/test_placement_class.cpp; modules/foundation/core/include/SushiRuntime/core/placement_class.hpp
Small_PlacementClass.SixSegmentNameKeepsFourPrefixes | keep | 9 | the depth limit is 4; boundary at 4 and 5 segments | tests/foundation/core/test_placement_class.cpp; modules/foundation/core/include/SushiRuntime/core/placement_class.hpp
Small_PlacementClass.HasPrefixMatchesStrictPrefixesOnly | keep | 9 | has_prefix is false for the whole name | tests/foundation/core/test_placement_class.cpp; modules/foundation/core/include/SushiRuntime/core/placement_class.hpp
Small_PlacementClass.DefaultIsUnnamedAndEqualityComparesTheValue | split | 3 | the name joins two promises: the default names nothing (DefaultIsUnnamed), and equality compares the value (EqualityComparesTheValue). Both can break apart | tests/foundation/core/test_placement_class.cpp; modules/foundation/core/include/SushiRuntime/core/placement_class.hpp
```

### `tests/foundation/core/test_ref_counted.cpp`

```
Medium_RefCountedTest.MakeSushiOwnsAndReleasesOnce | resize | 8 | no device, file, thread or clock; it is refcount logic. Resize Medium to Small | tests/foundation/core/test_ref_counted.cpp; modules/foundation/core/include/SushiRuntime/core/ref_counted.hpp; modules/foundation/core/include/SushiRuntime/core/sushi_ptr.hpp
Medium_RefCountedTest.CopyBumpsMoveTransfers | resize | 8 | refcount logic on one thread, no device, file or clock. Resize Medium to Small | tests/foundation/core/test_ref_counted.cpp; modules/foundation/core/include/SushiRuntime/core/ref_counted.hpp; modules/foundation/core/include/SushiRuntime/core/sushi_ptr.hpp
Medium_RefCountedTest.LeakDetachesWithoutChangingCount | delete | 4 | same promise as sushi_ptr's leak in tests/foundation/core/test_sushi_ptr.cpp::Small_SushiPtrTest.LeakRoundTripHandsReferenceBack. leak() is a sushi_ptr member (sushi_ptr.hpp line 220), so the sushi_ptr file's test stays | tests/foundation/core/test_ref_counted.cpp; tests/foundation/core/test_sushi_ptr.cpp; modules/foundation/core/include/SushiRuntime/core/sushi_ptr.hpp
Medium_RefCountedTest.DerivedConvertsToBaseAndDeletesOnce | resize | 8 | single-thread refcount and virtual delete; no device, file or clock. Resize Medium to Small | tests/foundation/core/test_ref_counted.cpp; modules/foundation/core/include/SushiRuntime/core/ref_counted.hpp; modules/foundation/core/include/SushiRuntime/core/sushi_ptr.hpp
Medium_RefCountedTest.ConcurrentRefCountingIsAtomic | rewrite | 7 | the thread boundary is right for medium. The body holds an if with std::abort() and two loops; the straight-line rule forbids them. Move the stress loop into a named helper and turn the if into an assertion | tests/foundation/core/test_ref_counted.cpp; modules/foundation/core/include/SushiRuntime/core/ref_counted.hpp
```

### `tests/foundation/core/test_result.cpp`

```
Small_Result.SuccessValueIsReadThroughEveryValueAccessor | keep | 9 | one promise over the value accessors of a success | tests/foundation/core/test_result.cpp; modules/foundation/core/include/SushiRuntime/core/result.hpp
Small_Result.AnExpiringResultGivesItsValueAway | keep | 9 | value() and operator* on a temporary move the value out | tests/foundation/core/test_result.cpp; modules/foundation/core/include/SushiRuntime/core/result.hpp
Small_Result.ErrorOnErrorReturnsTheStoredError | keep | 9 | error() returns the stored error by reference | tests/foundation/core/test_result.cpp; modules/foundation/core/include/SushiRuntime/core/result.hpp
Small_Result.TheErrorTypeDefaultsToError | delete | 1 | forbidden kind: static_assert run as a test, with no runtime assertion. Other tests use Result<void> as Error: test_error_translation.cpp reads result.error().code, and test_result.cpp reads error().code. A type change would fail to compile there | tests/foundation/core/test_result.cpp; tests/foundation/core/test_error_translation.cpp; modules/foundation/core/include/SushiRuntime/core/result.hpp
Small_Result.VoidSuccessHoldsNoErrorAndValueReturns | split | 3 | the name joins two results that can break apart: has_value() is true (VoidSuccessHoldsNoError), and value() returns without stopping (VoidValueReturnsOnSuccess) | tests/foundation/core/test_result.cpp; modules/foundation/core/include/SushiRuntime/core/result.hpp
Small_Result.VoidErrorReturnsTheStoredError | keep | 9 | error() on a failed Result<void> returns code and message | tests/foundation/core/test_result.cpp; modules/foundation/core/include/SushiRuntime/core/result.hpp
Medium_ResultDeathTest.EveryValueAccessorOnAnErrorStopsWithTheStoredMessage | resize | 8 | stops through the process-wide SR_VERIFY handler. Seam: the message from Detail::describe_stored_error, checked small (the seam must not be read as a Detail reach in the test). The stop stays one death test | tests/foundation/core/test_result.cpp; modules/foundation/core/include/SushiRuntime/core/result.hpp; modules/foundation/core/include/SushiRuntime/core/assert.hpp
Medium_ResultDeathTest.EveryValueAccessorOnAnErrorOfAnotherTypeStops | resize | 8 | the same stop and seam as the previous case | tests/foundation/core/test_result.cpp; modules/foundation/core/include/SushiRuntime/core/result.hpp; modules/foundation/core/include/SushiRuntime/core/assert.hpp
Medium_ResultDeathTest.AValueAccessorOnAnExpiringErrorStops | resize | 8 | the same stop and seam as above | tests/foundation/core/test_result.cpp; modules/foundation/core/include/SushiRuntime/core/result.hpp; modules/foundation/core/include/SushiRuntime/core/assert.hpp
Medium_ResultDeathTest.ErrorOnAValueStops | resize | 8 | the same stop and seam as above | tests/foundation/core/test_result.cpp; modules/foundation/core/include/SushiRuntime/core/result.hpp; modules/foundation/core/include/SushiRuntime/core/assert.hpp
Medium_ResultDeathTest.VoidValueOnAnErrorStopsWithTheStoredMessage | resize | 8 | the same stop and seam as above, for Result<void> and Result<void, int> | tests/foundation/core/test_result.cpp; modules/foundation/core/include/SushiRuntime/core/result.hpp; modules/foundation/core/include/SushiRuntime/core/assert.hpp
Medium_ResultDeathTest.VoidErrorOnSuccessStops | resize | 8 | the same stop and seam as above | tests/foundation/core/test_result.cpp; modules/foundation/core/include/SushiRuntime/core/result.hpp; modules/foundation/core/include/SushiRuntime/core/assert.hpp
```

### `tests/foundation/core/test_span.cpp`

```
Small_Span.DefaultIsEmpty | keep | 9 | a default span is an empty view | tests/foundation/core/test_span.cpp; modules/foundation/core/include/SushiRuntime/core/span.hpp
Small_Span.ConstructsFromPointerAndCount | keep | 9 | pointer and count view, with data, size, size_bytes and element access | tests/foundation/core/test_span.cpp; modules/foundation/core/include/SushiRuntime/core/span.hpp
Small_Span.ConstructsFromCArray | merge | 4 | same promise as ConstructsFromLvalueVector (a span over a contiguous container has its data and size), with another container. Merge into ConstructsFromLvalueVector as a row | tests/foundation/core/test_span.cpp; modules/foundation/core/include/SushiRuntime/core/span.hpp
Small_Span.ConstructsFromLvalueVector | keep | 9 | the span aliases the vector: data pointer, size and write-through | tests/foundation/core/test_span.cpp; modules/foundation/core/include/SushiRuntime/core/span.hpp
Small_Span.ConstructsFromStdArray | merge | 4 | same promise as ConstructsFromLvalueVector, with std::array. Merge into ConstructsFromLvalueVector as a row | tests/foundation/core/test_span.cpp; modules/foundation/core/include/SushiRuntime/core/span.hpp
Small_Span.ConstViewOverConstContainer | keep | 9 | a const span over a const vector: size and data pointer | tests/foundation/core/test_span.cpp; modules/foundation/core/include/SushiRuntime/core/span.hpp
Small_Span.ConstViewOverNonConstLvalue | merge | 4 | same promise as ConstViewOverConstContainer (a const span binds to a container and reads its elements). Its check is a sum, weaker than the pointer check. Merge into ConstViewOverConstContainer | tests/foundation/core/test_span.cpp; modules/foundation/core/include/SushiRuntime/core/span.hpp
Small_Span.AcceptsTemporaryContainerForConstElements | keep | 9 | a const span binds to a temporary container | tests/foundation/core/test_span.cpp; modules/foundation/core/include/SushiRuntime/core/span.hpp
Small_Span.FirstLastAndSubspanSelectElements | split | 3 | one test with three actions (first, last, subspan). Split into FirstSelectsTheLeadingElements, LastSelectsTheTrailingElements and SubspanSelectsAWindow | tests/foundation/core/test_span.cpp; modules/foundation/core/include/SushiRuntime/core/span.hpp
Small_Span.ConvertsToAndFromStdSpan | keep | 9 | a std::span converts both ways, keeping data and size (C++20 only) | tests/foundation/core/test_span.cpp; modules/foundation/core/include/SushiRuntime/core/span.hpp
```

### `tests/foundation/core/test_strong_id.cpp`

```
Small_StrongId.DefaultIsZero | delete | 1 | forbidden kind: a default value. The test states only that the default is zero, which the default constructor's initialiser (strong_id.hpp line 47) does. No other promise is named | tests/foundation/core/test_strong_id.cpp; modules/foundation/core/include/SushiRuntime/core/strong_id.hpp
Small_StrongId.ExplicitConstructionKeepsValue | keep | 9 | explicit construction keeps the value | tests/foundation/core/test_strong_id.cpp; modules/foundation/core/include/SushiRuntime/core/strong_id.hpp
Small_StrongId.EqualityComparesValues | keep | 9 | equality compares values | tests/foundation/core/test_strong_id.cpp; modules/foundation/core/include/SushiRuntime/core/strong_id.hpp
Small_StrongId.OrderingFollowsValues | keep | 9 | operator< follows the value | tests/foundation/core/test_strong_id.cpp; modules/foundation/core/include/SushiRuntime/core/strong_id.hpp
Small_StrongId.UsableAsUnorderedMapKey | keep | 9 | the id hashes and compares as an unordered_map key | tests/foundation/core/test_strong_id.cpp; modules/foundation/core/include/SushiRuntime/core/strong_id.hpp
Small_StrongId.UsableAsOrderedMapKey | merge | 4 | same promise as OrderingFollowsValues (operator< orders the ids), through a std::map. Merge into OrderingFollowsValues as a row | tests/foundation/core/test_strong_id.cpp; modules/foundation/core/include/SushiRuntime/core/strong_id.hpp
Small_StrongId.HashForwardsToRepresentation | keep | 9 | std::hash forwards to the representation | tests/foundation/core/test_strong_id.cpp; modules/foundation/core/include/SushiRuntime/core/strong_id.hpp
```

### `tests/foundation/core/test_sushi_ptr.cpp`

```
Small_SushiPtrTest.RawPointerComparisonKeepsCount | keep | 9 | comparison with a raw pointer, both orders and const forms, does not copy | tests/foundation/core/test_sushi_ptr.cpp; modules/foundation/core/include/SushiRuntime/core/sushi_ptr.hpp
Small_SushiPtrTest.BaseTypeComparisonKeepsCount | keep | 9 | cross-type comparison compares managed objects and does not copy | tests/foundation/core/test_sushi_ptr.cpp; modules/foundation/core/include/SushiRuntime/core/sushi_ptr.hpp
Small_SushiPtrTest.NullComparisonsAreUnambiguous | keep | 9 | nullptr overloads are chosen and stay unambiguous | tests/foundation/core/test_sushi_ptr.cpp; modules/foundation/core/include/SushiRuntime/core/sushi_ptr.hpp
Small_SushiPtrTest.SelfAssignmentKeepsObject | keep | 9 | self-copy and self-move keep the object and its count | tests/foundation/core/test_sushi_ptr.cpp; modules/foundation/core/include/SushiRuntime/core/sushi_ptr.hpp
Small_SushiPtrTest.ResetReleasesOldAndTakesNew | split | 3 | the name joins two results that break apart: the old object is released (ResetReleasesTheOldObject), and the new one is taken (ResetTakesTheNewObject). A third action, reset to null, belongs to one of them | tests/foundation/core/test_sushi_ptr.cpp; modules/foundation/core/include/SushiRuntime/core/sushi_ptr.hpp
Small_SushiPtrTest.LeakRoundTripHandsReferenceBack | keep | 9 | leak() hands the reference over and adopting it back restores the count | tests/foundation/core/test_sushi_ptr.cpp; modules/foundation/core/include/SushiRuntime/core/sushi_ptr.hpp
```

### `tests/foundation/core/test_version.cpp`

```
Small_Version.TheTestProgramMatchesTheLibraryItLoaded | resize | 8 | reads the loaded library through runtime_version() and calls require_matching_version(), which stops on mismatch (version.hpp 131-137). Seam: version_matches over two given versions is small; the loaded read stays medium | tests/foundation/core/test_version.cpp; modules/foundation/core/include/SushiRuntime/core/version.hpp
Small_Version.TheHeaderMacrosAndTheLibraryReportOneVersion | resize | 8 | reads the loaded library (runtime_version and runtime_version_string). The pure part is a comparison of two versions, small through the same seam. The loaded read stays medium | tests/foundation/core/test_version.cpp; modules/foundation/core/include/SushiRuntime/core/version.hpp
Small_Version.APatchDifferenceIsAMatch | rewrite | 1 | forbidden kinds: no assertion (the test passes when require_same_release does not stop) and a Detail:: symbol (line 59). Only test of the promise. Rewrite as a predicate that returns a bool, asserted true | tests/foundation/core/test_version.cpp; modules/foundation/core/include/SushiRuntime/core/version.hpp
Medium_VersionDeathTest.AnOlderLibraryStopsWithBothVersions | rewrite | 1 | a Detail:: symbol (line 65), require_same_release is in namespace Detail (version.hpp). Only test of the older-library message. Rewrite through the message text, not the stop | tests/foundation/core/test_version.cpp; modules/foundation/core/include/SushiRuntime/core/version.hpp
Medium_VersionDeathTest.ANewerLibraryStopsAndIsCalledNewer | rewrite | 1 | a Detail:: symbol (lines 73, 76). Only test of the newer-library message. Rewrite as for AnOlderLibraryStopsWithBothVersions | tests/foundation/core/test_version.cpp; modules/foundation/core/include/SushiRuntime/core/version.hpp
```

### `tests/foundation/device_probe/test_device_probe.cpp`

```
Medium_DeviceProbe.AgreesWithTheDeviceList | rewrite | 6 | the oracle is the code under test: has_any_sycl_device() is !sycl::device::get_devices().empty() (device_probe.hpp), and the test writes the same expression. Rewrite with an oracle from a different path, such as counting devices per platform. A machine with no device should report skipped, not pass | tests/foundation/device_probe/test_device_probe.cpp; modules/foundation/device_probe/include/SushiRuntime/device_probe/device_probe.hpp
```

### `tests/foundation/log/test_log_gate.cpp`

```
Small_LogGate.TheFloorFollowsTheBuild | keep | 9 | the floor is a constant per build; the expected values follow the documented build policy (NDEBUG, SR_ENABLE_LOGGING). Compile-time, no process-wide state | tests/foundation/log/test_log_gate.cpp; modules/foundation/log/include/SushiRuntime/log/log_gate.hpp
Small_LogGate.ALevelPassesAtOrAboveItsCategoryLevel | rewrite | 1 | a Detail:: symbol: Detail::is_log_enabled (lines 67-73). It reads and writes the process-wide level (set_log_level). Only test of the gate. Rewrite with a threshold passed in (seam), and the process-wide level kept out of the test | tests/foundation/log/test_log_gate.cpp; modules/foundation/log/include/SushiRuntime/log/log_gate.hpp
```

### `tests/foundation/log/test_logger.cpp`

```
Medium_LoggerTest.DisabledLevelDoesNotEvaluateItsArguments | resize | 8 | the level decision is on values, and the gate reads the process-wide level slot. Seam: a gate that takes the threshold. The macro expansion stays in one medium test | tests/foundation/log/test_logger.cpp; modules/foundation/log/include/SushiRuntime/log/logger.hpp; modules/foundation/log/include/SushiRuntime/log/log_gate.hpp
Medium_LoggerTest.SetLogLevelTakesEffect | resize | 8 | the level decision is on values, held in a process-wide slot. Same seam as DisabledLevelDoesNotEvaluateItsArguments; the sink is also process-wide | tests/foundation/log/test_logger.cpp; modules/foundation/log/include/SushiRuntime/log/logger.hpp; modules/foundation/log/include/SushiRuntime/log/log_gate.hpp
Medium_LoggerTest.SinkReceivesEveryFieldAndTheLine | resize | 8 | the field mapping and the line are decisions on values, and the test reads the real clock (time_ns) and the process-wide sink. Seam: a record-to-fields function and a clock passed in. Line format is SushiLog's; this file checks the SushiRuntime side | tests/foundation/log/test_logger.cpp; modules/foundation/log/include/SushiRuntime/log/logger.hpp
Medium_LoggerTest.FatalIsDeliveredAfterTheQueueOnTheCallingThread | keep | 9 | the queue and the caller's thread are a thread boundary, so medium is the right size. It changes process-wide async mode and restores it in TearDown | tests/foundation/log/test_logger.cpp; modules/foundation/log/include/SushiRuntime/log/logger.hpp
```

### `tests/foundation/log/test_sushiruntime_log_project.cpp`

```
Small_SushiRuntimeLogProject.TheProjectIsTheRuntimeReadFromItsVariable | delete | 1 | forbidden kind: static_asserts run as a test, then SUCCEED() with no runtime assertion. The body states constants only; they belong in sushiruntime_log_project.hpp | tests/foundation/log/test_sushiruntime_log_project.cpp; modules/foundation/log/include/SushiRuntime/log/sushiruntime_log_project.hpp
```

### `tests/foundation/machine/test_device_description.cpp`

```
Small_DeviceDescription.DefaultsDescribeNoDevice | delete | 1 | default value of a plain record, no sibling test relies on the defaults | tests/foundation/machine/test_device_description.cpp;modules/foundation/machine/include/SushiRuntime/machine/device_description.hpp;tests/platform/device/test_device_queue_set.cpp
```

### `tests/foundation/machine/test_device_facts.cpp`

```
Small_DeviceFacts.DefaultsDescribeNoDevice | delete | 1 | default value of a plain record, callers build DeviceFacts with every field | tests/foundation/machine/test_device_facts.cpp;modules/foundation/machine/include/SushiRuntime/machine/device_facts.hpp;modules/surface/api/source/runtime.cpp;tests/extension/distributed/test_distributed.cpp
```

### `tests/foundation/machine/test_device_filter.cpp`

```
Small_DeviceFilter.DefaultsAcceptEveryClass | rewrite | 1 | default value the default runtime relies on through create(config = {}), no test states it, rewrite against the devices a default runtime admits | tests/foundation/machine/test_device_filter.cpp;modules/foundation/machine/include/SushiRuntime/machine/device_filter.hpp;modules/engine/execution/include/SushiRuntime/execution/runtime_context.hpp;modules/surface/api/include/SushiRuntime/api/runtime/runtime_config.hpp;tests/surface/api/runtime/test_runtime_config.cpp
```

### `tests/foundation/machine/test_device_kind.cpp`

```
Small_DeviceKind.FourDistinctClassesInOneByte | rewrite | 1 | static_assert run as a test and a constant check, move the size assert into device_kind.hpp, wire round trips cover three of the four values | tests/foundation/machine/test_device_kind.cpp;modules/foundation/machine/include/SushiRuntime/machine/device_kind.hpp;tests/extension/wire/test_wire_messages.cpp
```

### `tests/foundation/machine/test_node_facts.cpp`

```
Small_NodeFacts.DefaultsDescribeNoMachine | delete | 1 | default value of a plain record, no sibling test relies on it | tests/foundation/machine/test_node_facts.cpp;modules/foundation/machine/include/SushiRuntime/machine/node_facts.hpp
Small_NodeFacts.ACopyKeepsTheLabelsInOrderAndTheDevices | keep | 9 | a copy keeps the labels in order and every device, value semantics of a public record | tests/foundation/machine/test_node_facts.cpp;modules/foundation/machine/include/SushiRuntime/machine/node_facts.hpp
```

### `tests/foundation/machine/test_node_label.cpp`

```
Small_NodeLabel.DefaultsHoldAnEmptyKeyAndValue | delete | 1 | default value, an empty key is refused by PlacementRules::add, so the default is never a valid label | tests/foundation/machine/test_node_label.cpp;modules/foundation/machine/include/SushiRuntime/machine/node_label.hpp;tests/foundation/placement/test_placement_rules.cpp
Small_NodeLabel.ACopyKeepsTheKeyAndTheValue | keep | 9 | a copy keeps the key and the value, value semantics of a public record | tests/foundation/machine/test_node_label.cpp;modules/foundation/machine/include/SushiRuntime/machine/node_label.hpp
```

### `tests/foundation/machine/test_operating_system.cpp`

```
Small_OperatingSystem.ThreeDistinctSystemsInOneByte | rewrite | 1 | static_assert run as a test and a constant check, move the size assert into operating_system.hpp, wire round trips cover Linux and Windows | tests/foundation/machine/test_operating_system.cpp;modules/foundation/machine/include/SushiRuntime/machine/operating_system.hpp;tests/extension/wire/test_wire_messages.cpp
```

### `tests/foundation/machine/test_pin_policy.cpp`

```
Small_PinPolicy.TwoPlacementsInOneByte | rewrite | 1 | static_assert run as a test and a constant check, move the size assert into pin_policy.hpp, the engine settings tests use both values | tests/foundation/machine/test_pin_policy.cpp;modules/foundation/machine/include/SushiRuntime/machine/pin_policy.hpp;tests/engine/execution/test_runtime_settings.cpp
```

### `tests/foundation/machine/test_worker_pool.cpp`

```
Medium_WorkerPool.DefaultsToAnEmptyPinnedPool | delete | 1 | default values, the pinned default is already stated by test_worker_pool_settings.cpp and test_runtime_settings.cpp, and a Medium_ size fits no device or thread | tests/foundation/machine/test_worker_pool.cpp;modules/foundation/machine/include/SushiRuntime/machine/worker_pool.hpp;tests/engine/scheduler/test_worker_pool_settings.cpp;tests/engine/execution/test_runtime_settings.cpp
```

### `tests/foundation/measurement/test_completion_signal.cpp`

```
Medium_CompletionSignal.AWaitWithNoRaiseTimesOut | resize | 8 | the outcome is decided on the raised flag, but wait_for reads the real clock, resize to small with a waiter that takes a clock the test advances | tests/foundation/measurement/test_completion_signal.cpp;modules/foundation/measurement/include/SushiRuntime/measurement/completion_signal.hpp
Medium_CompletionSignal.ARaiseWakesTheWaiterAndResetClearsIt | split | 3 | "And" joins a thread handoff, which stays medium, and a reset, a decision on values that moves to small with the clock seam | tests/foundation/measurement/test_completion_signal.cpp;modules/foundation/measurement/include/SushiRuntime/measurement/completion_signal.hpp
```

### `tests/foundation/measurement/test_measurement.cpp`

```
Small_Measurement.SummaryTakesTheMiddleAndTheExtremes | split | 3 | "And" joins the median, odd and even counts as rows, and the extremes, two results that break apart, EXPECT_NEAR at zero K_TOLERANCE should be EXPECT_EQ for exact figures | tests/foundation/measurement/test_measurement.cpp;modules/foundation/measurement/include/SushiRuntime/measurement/measurement.hpp
Small_Measurement.NoSampleGivesAnEmptySummary | keep | 9 | no sample gives an all-zero summary, checks run_count and median only, minimum and maximum are not checked | tests/foundation/measurement/test_measurement.cpp;modules/foundation/measurement/include/SushiRuntime/measurement/measurement.hpp
Small_Measurement.MeasureCallsTheRunEachTime | split | 3 | (a) the call count stays as its own test, (b) the median of the returns is deleted at question 4, SummaryTakesTheMiddleAndTheExtremes covers it, (c) the default run count is deleted at question 6, the expected value is K_DEFAULT_RUN_COUNT, the constant the default argument uses | tests/foundation/measurement/test_measurement.cpp;modules/foundation/measurement/include/SushiRuntime/measurement/measurement.hpp
Small_Measurement.TheLineCarriesEveryKey | rewrite | 6 | the build= value is expected from build_type(), the function format_measurement calls, so the oracle is the code under test, and the key loop in the body is a loop outside a row table | tests/foundation/measurement/test_measurement.cpp;modules/foundation/measurement/include/SushiRuntime/measurement/measurement.hpp
```

### `tests/foundation/placement/test_placement_rule.cpp`

```
Small_AbsentPolicy.DefaultsRunLocallyWithNoBound | rewrite | 1 | default value, the local default is what a policy with no stated action gets, no test states it, rewrite against that behaviour | tests/foundation/placement/test_placement_rule.cpp;modules/foundation/placement/include/SushiRuntime/placement/placement_rule.hpp
Small_PlacementMatch.DefaultsStateNothing | delete | 1 | default value, "an empty part matches any node" is stated by the resolve rows of test_placement_rules.cpp | tests/foundation/placement/test_placement_rule.cpp;modules/foundation/placement/include/SushiRuntime/placement/placement_rule.hpp;tests/foundation/placement/test_placement_rules.cpp
Small_PlacementTarget.DefaultsNameNothing | delete | 1 | default value, the promises are tested in test_placement_target_matching.cpp: a target with no machine part matches every machine, and a target with no device part picks device 0 | tests/foundation/placement/test_placement_rule.cpp;modules/foundation/placement/include/SushiRuntime/placement/placement_rule.hpp;tests/foundation/placement/test_placement_target_matching.cpp
Small_PlacementRule.DefaultsHoldNoNamePriorityZeroAndALocalPolicy | delete | 1 | default values, the local policy default is carried by the rewrite of Small_AbsentPolicy.DefaultsRunLocallyWithNoBound | tests/foundation/placement/test_placement_rule.cpp;modules/foundation/placement/include/SushiRuntime/placement/placement_rule.hpp
```

### `tests/foundation/placement/test_placement_rules.cpp`

```
Small_PlacementRules.AnEmptySetHasNoRuleAndResolvesNothing | keep | 9 | an empty set resolves nothing | tests/foundation/placement/test_placement_rules.cpp;modules/foundation/placement/include/SushiRuntime/placement/placement_rules.hpp
Small_PlacementRules.RuleReturnsTheRulesInInsertionOrder | keep | 9 | rule() returns the rules in insertion order, each kept as added | tests/foundation/placement/test_placement_rules.cpp;modules/foundation/placement/include/SushiRuntime/placement/placement_rules.hpp
Small_PlacementRules.AnExactClassOfAnyDepthIsAccepted | keep | 9 | an exact class deeper than the prefix depth is accepted and matched | tests/foundation/placement/test_placement_rules.cpp;modules/foundation/placement/include/SushiRuntime/placement/placement_rules.hpp;modules/foundation/core/include/SushiRuntime/core/placement_class.hpp
Small_PlacementRules.ResolveTableMatchesClassPrefixKeyOperationAndPriority | rewrite | 7 | the name lists features and states no single promise, and the loop that adds ten rules builds input, rows or a builder replace it | tests/foundation/placement/test_placement_rules.cpp;modules/foundation/placement/include/SushiRuntime/placement/placement_rules.hpp;modules/foundation/core/include/SushiRuntime/core/placement_class.hpp;modules/foundation/placement/include/SushiRuntime/placement/placement_rule.hpp
Small_PlacementRules.AddRefusesEveryCaseOfTheRefusedWhenColumn | keep | 9 | add refuses each design-table row with InvalidArgument, names the rule and keeps nothing, the rows share one set, so a row that wrongly adds would couple the others | tests/foundation/placement/test_placement_rules.cpp;modules/foundation/placement/include/SushiRuntime/placement/placement_rules.hpp
Small_PlacementRules.ARefusalNamesAnUnnamedRuleByItsPosition | keep | 9 | a refusal names an unnamed rule by its position and keeps the set unchanged, one all-or-nothing outcome | tests/foundation/placement/test_placement_rules.cpp;modules/foundation/placement/include/SushiRuntime/placement/placement_rules.hpp
Small_PlacementRules.APositiveWaitBoundAndDeadlineAreAccepted | split | 3 | "And" joins a positive Wait bound and a positive deadline, two acceptance checks that break apart | tests/foundation/placement/test_placement_rules.cpp;modules/foundation/placement/include/SushiRuntime/placement/placement_rules.hpp;modules/foundation/placement/include/SushiRuntime/placement/placement_rule.hpp
Small_PlacementRules.Regression_AWaitBoundOrDeadlineAboveTheMaximumIsRefused | split | 3 | three promises: a Wait bound above K_MAX_DURATION is refused, a deadline above it is refused, the maximum is accepted for both | tests/foundation/placement/test_placement_rules.cpp;modules/foundation/placement/include/SushiRuntime/placement/placement_rules.hpp;modules/foundation/placement/source/placement_rules.cpp
```

### `tests/foundation/placement/test_placement_target_matching.cpp`

```
Small_PlacementTargetMatching.MachinePartMatchesByNameOrLabel | rewrite | 7 | five calls on one changing fixture state, one predicate, rows | tests/foundation/placement/test_placement_target_matching.cpp;modules/foundation/placement/include/SushiRuntime/placement/placement_target_matching.hpp;modules/foundation/machine/include/SushiRuntime/machine/node_facts.hpp
Small_PlacementTargetMatching.DevicePartPicksByIndexKindOrMemory | split | 3 | three selectors, index, kind and least memory, each its own promise, each as rows | tests/foundation/placement/test_placement_target_matching.cpp;modules/foundation/placement/include/SushiRuntime/placement/placement_target_matching.hpp;modules/foundation/machine/include/SushiRuntime/machine/device_kind.hpp
Small_PlacementTargetMatching.DescriptionsNameTheRuleAndEveryTargetPart | split | 3 | "And" joins describe_rule and describe_target, two functions with two promises | tests/foundation/placement/test_placement_target_matching.cpp;modules/foundation/placement/include/SushiRuntime/placement/placement_target_matching.hpp
Small_PlacementTargetMatching.Regression_ADeviceOnlyTargetNamesOnlyAMachineWithTheDevice | split | 3 | device_on names a machine only when it has the device, and only when the machine part also matches, two conditions that break apart | tests/foundation/placement/test_placement_target_matching.cpp;modules/foundation/placement/include/SushiRuntime/placement/placement_target_matching.hpp
Small_PlacementTargetMatching.Regression_MachineNamesCompareByCaseAndFirstLabel | split | 3 | machine_names_match compares the canonical form, as rows, and machine_matches applies the same comparison to a target and a machine name | tests/foundation/placement/test_placement_target_matching.cpp;modules/foundation/placement/include/SushiRuntime/placement/placement_target_matching.hpp
```

### `tests/foundation/profile/test_profile.cpp`

```
Small_Profile.EachMacroWritesItsKind | rewrite | 7 | four macro calls in one body, rows, and the fixture opens the process-wide Collector::instance, which the seam of question 8 must replace with a sink passed in | tests/foundation/profile/test_profile.cpp;modules/foundation/profile/include/SushiRuntime/profile/profile.hpp
Small_Profile.TheThreadIndexIsTheCollectorsOnceTheThreadIsRegistered | rewrite | 6 | the expected value is Collector::thread_index(), the call current_thread_index makes, so the oracle is the code under test, rewrite against the index a drained batch carries | tests/foundation/profile/test_profile.cpp;modules/foundation/profile/include/SushiRuntime/profile/profile.hpp
Small_Profile.AThreadThatNeverRecordedReadsZero | resize | 8 | the decision is on values, no index reads 0, the thread and the process-wide registry are the reach, resize to small with a thread-index source passed in as the seam | tests/foundation/profile/test_profile.cpp;modules/foundation/profile/include/SushiRuntime/profile/profile.hpp
Small_Profile.NamingAThreadPutsTheNameInTheDrain | rewrite | 1 | control flow in the body, the for and if over the drain, outside a row table, and the test is a thread-to-collector boundary, so the rewrite is Medium_ | tests/foundation/profile/test_profile.cpp;modules/foundation/profile/include/SushiRuntime/profile/profile.hpp
Small_ProfileLockWait.TheLockIsReleasedAtTheEndOfItsScope | keep | 9 | SR_LOCK_WAIT holds the lock for the scope and releases it at the end, the SushiProfiler headers are not in this checkout, so whether the uncontended path reaches the process-wide collector is not read here | tests/foundation/profile/test_profile.cpp;modules/foundation/profile/include/SushiRuntime/profile/profile.hpp
```

### `tests/foundation/profile/test_profile_gate.cpp`

```
Small_ProfileGate.AZoneAtTheCompiledLevelWritesOneEvent | resize | 8 | the front writes into the process-wide Collector::instance that the fixture opens, resize with a sink passed in as the seam | tests/foundation/profile/test_profile_gate.cpp;modules/foundation/profile/include/SushiRuntime/profile/profile.hpp;modules/foundation/profile/include/SushiRuntime/profile/profile_gate.hpp;tests/foundation/profile/gate_units.hpp;tests/foundation/profile/gate_unit_level_one.cpp
Small_ProfileGate.AZoneAboveTheCompiledLevelWritesNothing | split | 3 | (a) zones above the compiled level write nothing and register no site, (b) the level-2 unit's fine zone writes one event as a control, two promises | tests/foundation/profile/test_profile_gate.cpp;modules/foundation/profile/include/SushiRuntime/profile/profile_gate.hpp;tests/foundation/profile/gate_units.hpp;tests/foundation/profile/gate_unit_level_one.cpp;tests/foundation/profile/gate_unit_level_two.cpp
Small_ProfileGate.AUnitCompiledAtLevelZeroWritesNothing | resize | 8 | the compiled level decides, but the writes reach the process-wide collector, resize with a sink seam | tests/foundation/profile/test_profile_gate.cpp;modules/foundation/profile/include/SushiRuntime/profile/profile_gate.hpp;tests/foundation/profile/gate_unit_level_zero.cpp
Small_ProfileGate.AUnitCompiledAtLevelThreeWritesEveryZone | resize | 8 | same reach of the process-wide collector, the promise itself is one, resize with a sink seam | tests/foundation/profile/test_profile_gate.cpp;modules/foundation/profile/include/SushiRuntime/profile/profile_gate.hpp;tests/foundation/profile/gate_unit_level_three.cpp
Small_ProfileGate.AClosedCollectorWritesNothing | resize | 8 | same reach, the close call acts on the process-wide instance, resize with a sink seam | tests/foundation/profile/test_profile_gate.cpp;modules/foundation/profile/include/SushiRuntime/profile/profile_gate.hpp;tests/foundation/profile/gate_unit_level_three.cpp
Small_ProfileGateLevel.ThisUnitCompilesAtTheDefaultLevel | delete | 1 | constant check and a static_assert run as a test, the expected value restates the definition of K_COMPILED_LEVEL in profile_gate.hpp | tests/foundation/profile/test_profile_gate.cpp;modules/foundation/profile/include/SushiRuntime/profile/profile_gate.hpp
```

### `tests/foundation/profile/test_profile_keys.cpp`

```
Small_ProfileKeys.EveryKeyIsListedOnce | delete | 1 | constant check, K_ALL_KEYS has no reader outside its header and this test, so the uniqueness it guards guards nothing used | tests/foundation/profile/test_profile_keys.cpp;modules/foundation/profile/include/SushiRuntime/profile/profile_keys.hpp
Small_ProfileKeys.TheNamesAreSpelledAsTheProfileReaderExpects | rewrite | 1 | constant check, the layer, scheduler and chain spellings are checked reader side, and the host track name has no literal check in the tree, rewrite keeps host as a reader-side check | tests/foundation/profile/test_profile_keys.cpp;modules/foundation/profile/include/SushiRuntime/profile/profile_keys.hpp;tests/surface/profiling/test_device_source.cpp;tests/surface/profiling/test_register_profiler_parts/test_runtime_session.cpp;modules/surface/profiling/source/device_track_name.cpp
```

### `tests/foundation/region/test_buffer_region.cpp`

```
Small_BufferRegion.ADefaultRegionOverlapsNothing | rewrite | 7 | five overlaps calls over four input pairs, one promise, rows | tests/foundation/region/test_buffer_region.cpp;modules/foundation/region/include/SushiRuntime/region/buffer_region.hpp
Small_BufferRegion.ADefaultRegionContainsNothingAndIsContainedByNothing | split | 3 | "And" joins contains and is contained by, two results that break apart | tests/foundation/region/test_buffer_region.cpp;modules/foundation/region/include/SushiRuntime/region/buffer_region.hpp
Small_BufferRegion.RegionsOfOneAllocationOverlapWhenTheirIntervalsIntersect | split | 3 | (a) intersecting intervals overlap, as rows, (b) half-open intervals that touch share no byte, a boundary promise of its own | tests/foundation/region/test_buffer_region.cpp;modules/foundation/region/include/SushiRuntime/region/buffer_region.hpp
Small_BufferRegion.RegionsOfDifferentAllocationsNeverOverlap | keep | 9 | regions of different bases neither overlap nor contain, one pair and one guard on the base | tests/foundation/region/test_buffer_region.cpp;modules/foundation/region/include/SushiRuntime/region/buffer_region.hpp
Small_BufferRegion.ARegionContainsWhatLiesEntirelyInside | rewrite | 7 | six contains calls over several pairs, positive and negative, rows | tests/foundation/region/test_buffer_region.cpp;modules/foundation/region/include/SushiRuntime/region/buffer_region.hpp
```

### `tests/foundation/region/test_liveness.cpp`

```
Small_Liveness.AnObserverExpiresWithItsToken | keep | 9 | an observer locks while the token lives and expires with it | tests/foundation/region/test_liveness.cpp;modules/foundation/region/include/SushiRuntime/region/liveness.hpp
Small_Liveness.EachTokenIsItsOwn | keep | 9 | each make_liveness call is a distinct token | tests/foundation/region/test_liveness.cpp;modules/foundation/region/include/SushiRuntime/region/liveness.hpp
```

### `tests/foundation/report/test_clock_pairing.cpp`

```
Small_ClockPairing.ADefaultPairingIsTheIdentity | delete | 1 | forbidden kind: a test of a default value; the identity of a run is the header's statement, not a default struct | tests/foundation/report/test_clock_pairing.cpp; modules/foundation/report/include/SushiRuntime/report/clock_pairing.hpp
Small_ClockPairing.APairingKeepsItsThreeReadings | delete | 1 | forbidden kind: a getter; the struct holds three fields it was given | tests/foundation/report/test_clock_pairing.cpp; modules/foundation/report/include/SushiRuntime/report/clock_pairing.hpp
Small_ClockPairing.ADeviceClockKeepsItsDeviceAndPairing | delete | 1 | forbidden kind: a getter; the same body as tests/foundation/report/test_node_event_record.cpp, deleted with it | tests/foundation/report/test_clock_pairing.cpp; modules/foundation/report/include/SushiRuntime/report/clock_pairing.hpp
```

### `tests/foundation/report/test_dispatch_width.cpp`

```
Small_DispatchWidth.UnknownIsTheLargestWidth | delete | 1 | forbidden kind: a static_assert run as a test with SUCCEED(); a constant | tests/foundation/report/test_dispatch_width.cpp; modules/foundation/report/include/SushiRuntime/report/dispatch_width.hpp
```

### `tests/foundation/report/test_drop_counts.cpp`

```
Small_DropCounts.ADefaultCountLostNothing | rewrite | 1 | forbidden kind: a for loop in the test body over the causes; the default-count promise (any() false, total() 0) stays, written as straight-line checks | tests/foundation/report/test_drop_counts.cpp; modules/foundation/report/include/SushiRuntime/report/drop_counts.hpp
Small_DropCounts.CountsPerCauseAndSaysWhetherAnyWasLost | split | 3 | two promises: any() says whether a loss happened, and of() and total() read the per-cause counts; the per-cause reads repeat EveryCauseHasItsOwnSlot, so the split keeps the any() test and drops the repeat | tests/foundation/report/test_drop_counts.cpp; modules/foundation/report/include/SushiRuntime/report/drop_counts.hpp
Small_DropCounts.EveryCauseHasItsOwnSlot | rewrite | 1 | forbidden kind: a for loop in the test body that builds the per-cause counts; the only test of the slot promise, so the loop becomes explicit assignments | tests/foundation/report/test_drop_counts.cpp; modules/foundation/report/include/SushiRuntime/report/drop_counts.hpp
```

### `tests/foundation/report/test_node_event_record.cpp`

```
Small_NodeEventRecord.HasTheSizeAndTheOrderTheEngineMirrors | split | 3 | two promises: the size and order are held by the file-scope static_asserts, so the runtime sizeof literal is a copy of them and goes; the default-state promise (unknown widths, host device, zero thread) stays as its own test | tests/foundation/report/test_node_event_record.cpp; modules/foundation/report/include/SushiRuntime/report/node_event_record.hpp
Small_NodeEventRecord.ADeviceClockKeepsItsDeviceAndPairing | delete | 1 | forbidden kind: a getter; duplicate of tests/foundation/report/test_clock_pairing.cpp::Small_ClockPairing.ADeviceClockKeepsItsDeviceAndPairing, and both are getters | tests/foundation/report/test_node_event_record.cpp; modules/foundation/report/include/SushiRuntime/report/clock_pairing.hpp
```

### `tests/foundation/report/test_node_table.cpp`

```
Small_NodeTable.FindsARowByRegistrationIndex | keep | 9 | node_table: find() returns the row of an index whatever order the rows came in | tests/foundation/report/test_node_table.cpp; modules/foundation/report/include/SushiRuntime/report/node_table.hpp
Small_NodeTable.AnIndexPastTheTableHasNoRow | keep | 9 | node_table: find() returns null for an index no row holds | tests/foundation/report/test_node_table.cpp; modules/foundation/report/include/SushiRuntime/report/node_table.hpp
Small_NodeTable.KeepsAnAbsentCostApartFromAStatedZero | keep | 9 | node_table: a cost never stated stays absent, apart from a stated zero cost | tests/foundation/report/test_node_table.cpp; modules/foundation/report/include/SushiRuntime/report/node_table.hpp
Small_NodeTable.ARowKeepsItsOperationHostFlagAndMultiplicity | delete | 1 | forbidden kind: a getter round trip of stored fields; host_native and multiplicity are read by tests/engine/graph/test_compiled_graph.cpp and tests/surface/profiling/test_device_source.cpp | tests/foundation/report/test_node_table.cpp; modules/foundation/report/include/SushiRuntime/report/node_table.hpp
Small_NodeTable.CarriesTheLabelAndTheOriginOfItsRows | delete | 1 | forbidden kind: a getter round trip; label and origin are read by tests/surface/profiling/test_register_profiler_parts/test_graph_identity.cpp | tests/foundation/report/test_node_table.cpp; modules/foundation/report/include/SushiRuntime/report/node_table.hpp
Small_NodeTable.CarriesTheGraphId | delete | 1 | forbidden kind: a getter round trip; graph_id is read by tests/surface/profiling/test_register_profiler_parts/test_graph_identity.cpp | tests/foundation/report/test_node_table.cpp; modules/foundation/report/include/SushiRuntime/report/node_table.hpp
```

### `tests/foundation/report/test_run_observer.cpp`

```
Small_RunObservation.StartsEmpty | delete | 1 | forbidden kind: a test of default values; the drop default is in test_drop_counts.cpp, the scheduler default in run_report | tests/foundation/report/test_run_observer.cpp; modules/foundation/report/include/SushiRuntime/report/run_observer.hpp
Small_RunObservation.AnObserverReadsTheObservationItIsGiven | rewrite | 1 | forbidden kind: a spy written by the test (CountingObserver) for the repository's RunObserver; rewrite needs the in-memory RunObserver adapter in modules/foundation/report, named by the port's module | tests/foundation/report/test_run_observer.cpp; modules/foundation/report/include/SushiRuntime/report/run_observer.hpp
Small_RunObserverHandle.ADefaultHandleStandsForNothing | keep | 9 | run_observer: a default handle stands for no observer and reset() on it does nothing | tests/foundation/report/test_run_observer.cpp; modules/foundation/report/include/SushiRuntime/report/run_observer.hpp
Small_RunObserverHandle.ResetRemovesOnceAndOnlyOnce | keep | 9 | run_observer_handle: reset() removes its observer once, and a second reset removes nothing | tests/foundation/report/test_run_observer.cpp; modules/foundation/report/include/SushiRuntime/report/run_observer.hpp
Small_RunObserverHandle.DestructionRemovesTheObserver | keep | 9 | run_observer_handle: destruction removes its observer | tests/foundation/report/test_run_observer.cpp; modules/foundation/report/include/SushiRuntime/report/run_observer.hpp
Small_RunObserverHandle.AMovedHandleCarriesTheRemoval | split | 3 | two actions: move construction, then move assignment on a separate handle; each is its own promise | tests/foundation/report/test_run_observer.cpp; modules/foundation/report/include/SushiRuntime/report/run_observer.hpp
Small_RunObserverHandle.AssigningOverALiveHandleRemovesItsObserver | keep | 9 | run_observer_handle: move assignment removes the observer the target stood for | tests/foundation/report/test_run_observer.cpp; modules/foundation/report/include/SushiRuntime/report/run_observer.hpp
```

### `tests/foundation/report/test_run_report.cpp`

```
Small_NodeEventRecord.LayoutIsFortyEightBytesAndTriviallyCopyable | delete | 1 | forbidden kind: a static_assert run as a test; the layout is already held by the file-scope static_asserts of tests/foundation/report/test_node_event_record.cpp, a duplicate | tests/foundation/report/test_run_report.cpp; tests/foundation/report/test_node_event_record.cpp; modules/foundation/report/include/SushiRuntime/report/node_event_record.hpp
Small_NodeEventRecord.ResetKeepsWarningsCapacity | keep | 9 | run_report: reset() keeps the capacity of the report's vectors; the suite prefix names NodeEventRecord, a wrong unit, and should read Small_RunReport | tests/foundation/report/test_run_report.cpp; modules/foundation/report/include/SushiRuntime/report/run_report.hpp
Small_NodeEventRecord.ResetZeroesTheOutcomeAndTheCounts | keep | 9 | run_report: reset() returns outcome, layer and counts to their constructed values; the suite prefix should read Small_RunReport | tests/foundation/report/test_run_report.cpp; modules/foundation/report/include/SushiRuntime/report/run_report.hpp
Small_NodeEventRecord.WidthsDefaultToUnknown | delete | 4 | duplicate of the default-state promise in tests/foundation/report/test_node_event_record.cpp::Small_NodeEventRecord.HasTheSizeAndTheOrderTheEngineMirrors, which stays; the constant check is a forbidden kind as well | tests/foundation/report/test_run_report.cpp; tests/foundation/report/test_node_event_record.cpp; modules/foundation/report/include/SushiRuntime/report/node_event_record.hpp
RunReportNamesTests.ADefaultReportIsEmpty | delete | 1 | forbidden kind: a test of default values; the reset test covers the constructed values | tests/foundation/report/test_run_report.cpp; modules/foundation/report/include/SushiRuntime/report/run_report.hpp
```

### `tests/foundation/sycl/test_error_translation.cpp`

```
Small_SYCLErrorTranslation.ASYCLExceptionTakesTheDeviceCode | keep | 9 | sycl error_translation: a sycl::exception takes the code given for device failures, with its text | tests/foundation/sycl/test_error_translation.cpp; modules/foundation/sycl/include/SushiRuntime/sycl/error_translation.hpp
Small_SYCLErrorTranslation.AMemoryAllocationErrorIsAllocationFailed | keep | 9 | sycl error_translation: a memory allocation errc maps to AllocationFailed | tests/foundation/sycl/test_error_translation.cpp; modules/foundation/sycl/include/SushiRuntime/sycl/error_translation.hpp
Small_SYCLErrorTranslation.AStandardExceptionTakesTheOtherCode | split | 3 | two actions: a throwing call and a call that returns; each is its own promise | tests/foundation/sycl/test_error_translation.cpp; modules/foundation/sycl/include/SushiRuntime/sycl/error_translation.hpp
```

### `tests/foundation/sycl/test_sycl_compat.cpp`

```
Medium_SYCLCompat.TheTraitSeesHostTask | rewrite | 1 | forbidden kind: a static_assert in the test body, and the name claims a positive case (a SYCL handler has host_task) that the body does not check; rewrite to check both sides, small, with sycl headers only | tests/foundation/sycl/test_sycl_compat.cpp; modules/foundation/sycl/include/SushiRuntime/sycl/sycl_compat.hpp
Medium_SYCLCompat.AHostTaskRunsOnce | rewrite | 8 | the test body makes a sycl::queue; the boundary is medium but the queue must come from the module's base, which this module lacks; no seam for the queue; the test otherwise reaches what medium allows | tests/foundation/sycl/test_sycl_compat.cpp; modules/foundation/sycl/include/SushiRuntime/sycl/sycl_compat.hpp; tests/README.md
```

### `tests/foundation/test_runner/test_log_capture.cpp`

```
Small_LogCapture.RecordsOnlyWhatTheLevelPasses | resize | 8 | LogCapture installs a sink on the process-wide logger (SushiLog::set_log_sink, set_log_level), which a small test may not reach; the seam is the logger's sink passed in at the shell, which the module does not have; the level gate itself is SushiLog's | tests/foundation/test_runner/test_log_capture.cpp; modules/foundation/test_runner/include/SushiRuntime/test_runner/log_capture.hpp
Small_LogCapture.RestoresTheSinkAndTheLevelOnDestruction | resize | 8 | the same process-wide sink and level; restoring them is a process-wide state check, so the seam (a sink passed in) comes first | tests/foundation/test_runner/test_log_capture.cpp; modules/foundation/test_runner/include/SushiRuntime/test_runner/log_capture.hpp
```

### `tests/foundation/test_runner/test_recording_observer.cpp`

```
Small_RecordingObserver.StartsEmpty | rewrite | 1 | forbidden kind under the recorder ruling: a recorder for the repository's RunObserver, held outside the port's module; rewrite needs the in-memory RunObserver adapter in modules/foundation/report | tests/foundation/test_runner/test_recording_observer.cpp; modules/foundation/test_runner/include/SushiRuntime/test_runner/recording_observer.hpp; modules/foundation/report/include/SushiRuntime/report/run_observer.hpp
Small_RecordingObserver.KeepsEveryObservationInOrder | rewrite | 1 | forbidden kind under the recorder ruling, as above; the rewrite moves the recorder into the report module as its in-memory adapter | tests/foundation/test_runner/test_recording_observer.cpp; modules/foundation/test_runner/include/SushiRuntime/test_runner/recording_observer.hpp; modules/foundation/report/include/SushiRuntime/report/run_observer.hpp
Small_RecordingObserver.ClearForgetsEveryObservation | rewrite | 1 | forbidden kind under the recorder ruling, as above | tests/foundation/test_runner/test_recording_observer.cpp; modules/foundation/test_runner/include/SushiRuntime/test_runner/recording_observer.hpp; modules/foundation/report/include/SushiRuntime/report/run_observer.hpp
Small_RecordingObserver.KeepsObservationsFromSeveralThreads | rewrite | 1 | forbidden kind under the recorder ruling, and a small test that starts four threads, which the small size forbids; the rewrite moves to the in-memory adapter and to medium only if a real thread is needed | tests/foundation/test_runner/test_recording_observer.cpp; modules/foundation/test_runner/include/SushiRuntime/test_runner/recording_observer.hpp; tests/README.md
```

### `tests/foundation/thread_barrier/test_barrier.cpp`

```
Medium_Barrier.NoThreadPassesBeforeTheLastArrives | rewrite | 1 | forbidden kind: control flow in the test body (an if inside the thread body, and loops that build the threads and the rounds); the rewrite names a builder for the threads and checks the recorded arrivals in straight line | tests/foundation/thread_barrier/test_barrier.cpp; modules/foundation/thread_barrier/include/SushiRuntime/thread_barrier/barrier.hpp
Medium_Barrier.ABarrierOfOneNeverWaits | rewrite | 1 | forbidden kind: the only check is SUCCEED(); the promise is that a barrier of one returns at once, observable by returning under the size timeout; no thread is needed, so the rewrite is small | tests/foundation/thread_barrier/test_barrier.cpp; modules/foundation/thread_barrier/include/SushiRuntime/thread_barrier/barrier.hpp
```

### `tests/foundation/usm_guard/test_usm_shared.cpp`

```
Medium_USMShared.TheHostWritesAndReadsEveryElement | rewrite | 1 | forbidden kind: two for loops in the test body, one of them asserting per element; the body also makes a sycl::queue, which the module's base must hand in; rewrite with a builder or rows | tests/foundation/usm_guard/test_usm_shared.cpp; modules/foundation/usm_guard/include/SushiRuntime/usm_guard/usm_shared.hpp
Medium_USMShared.AMoveHandsTheAllocationOver | split | 3 | two actions: a move construction and a move assignment, each its own promise; the body also makes a sycl::queue, to be handed in by the base | tests/foundation/usm_guard/test_usm_shared.cpp; modules/foundation/usm_guard/include/SushiRuntime/usm_guard/usm_shared.hpp
```

### `tests/platform/device/test_device_descriptor.cpp`

```
Medium_DeviceDescriptor.KeepsTheDeviceAndItsDescription | delete | 1 | forbidden kind: a getter round trip of an aggregate; the device and inventory carry-through is checked by tests/platform/device/test_device_queue_set.cpp::Medium_DeviceQueueSet.CountAndInventoryFollowTheDescriptors and PlainQueueIsOneObjectOnItsDevice | tests/platform/device/test_device_descriptor.cpp; modules/platform/device/include/SushiRuntime/device/device_descriptor.hpp
```

### `tests/platform/device/test_device_log_category.cpp`

```
Small_DeviceLogCategory.TheCategoryIsNamedAfterTheModule | delete | 1 | forbidden kind: a log category, with static_assert and SUCCEED(); the name is a constant | tests/platform/device/test_device_log_category.cpp; modules/platform/device/include/SushiRuntime/device/device_log_category.hpp
```

### `tests/platform/device/test_device_queue_set.cpp`

```
Medium_DeviceQueueSet.CountAndInventoryFollowTheDescriptors | resize | 8 | the inventory copy and the position numbering decide on values, but the set takes a sycl::device in each DeviceDescriptor and builds real queues, so the decision is reached through a device; the seam is an inventory built from DeviceDescription values, which the module lacks; the queue-building part stays medium | tests/platform/device/test_device_queue_set.cpp; modules/platform/device/include/SushiRuntime/device/device_queue_set.hpp; modules/platform/device/include/SushiRuntime/device/device_descriptor.hpp
Medium_DeviceQueueSet.TheSetNumbersDevicesByPosition | merge | 4 | same promise (devices are numbered by position) as CountAndInventoryFollowTheDescriptors; the other test gives other inputs, so the two merge into rows | tests/platform/device/test_device_queue_set.cpp; modules/platform/device/include/SushiRuntime/device/device_queue_set.hpp
Medium_DeviceQueueSet.PlainQueueIsOneObjectOnItsDevice | rewrite | 1 | forbidden kind: a for loop in the test body; the set is built in the body, so the queues are made there rather than by the module's base; rewrite as rows over the devices | tests/platform/device/test_device_queue_set.cpp; modules/platform/device/include/SushiRuntime/device/device_queue_set.hpp
Medium_DeviceQueueSet.EachFlavourIsBuiltOnceWithThePropertiesAsked | rewrite | 1 | forbidden kind: a for loop over the flavours in the test body; the oracle restates the set's in-order rule; the set is built in the body; rewrite as rows over the flavours, with the expected slot named | tests/platform/device/test_device_queue_set.cpp; modules/platform/device/include/SushiRuntime/device/device_queue_set.hpp; modules/platform/device/README.md
Medium_DeviceQueueSet.HasProfilingQueueTurnsTrueWhenOneIsBuilt | split | 3 | four actions: a non-profiling build, a profiling build, a second device, and a bad index; each is its own promise | tests/platform/device/test_device_queue_set.cpp; modules/platform/device/include/SushiRuntime/device/device_queue_set.hpp
Medium_DeviceQueueSet.AProfilingSetBuildsEveryPlainQueueWithProfiling | rewrite | 1 | forbidden kind: a for loop over every device in the test body; the set is built in the body; rewrite as rows over the profiling flag | tests/platform/device/test_device_queue_set.cpp; modules/platform/device/include/SushiRuntime/device/device_queue_set.hpp
Medium_DeviceQueueSet.DeviceForNUMAReturnsTheFirstDeviceOfTheNode | resize | 8 | device_for_numa decides on the route table, a value decision; the set is built from sycl devices, so the test reaches the route through a device; the seam is the route table built from NUMA ids as values, which the module lacks | tests/platform/device/test_device_queue_set.cpp; modules/platform/device/include/SushiRuntime/device/device_queue_set.hpp; modules/platform/device/README.md
Medium_DeviceQueueSet.DeviceForNUMAFallsBackToDeviceZeroWhereNoDeviceSits | resize | 8 | the fallback route is a value decision reached through a sycl-device set; the seam is the route table as values; the four ids become rows in the small test | tests/platform/device/test_device_queue_set.cpp; modules/platform/device/include/SushiRuntime/device/device_queue_set.hpp; modules/platform/device/README.md
Medium_DeviceQueueSet.AnInvalidNUMAIDInADescriptorBecomesNodeZero | resize | 8 | the placement of an invalid NUMA id on node 0 is a value decision; the one routing check reads the same placement; the seam is the placement as values | tests/platform/device/test_device_queue_set.cpp; modules/platform/device/include/SushiRuntime/device/device_queue_set.hpp; modules/platform/device/README.md
Medium_DeviceQueueSet.QueueOrNUMALocalReturnsTheNamedDevice | resize | 8 | a named device wins over the fallback, a value decision; the test compares queue addresses of a sycl-device set; the seam returns the device index as a value | tests/platform/device/test_device_queue_set.cpp; modules/platform/device/include/SushiRuntime/device/device_queue_set.hpp; modules/platform/device/README.md
Medium_DeviceQueueSet.QueueOrNUMALocalRotatesOverTheDevicesOfTheNode | rewrite | 1 | forbidden kind: two for loops in the test body; the rotation cursor is thread_local process-wide state (README), reached here by starting a thread; rewrite as rows and an injected cursor | tests/platform/device/test_device_queue_set.cpp; modules/platform/device/include/SushiRuntime/device/device_queue_set.hpp; modules/platform/device/README.md
Medium_DeviceQueueSetDeathTest.ABadIndexStopsNamingIndexAndCount | rewrite | 7 | five accessors in one body, a sign of several tests; the same promise (a bad index stops the process) over other inputs, so the rewrite is one parametrised death test | tests/platform/device/test_device_queue_set.cpp; modules/platform/device/include/SushiRuntime/device/device_queue_set.hpp; modules/platform/device/README.md
Medium_DeviceQueueSetDeathTest.AnIndexIntoAnEmptySetStops | merge | 4 | same promise (a bad index stops the process) with an empty set as the input; merged into the rows of ABadIndexStopsNamingIndexAndCount | tests/platform/device/test_device_queue_set.cpp; modules/platform/device/include/SushiRuntime/device/device_queue_set.hpp
Medium_DeviceQueueSet.AnEmptySetHoldsNothingAndResolvesNoNUMANode | split | 3 | the name claims NUMA resolution the body does not check (that is ANUMALookupOnAnEmptySetStops); the body states the empty state and that wait_all() and raise_asynchronous_errors() succeed; each is its own promise, and the empty-set state needs no device, so it is small | tests/platform/device/test_device_queue_set.cpp; modules/platform/device/include/SushiRuntime/device/device_queue_set.hpp; modules/platform/device/README.md
Medium_DeviceQueueSetDeathTest.ANUMALookupOnAnEmptySetStops | merge | 4 | same promise (a bad lookup stops the process) on an empty set through other accessors; merged into the rows of ABadIndexStopsNamingIndexAndCount | tests/platform/device/test_device_queue_set.cpp; modules/platform/device/include/SushiRuntime/device/device_queue_set.hpp
Medium_DeviceQueueSet.WaitAllReturnsAfterAKernelOnALazilyBuiltQueueFinished | rewrite | 8 | a real kernel and USM are the boundary, correctly medium; the body makes the set and its lazy queue, which the module's base must hand in; the promise (wait_all waits for lazily built queues) is the only test of it | tests/platform/device/test_device_queue_set.cpp; modules/platform/device/include/SushiRuntime/device/device_queue_set.hpp; modules/platform/device/README.md
Medium_DeviceQueueSet.LogsBackendAndDriverInTheDeviceLine | split | 3 | two results joined by "and": the backend and the driver of the device line, each able to break alone | tests/platform/device/test_device_queue_set.cpp; modules/platform/device/source/device_queue_set.cpp
Medium_DeviceQueueSet.LogsTheBuildOfALazyQueueFlavour | split | 3 | two actions: the first build logs one DEBUG line, and a second request logs nothing; each is its own promise | tests/platform/device/test_device_queue_set.cpp; modules/platform/device/source/device_queue_set.cpp; modules/platform/device/README.md
```

### `tests/platform/device/test_queue_flavour.cpp`

```
Small_QueueFlavour.TheDefaultIsThePlainQueue | delete | 1 | forbidden kind: a static_assert in the test and a default value; the plain queue is exercised by the device set tests | tests/platform/device/test_queue_flavour.cpp; modules/platform/device/include/SushiRuntime/device/queue_flavour.hpp
Small_QueueFlavour.EqualityComparesBothFields | keep | 9 | queue_flavour: two flavours are equal exactly when both fields are; the comparison is hand-written, so each combination kills a mutant | tests/platform/device/test_queue_flavour.cpp; modules/platform/device/include/SushiRuntime/device/queue_flavour.hpp
```

### `tests/platform/memory/benchmark_epoch_reclamation.cpp`

```
Stress_EpochStress.HighChurnNoLeak | rewrite | 1 | loops in the test body and in worker threads build the input; the suite prefix Stress_ carries no size and is not Benchmark_, and the file measures nothing, so it is a stress test, not a benchmark; rewrite as a Large_ suite with a named builder | tests/platform/memory/benchmark_epoch_reclamation.cpp; tests/platform/memory/README.md; modules/platform/memory/include/SushiRuntime/memory/epoch_reclamation.hpp
Stress_EpochStress.ThreadSpawnExitHandoff | rewrite | 1 | a busy while loop in a drainer thread and loops in the test body build the input; the suite prefix Stress_ carries no size; rewrite as a Large_ suite with a named builder | tests/platform/memory/benchmark_epoch_reclamation.cpp; modules/platform/memory/include/SushiRuntime/memory/epoch_reclamation.hpp
```

### `tests/platform/memory/test_epoch_reclamation.cpp`

```
Medium_EpochReclamationTest.GuardEnterExitBalanced | rewrite | 1 | EXPECT_NO_THROW wraps guards whose balance it never checks; the nesting promise has no assertion, and the rewrite needs a seam that exposes the slot state | tests/platform/memory/test_epoch_reclamation.cpp; modules/platform/memory/include/SushiRuntime/memory/epoch_reclamation.hpp; tests/platform/memory/README.md
Medium_EpochReclamationTest.RetiredObjectsEventuallyReclaimed | resize | 8 | epoch_reclamation: a retired object is destroyed once no open guard can reach it; it reaches EpochManager::instance(), a process-wide singleton, so the seam is a manager built per test that EpochGuard and retire take | tests/platform/memory/test_epoch_reclamation.cpp; modules/platform/memory/include/SushiRuntime/memory/epoch_reclamation.hpp; tests/platform/memory/README.md
Medium_EpochReclamationTest.ThreadExitHandsOffOrphans | rewrite | 1 | loops in the test body build the input (300 retires in a thread, 1024 retires in the body); the orphan handoff between threads is a boundary and stays medium, and the rewrite moves the loops into a named builder | tests/platform/memory/test_epoch_reclamation.cpp; modules/platform/memory/include/SushiRuntime/memory/epoch_reclamation.hpp; tests/platform/memory/README.md
Medium_EpochReclamationTest.ConcurrentRetireNoCrash | rewrite | 1 | loops in the test body and in each worker build the input; the concurrency promise stays medium and the loops move into a named builder | tests/platform/memory/test_epoch_reclamation.cpp; modules/platform/memory/include/SushiRuntime/memory/epoch_reclamation.hpp
Medium_EpochReclamationDeathTest.AThreadPastTheSlotLimitStopsNamingTheLimit | rewrite | 1 | EXPECT_NO_THROW wraps a guard that cannot throw, since a full slot table stops the process; the slot-return promise has no assertion; split into the death test and a separate slot-return test | tests/platform/memory/test_epoch_reclamation.cpp; modules/platform/memory/include/SushiRuntime/memory/epoch_reclamation.hpp; tests/platform/memory/README.md
Medium_EpochReclamationTest.ABlockedReclaimScansOncePerThreshold | rewrite | 1 | Detail:: symbols (Detail::tls_state, Detail::EpochThreadLocalState::K_RETIRE_THRESHOLD) and the internal scan_count are read, and a loop in the body builds the input; keep the safety promise (an open reader keeps objects alive) and rewrite it on the public API | tests/platform/memory/test_epoch_reclamation.cpp; modules/platform/memory/include/SushiRuntime/memory/epoch_reclamation.hpp
Medium_EpochReclamationTest.ADestructorThatRetiresDeletesParentAndChildOnce | rewrite | 1 | loops in the test body build the input (4 times the threshold of parents, 32 times the threshold of drains); the named builder holds them; the promise is only tested here | tests/platform/memory/test_epoch_reclamation.cpp; modules/platform/memory/include/SushiRuntime/memory/epoch_reclamation.hpp
Medium_EpochReclamation.OrphanedNodesReclaimed | delete | 1 | its promise is tested by ThreadExitHandsOffOrphans in the same file; it also loops in the body and redeclares Tracked | tests/platform/memory/test_epoch_reclamation.cpp
```

### `tests/platform/memory/test_memory_log_category.cpp`

```
Small_MemoryLogCategory.TheCategoryIsNamedAfterTheModule | delete | 1 | static_assert run as a test with SUCCEED(); a log category name is trivial code | tests/platform/memory/test_memory_log_category.cpp; modules/platform/memory/include/SushiRuntime/memory/memory_log_category.hpp
```

### `tests/platform/memory/test_usm_allocation_owner.cpp`

```
Medium_USMAllocationOwner.DestroyingTheAllocationFreesTheUSM | resize | 8 | usm_allocation_owner: destroying an Allocation releases its address once through its allocator; the owner holds a concrete USMAllocator, which takes sycl::queue objects, so the seam is a release port the owner calls and the SYCL free stays medium | tests/platform/memory/test_usm_allocation_owner.cpp; modules/platform/memory/include/SushiRuntime/memory/usm_allocation_owner.hpp; modules/platform/memory/include/SushiRuntime/memory/usm_allocator.hpp
Medium_USMAllocationOwner.AnOwnerWithNoAllocatorDoesNothing | rewrite | 1 | SUCCEED() with no assertion; the null-allocator branch is real logic that no other test covers; rewrite to assert the observable result and make the test small, since it needs no device | tests/platform/memory/test_usm_allocation_owner.cpp; modules/platform/memory/include/SushiRuntime/memory/usm_allocation_owner.hpp
```

### `tests/platform/memory/test_usm_allocator.cpp`

```
Medium_UsmAllocator.EachKindAllocatesIsUsableIsCountedAndIsReleased | rewrite | 1 | loop over the kinds in the body with continue, if and else; the promise is tested only here; rewrite as one row per kind through a named builder, staying medium because a device writes the memory | tests/platform/memory/test_usm_allocator.cpp; modules/platform/memory/include/SushiRuntime/memory/usm_allocator.hpp; modules/platform/memory/source/usm_allocator.cpp
Medium_UsmAllocator.AnAlignedRequestReturnsAnAddressOnTheAlignment | rewrite | 1 | nested loops with continue and if in the body over kind, alignment and size; rewrite as a parametrised test over those rows | tests/platform/memory/test_usm_allocator.cpp; modules/platform/memory/include/SushiRuntime/memory/usm_allocator.hpp; modules/platform/memory/source/usm_allocator.cpp
Medium_UsmAllocator.AnAllocationResolvesToItsDeviceAtItsBaseAndInside | rewrite | 7 | several answers from different calls (device_of, owning_device, containing_base) and a check after release; split the device answer, which stays medium, from the containment answers, which can be small | tests/platform/memory/test_usm_allocator.cpp; modules/platform/memory/include/SushiRuntime/memory/usm_allocator.hpp; modules/platform/memory/source/usm_allocator.cpp
Medium_UsmAllocator.AForeignPointerHasNoDevice | resize | 8 | usm_allocator: an address never recorded has no device, base or owner; device_of and containing_base are logic on values and need no device, but owning_device asks SYCL, so the seam is a pointer-classification port | tests/platform/memory/test_usm_allocator.cpp; modules/platform/memory/include/SushiRuntime/memory/usm_allocator.hpp; modules/platform/memory/source/usm_allocator.cpp
Medium_UsmAllocator.OwningDeviceAsksSYCLAboutUSMItHasNoRecordOf | keep | 9 | usm_allocator: owning_device asks SYCL about USM it has no record of; the oracle is SYCL's own pointer query, the device reach is the promise itself, and the skip when SYCL reports the block is not shared is a precondition | tests/platform/memory/test_usm_allocator.cpp; modules/platform/memory/include/SushiRuntime/memory/usm_allocator.hpp; modules/platform/memory/source/usm_allocator.cpp
Medium_UsmAllocator.RecordOfKeepsTheKindAnAllocationWasMadeAs | split | 3 | several promises: a made allocation's record keeps device and kind until release; an adopted address has no kind | tests/platform/memory/test_usm_allocator.cpp; modules/platform/memory/include/SushiRuntime/memory/usm_allocator.hpp; modules/platform/memory/source/usm_allocator.cpp
Medium_UsmAllocator.AdoptRecordsAnAddressAndForgetDropsIt | split | 3 | several promises: adopt records device, bytes and containment; forget drops the record without freeing and ignores null | tests/platform/memory/test_usm_allocator.cpp; modules/platform/memory/include/SushiRuntime/memory/usm_allocator.hpp; modules/platform/memory/source/usm_allocator.cpp
Medium_UsmAllocator.ReleaseOfAnAdoptedAddressDropsTheRecordAndDoesNotFreeIt | rewrite | 1 | a loop with an ASSERT per byte in the body; the closing write then read of external[0] compares a value with itself and checks nothing; the promise is tested only here; rewrite as one whole-buffer comparison | tests/platform/memory/test_usm_allocator.cpp; modules/platform/memory/include/SushiRuntime/memory/usm_allocator.hpp; modules/platform/memory/source/usm_allocator.cpp
Medium_UsmAllocator.ASecondReleaseLogsOneErrorWithTheAddressAndFreesNothing | rewrite | 1 | ErrorLineCapture is a sink recorder the fixture writes for the logger's sink; the rewrite needs an in-memory log sink adapter, and none exists under modules/; it also reaches the process-wide logger (question 8) | tests/platform/memory/test_usm_allocator.cpp; modules/platform/memory/include/SushiRuntime/memory/usm_allocator.hpp; modules/platform/memory/source/usm_allocator.cpp
Medium_UsmAllocator.ReleaseOfForeignUSMLeavesItAllocated | rewrite | 1 | ErrorLineCapture is a sink recorder written by the fixture, and a loop with an ASSERT per byte sits in the body; the rewrite needs the in-memory log sink adapter; the device allocation stays medium | tests/platform/memory/test_usm_allocator.cpp; modules/platform/memory/include/SushiRuntime/memory/usm_allocator.hpp; modules/platform/memory/source/usm_allocator.cpp
Medium_UsmAllocatorDeathTest.ABadDeviceIndexStopsWithTheIndexAndTheCount | resize | 8 | usm_allocator: a device index out of range stops naming the index and the count; the bound is logic on a value, but the allocator takes sycl::queue objects, so the fixture needs SYCL devices; the seam is a queue port, and the death fork stays a death test | tests/platform/memory/test_usm_allocator.cpp; modules/platform/memory/include/SushiRuntime/memory/usm_allocator.hpp; modules/platform/memory/source/usm_allocator.cpp
Medium_UsmAllocator.TheReleaseObserverHearsEachReleasedAddressExactlyOnce | rewrite | 1 | RecordingObserver is a stand-in written by the test for USMReleaseObserver, a port the module owns; the rewrite needs an in-memory release observer adapter in the module, which does not exist; the test also has five promises to split | tests/platform/memory/test_usm_allocator.cpp; modules/platform/memory/include/SushiRuntime/memory/usm_release_observer.hpp; modules/platform/memory/include/SushiRuntime/memory/usm_allocator.hpp
Medium_UsmAllocator.SetReleaseObserverWaitsForAnObserverCallInFlight | rewrite | 1 | sleep_for of 200 ms to prove that a removal has not returned, which is a forbidden sleep; BlockingObserver is a stand-in for an owned port; rewrite with a clock the test advances and the in-memory observer adapter | tests/platform/memory/test_usm_allocator.cpp; modules/platform/memory/include/SushiRuntime/memory/usm_release_observer.hpp; modules/platform/memory/include/SushiRuntime/memory/usm_allocator.hpp
Medium_UsmAllocatorFailure.LogsTheRefusedRequest | rewrite | 1 | a try and catch in the body with a skip when SYCL throws, so the refusal check can become a skip; the refusal is a SYCL answer, so the rewrite drives an in-memory device port that returns null; the log check uses LogCapture on the process-wide logger | tests/platform/memory/test_usm_allocator.cpp; modules/platform/memory/include/SushiRuntime/memory/usm_request.hpp; modules/platform/memory/source/usm_allocator.cpp
```

### `tests/platform/memory/test_usm_registry.cpp`

```
Medium_UsmRegistry.RecordThenOwnerOfReturnsTheDevice | resize | 8 | registry: owner_of returns the device recorded for a first address; pure value logic labelled Medium, so small needs no seam | tests/platform/memory/test_usm_registry.cpp; modules/platform/memory/include/SushiRuntime/memory/usm_registry.hpp
Medium_UsmRegistry.AnUnrecordedPointerHasNoOwner | resize | 8 | registry: owner_of returns nothing for an unknown address and for null; pure value logic labelled Medium, so small needs no seam | tests/platform/memory/test_usm_registry.cpp; modules/platform/memory/include/SushiRuntime/memory/usm_registry.hpp
Medium_UsmRegistry.TakeReportsTheOwnerAndForgetsThePointer | resize | 8 | registry: take returns the owner once and forgets the pointer; pure value logic labelled Medium, so small needs no seam; absorbs ContainingAllocationForgetsATakenPointer as a row | tests/platform/memory/test_usm_registry.cpp; modules/platform/memory/include/SushiRuntime/memory/usm_registry.hpp
Medium_UsmRegistry.RecordingAPointerTwiceKeepsTheLatestOwner | resize | 8 | registry: a second record of a pointer replaces its owner; pure value logic labelled Medium, so small needs no seam | tests/platform/memory/test_usm_registry.cpp; modules/platform/memory/include/SushiRuntime/memory/usm_registry.hpp
Medium_UsmRegistry.SizeCountsLiveEntries | resize | 8 | registry: size counts recorded entries not yet taken; pure value logic labelled Medium, so small needs no seam | tests/platform/memory/test_usm_registry.cpp; modules/platform/memory/include/SushiRuntime/memory/usm_registry.hpp
Medium_UsmRegistry.ConcurrentRecordAndTakeLoseNothing | rewrite | 1 | a conditional EXPECT inside the worker body (if take returns an owner, then EXPECT_EQ) and loops that build the workload; the thread part stays medium; rewrite to collect each thread's owners and assert after join | tests/platform/memory/test_usm_registry.cpp; modules/platform/memory/include/SushiRuntime/memory/usm_registry.hpp
Medium_UsmRegistry.RecordOfReportsTheDeviceAndKindAtTheBaseOnly | resize | 8 | registry: record_of answers only for a base address with the device and kind; pure value logic labelled Medium, so small needs no seam | tests/platform/memory/test_usm_registry.cpp; modules/platform/memory/include/SushiRuntime/memory/usm_registry.hpp
Medium_UsmRegistry.ContainingAllocationResolvesAnInteriorPointer | resize | 8 | registry: containing_allocation finds the allocation covering an interior byte, the base and the last byte; pure value logic, so small needs no seam; its one-past-end row is the shared not-found row that absorbs ContainingAllocationIgnoresAnUnrelatedPointer | tests/platform/memory/test_usm_registry.cpp; modules/platform/memory/include/SushiRuntime/memory/usm_registry.hpp
Medium_UsmRegistry.ContainingAllocationIgnoresAnUnrelatedPointer | merge | 4 | merge into the rows of ContainingAllocationResolvesAnInteriorPointer; both check that containing_allocation finds nothing outside every recorded allocation | tests/platform/memory/test_usm_registry.cpp; modules/platform/memory/include/SushiRuntime/memory/usm_registry.hpp
Medium_UsmRegistry.AnEntryWithoutALengthResolvesOnlyAtItsBase | resize | 8 | registry: an entry recorded without a length resolves only at its base; pure value logic labelled Medium, so small needs no seam | tests/platform/memory/test_usm_registry.cpp; modules/platform/memory/include/SushiRuntime/memory/usm_registry.hpp
Medium_UsmRegistry.ContainingAllocationForgetsATakenPointer | merge | 4 | merge into the rows of TakeReportsTheOwnerAndForgetsThePointer; both check that a taken pointer is forgotten by the lookups | tests/platform/memory/test_usm_registry.cpp; modules/platform/memory/include/SushiRuntime/memory/usm_registry.hpp
```

### `tests/platform/memory/test_usm_release_observer.cpp`

```
Medium_USMReleaseObserver.HearsEachReleaseWithItsAddress | rewrite | 1 | RecordingObserver is a recorder written by the test for USMReleaseObserver, a port the module owns; the rewrite needs an in-memory release observer adapter in the module, which does not exist | tests/platform/memory/test_usm_release_observer.cpp; modules/platform/memory/include/SushiRuntime/memory/usm_release_observer.hpp; modules/platform/memory/include/SushiRuntime/memory/usm_allocator.hpp
```

### `tests/platform/memory/test_usm_request.cpp`

```
Small_USMRequest.DefaultsToSharedMemoryOnDeviceZero | delete | 1 | a default value; default values are trivial code | tests/platform/memory/test_usm_request.cpp; modules/platform/memory/include/SushiRuntime/memory/usm_request.hpp
Small_USMRequest.ThreeKindsInOneByte | delete | 1 | static_assert run as a test, and distinct constants are trivial code | tests/platform/memory/test_usm_request.cpp; modules/platform/memory/include/SushiRuntime/memory/usm_request.hpp
```

### `tests/platform/numa_lookup/test_numa_lookup.cpp`

```
Small_NumaLookup.EveryPuOfATwoNodeMachineMapsToItsOwnNode | rewrite | 1 | a for loop with ASSERT and EXPECT in the body over the processing units; rewrite as a row table of (unit, node) on the same synthetic topology | tests/platform/numa_lookup/test_numa_lookup.cpp; modules/platform/numa_lookup/include/SushiRuntime/numa_lookup/numa_lookup.hpp
Small_NumaLookup.ANUMANodeIsLocalToItself | keep | 9 | numa_lookup: a NUMA node object is local to itself; the oracle is the object's own os_index, not the code's output | tests/platform/numa_lookup/test_numa_lookup.cpp; modules/platform/numa_lookup/include/SushiRuntime/numa_lookup/numa_lookup.hpp
Small_NumaLookup.ANullObjectHasNoNode | keep | 9 | numa_lookup: a null object has no node | tests/platform/numa_lookup/test_numa_lookup.cpp; modules/platform/numa_lookup/include/SushiRuntime/numa_lookup/numa_lookup.hpp
```

### `tests/platform/observability/test_event_timestamps.cpp`

```
Medium_EventTimestamps.AProfiledEventReadsOrderedTimes | rewrite | 1 | an if and else in the body on has_submit; the invariant submit no later than start holds in both branches, so the branch can go and one expression checks it | tests/platform/observability/test_event_timestamps.cpp; modules/platform/observability/include/SushiRuntime/observability/event_timestamps.hpp
Medium_EventTimestamps.AnUnprofiledEventReadsNothing | merge | 4 | merge into the rows of AnUnreadableEventFailsTheDispatch; both check that a default event reads nothing and leaves the output alone; the test needs no device, so its medium label is wrong | tests/platform/observability/test_event_timestamps.cpp; modules/platform/observability/include/SushiRuntime/observability/event_timestamps.hpp
Small_EventTimestamps.MergesEventsIntoOneSpan | keep | 9 | event_timestamps: merge_timestamps keeps the earliest submit and start and the latest end | tests/platform/observability/test_event_timestamps.cpp; modules/platform/observability/include/SushiRuntime/observability/event_timestamps.hpp
Small_EventTimestamps.TheMergeDoesNotDependOnTheOrder | keep | 9 | event_timestamps: merge_timestamps does not depend on the order of its events; a metamorphic relation, a promise distinct from the span values | tests/platform/observability/test_event_timestamps.cpp; modules/platform/observability/include/SushiRuntime/observability/event_timestamps.hpp
Small_EventTimestamps.TheSubmitIsTheBackendsOnlyWhenEveryEventHadOne | keep | 9 | event_timestamps: the merged submit reading is the backend's own only when every event had one | tests/platform/observability/test_event_timestamps.cpp; modules/platform/observability/include/SushiRuntime/observability/event_timestamps.hpp
Small_EventTimestamps.MergingAnEventIntoItselfChangesNothing | keep | 9 | event_timestamps: merging an event into itself changes nothing | tests/platform/observability/test_event_timestamps.cpp; modules/platform/observability/include/SushiRuntime/observability/event_timestamps.hpp
Small_EventTimestamps.AnUnreadableEventFailsTheDispatch | merge | 4 | survivor of the merge with AnUnprofiledEventReadsNothing; read_dispatch_timestamps returns false for an unreadable event whether returned or handed over, and leaves the output unchanged | tests/platform/observability/test_event_timestamps.cpp; modules/platform/observability/include/SushiRuntime/observability/event_timestamps.hpp
```

### `tests/platform/observability/test_pairing_claim.cpp`

```
Small_PairingClaim.ASubmittedClaimKeepsThePairing | split | 3 | several promises: a submitted claim keeps its device pairing and collect returns the stored event reading; the window starts at a steady reading | tests/platform/observability/test_pairing_claim.cpp; modules/platform/observability/include/SushiRuntime/observability/pairing_claim.hpp; modules/platform/observability/include/SushiRuntime/observability/profiling_ledger.hpp
Small_PairingClaim.AClaimThatSubmittedNothingIsGivenUp | keep | 9 | pairing_claim: a claim that submitted nothing is given up so the next dispatch can claim the device | tests/platform/observability/test_pairing_claim.cpp; modules/platform/observability/include/SushiRuntime/observability/pairing_claim.hpp; modules/platform/observability/include/SushiRuntime/observability/profiling_ledger.hpp
Small_PairingClaim.ALosingClaimChangesNothing | keep | 9 | pairing_claim: a losing claim leaves the ledger as it was | tests/platform/observability/test_pairing_claim.cpp; modules/platform/observability/include/SushiRuntime/observability/pairing_claim.hpp; modules/platform/observability/include/SushiRuntime/observability/profiling_ledger.hpp
Small_PairingClaim.SteadyNsCountsNanosecondsSinceTheEpoch | keep | 9 | pairing_claim: steady_ns counts nanoseconds since the steady clock's epoch; oracle is the arithmetic of 3 microseconds | tests/platform/observability/test_pairing_claim.cpp; modules/platform/observability/include/SushiRuntime/observability/pairing_claim.hpp
```

### `tests/platform/observability/test_profiling_ledger.cpp`

```
Small_ProfilingLedger.MovedFromLedgerClaimsNothingBeforeReserve | merge | 4 | merge into the rows of AMovedLedgerCarriesItsPairings and MoveConstructedSourceReservesLikeFresh; all three check that a moved-from ledger claims nothing before a reserve | tests/platform/observability/test_profiling_ledger.cpp; modules/platform/observability/include/SushiRuntime/observability/profiling_ledger.hpp
Small_ProfilingLedger.MoveConstructedSourceReservesLikeFresh | split | 3 | several promises: the moved-to ledger keeps its pairing; the moved-from ledger holds none; after a move a reserve and a run equal a fresh ledger's run; keep the third as its own test and the first two are rows of the other tests | tests/platform/observability/test_profiling_ledger.cpp; modules/platform/observability/include/SushiRuntime/observability/profiling_ledger.hpp
Small_ProfilingLedger.MoveAssignedSourceReservesLikeFresh | merge | 4 | merge into the rows of MoveConstructedSourceReservesLikeFresh; the same promises through move assignment | tests/platform/observability/test_profiling_ledger.cpp; modules/platform/observability/include/SushiRuntime/observability/profiling_ledger.hpp
Small_ProfilingLedger.RecordFillsEveryField | keep | 9 | profiling_ledger: record fills every field of the record it stores | tests/platform/observability/test_profiling_ledger.cpp; modules/platform/observability/include/SushiRuntime/observability/profiling_ledger.hpp
Small_ProfilingLedger.ARecordWithNoDeviceNamesTheHost | rewrite | 1 | default values of DispatchFacts stand for a dispatch with no device; the promise that such a record names the host is real and tested only here; rewrite to state the host device explicitly, not through defaults | tests/platform/observability/test_profiling_ledger.cpp; modules/platform/observability/include/SushiRuntime/observability/profiling_ledger.hpp
Small_ProfilingLedger.KeepsOnePairingPerDevice | split | 3 | several promises: one owner per device; devices are claimed independently; an out-of-range device claims nothing; a device that never read its event has no pairing; the stored values are reported | tests/platform/observability/test_profiling_ledger.cpp; modules/platform/observability/include/SushiRuntime/observability/profiling_ledger.hpp
Small_ProfilingLedger.AReserveClearsThePairings | keep | 9 | profiling_ledger: reserve clears the pairing claims so the next run starts with none | tests/platform/observability/test_profiling_ledger.cpp; modules/platform/observability/include/SushiRuntime/observability/profiling_ledger.hpp
Small_ProfilingLedger.CountsADispatchThatFoundItsRowFull | merge | 4 | merge into the FullRow part of OverflowBehavesTheSameInDebugAndRelease; both check that a dispatch finding its row full is counted as FullRow and not stored | tests/platform/observability/test_profiling_ledger.cpp; modules/platform/observability/include/SushiRuntime/observability/profiling_ledger.hpp
Small_ProfilingLedger.CountsARecordForANodePastTheRows | keep | 9 | profiling_ledger: a record for a node past the rows is counted as UnknownNode | tests/platform/observability/test_profiling_ledger.cpp; modules/platform/observability/include/SushiRuntime/observability/profiling_ledger.hpp
Small_ProfilingLedger.OverflowBehavesTheSameInDebugAndRelease | rewrite | 1 | loops in the body build the input (five records and a collect), and the test has three promises to split: the overflowed row counts FullRow and keeps its capacity; overflow never aborts; the next reserve sizes the row to the demand | tests/platform/observability/test_profiling_ledger.cpp; modules/platform/observability/include/SushiRuntime/observability/profiling_ledger.hpp
Small_ProfilingLedger.NoteDropCountsEachCauseApart | split | 3 | several promises: each cause is counted apart and the total is their sum; the first drop names its node | tests/platform/observability/test_profiling_ledger.cpp; modules/platform/observability/include/SushiRuntime/observability/profiling_ledger.hpp
Small_ProfilingLedger.ACleanRunLosesNothing | keep | 9 | profiling_ledger: a clean run reports no drop and no first node | tests/platform/observability/test_profiling_ledger.cpp; modules/platform/observability/include/SushiRuntime/observability/profiling_ledger.hpp
Small_ProfilingLedger.AMovedLedgerCarriesItsPairings | merge | 4 | merge into the rows of MoveConstructedSourceReservesLikeFresh; both check that the moved-to ledger keeps its pairing and the moved-from one claims nothing | tests/platform/observability/test_profiling_ledger.cpp; modules/platform/observability/include/SushiRuntime/observability/profiling_ledger.hpp
```

### `tests/platform/observability/test_transfer_counters.cpp`

```
Medium_TransferCounters.BothCountersStartAtZero | delete | 1 | a default value, trivial code; the label Medium is wrong too, since no device is used | tests/platform/observability/test_transfer_counters.cpp; modules/platform/observability/include/SushiRuntime/observability/transfer_counters.hpp
Medium_TransferCounters.AddDeviceToHostRaisesOnlyItsCounter | merge | 4 | merge into the rows of AddHostToDeviceRaisesOnlyItsCounter; both check that one add raises only its own counter; the label Medium is wrong, since no device is used | tests/platform/observability/test_transfer_counters.cpp; modules/platform/observability/include/SushiRuntime/observability/transfer_counters.hpp
Medium_TransferCounters.AddHostToDeviceRaisesOnlyItsCounter | merge | 4 | merge into the rows of AddDeviceToHostRaisesOnlyItsCounter; the mirror of the same promise | tests/platform/observability/test_transfer_counters.cpp; modules/platform/observability/include/SushiRuntime/observability/transfer_counters.hpp
Medium_TransferCounters.RepeatedAddsAccumulate | keep | 9 | transfer_counters: repeated adds accumulate in each counter; the label Medium is wrong, the test is small | tests/platform/observability/test_transfer_counters.cpp; modules/platform/observability/include/SushiRuntime/observability/transfer_counters.hpp
Medium_TransferCounters.ConcurrentAddsGiveExactTotals | rewrite | 1 | loops in each worker body build the workload; move the workers into a named builder; the promise (no lost add across threads) stays medium | tests/platform/observability/test_transfer_counters.cpp; modules/platform/observability/include/SushiRuntime/observability/transfer_counters.hpp
```

### `tests/platform/topology/test_affinity.cpp`

```
Medium_AffinityTest.NegativeCoreLeavesAffinityUnchanged | merge | 4 | merge into the rows of CoreBeyondTheMachineLeavesAffinityUnchanged; both check that an out-of-range pin leaves the affinity unchanged | tests/platform/topology/test_affinity.cpp; modules/platform/topology/include/SushiRuntime/topology/affinity.hpp
Medium_AffinityTest.CoreBeyondTheMachineLeavesAffinityUnchanged | merge | 4 | survivor of the merge with NegativeCoreLeavesAffinityUnchanged and the no-throw part of PinToInvalidCoreIsGraceful | tests/platform/topology/test_affinity.cpp; modules/platform/topology/include/SushiRuntime/topology/affinity.hpp
Medium_AffinityTest.CoreZeroBecomesTheOnlyCoreOfTheThread | keep | 9 | affinity: pinning to core 0 leaves core 0 as the only core; the OS affinity read back is the boundary, so medium is the smallest size that sees it; the skip is a precondition on the OS affinity | tests/platform/topology/test_affinity.cpp; modules/platform/topology/include/SushiRuntime/topology/affinity.hpp
Medium_AffinityTest.PinToValidCore | delete | 1 | try and catch in the worker body; the promise that a valid pin does not throw is checked by CoreZeroBecomesTheOnlyCoreOfTheThread, since a throw ends that worker and fails it | tests/platform/topology/test_affinity.cpp; modules/platform/topology/include/SushiRuntime/topology/affinity.hpp
Medium_AffinityTest.PinToInvalidCoreIsGraceful | delete | 1 | try and catch in the worker body; the promise is covered by NegativeCoreLeavesAffinityUnchanged and CoreBeyondTheMachineLeavesAffinityUnchanged, which check the unchanged affinity and would fail on a throw | tests/platform/topology/test_affinity.cpp; modules/platform/topology/include/SushiRuntime/topology/affinity.hpp
Medium_AffinityTest.PinIsRepeatable | rewrite | 1 | try and catch and a for loop in the worker body; this is the only test of repeated pins, so rewrite it as a named builder with no try in the body | tests/platform/topology/test_affinity.cpp; modules/platform/topology/include/SushiRuntime/topology/affinity.hpp
```

### `tests/platform/topology/test_host_identity.cpp`

```
Small_HostIdentity.HostNameIsStableAndHoldsNoTerminator | split | 3 | several promises: the host name is stable across calls; it holds no terminator | tests/platform/topology/test_host_identity.cpp; modules/platform/topology/include/SushiRuntime/topology/host_identity.hpp
Small_HostIdentity.OperatingSystemIsTheOneCompiledFor | keep | 9 | host_identity: host_operating_system names the operating system the library was compiled for; the oracle is the compile target | tests/platform/topology/test_host_identity.cpp; modules/platform/topology/include/SushiRuntime/topology/host_identity.hpp
```

### `tests/platform/topology/test_hw_discovery.cpp`

```
Medium_HardwareDiscovery.AFilterThatAdmitsNoClassYieldsNoDeviceAndDoesNotThrow | resize | 8 | hw_discovery: a filter that admits no class yields no device and no error from both accessors; the empty result is logic on the filter, but enumeration reads SYCL and hwloc, so the seam is a device-list port and a topology port with in-memory adapters | tests/platform/topology/test_hw_discovery.cpp; modules/platform/topology/include/SushiRuntime/topology/hw_discovery.hpp; modules/platform/topology/include/SushiRuntime/topology/hwloc_topology_provider.hpp
Medium_HardwareDiscovery.NoTwoNodesShareAPCIAddress | rewrite | 1 | a for loop with continue in the body; the invariant is logic over a list of nodes, so rewrite it over fabricated nodes through a named builder and make it small | tests/platform/topology/test_hw_discovery.cpp; modules/platform/topology/include/SushiRuntime/topology/hw_discovery.hpp
Medium_HardwareDiscovery.EveryNodeTheGPUOnlyFilterAdmitsIsAGPU | rewrite | 1 | a for loop with EXPECT_TRUE in the body; the filter is logic over a list of nodes, so rewrite over fabricated nodes and make it small | tests/platform/topology/test_hw_discovery.cpp; modules/platform/topology/include/SushiRuntime/topology/hw_discovery.hpp
Medium_HardwareDiscovery.LogicalIDsAreSequentialAndEveryDeviceIsNamed | rewrite | 1 | a for loop with EXPECTs in the body; two promises that can break apart (sequential ids, named devices) to split; the real enumeration stays medium | tests/platform/topology/test_hw_discovery.cpp; modules/platform/topology/include/SushiRuntime/topology/hw_discovery.hpp
Small_HwDiscovery.LogsAPciAddressThatDoesNotParse | split | 3 | several promises: a valid address parses to its fields and logs nothing; an address that does not parse returns nothing and logs one DEBUG line naming the text and the device; the log part reaches the process-wide logger through LogCapture (question 8), so it needs a log port | tests/platform/topology/test_hw_discovery.cpp; modules/platform/topology/include/SushiRuntime/topology/hw_discovery.hpp
```

### `tests/platform/topology/test_hwloc_topology_provider.cpp`

```
Small_HwlocTopologyProvider.EveryDeviceSitsOnAKnownNode | rewrite | 1 | a for loop with EXPECTs in the body; enumerate reads SYCL and hwloc, so the Small_ label hides a real-machine reach; rewrite over fabricated nodes and a distance model built in the test | tests/platform/topology/test_hwloc_topology_provider.cpp; modules/platform/topology/include/SushiRuntime/topology/hwloc_topology_provider.hpp; modules/platform/topology/include/SushiRuntime/topology/hw_discovery.hpp
Small_HwlocTopologyProvider.TheCoreMapAndTheDistancesAgree | rewrite | 1 | nested loops with EXPECTs in the body; two promises (the core map names valid nodes; local is no more than remote) to split; reads the real hwloc topology | tests/platform/topology/test_hwloc_topology_provider.cpp; modules/platform/topology/include/SushiRuntime/topology/hwloc_topology_provider.hpp; modules/platform/topology/include/SushiRuntime/topology/matrix_distance_model.hpp
```

### `tests/platform/topology/test_matrix_distance_model.cpp`

```
TopologyAbstractionTests.MatrixDistanceModelObeysACPIConvention | merge | 4 | merge into the rows of MatrixDistanceModelReadsTheSourceRow; both check distance(a, b) against the matrix entry of row a | tests/platform/topology/test_matrix_distance_model.cpp; modules/platform/topology/include/SushiRuntime/topology/matrix_distance_model.hpp
TopologyAbstractionTests.MatrixDistanceModelSupportsAsymmetricFabric | merge | 4 | merge into the rows of MatrixDistanceModelReadsTheSourceRow; the same promise on a 3-node matrix; the strict comparison is derived from the entries | tests/platform/topology/test_matrix_distance_model.cpp; modules/platform/topology/include/SushiRuntime/topology/matrix_distance_model.hpp
TopologyAbstractionTests.MatrixDistanceModelReadsTheSourceRow | merge | 4 | survivor: the rows of this test absorb MatrixDistanceModelObeysACPIConvention and MatrixDistanceModelSupportsAsymmetricFabric; distance(a, b) is the matrix entry of row a | tests/platform/topology/test_matrix_distance_model.cpp; modules/platform/topology/include/SushiRuntime/topology/matrix_distance_model.hpp
```

### `tests/platform/topology/test_numa_mapper.cpp`

```
Small_NUMAMapper.ALocalNodeIsTenAway | keep | 9 | numa_mapper: distance(n, n) is 10; the code returns it before any hwloc call | tests/platform/topology/test_numa_mapper.cpp; modules/platform/topology/include/SushiRuntime/topology/numa_mapper.hpp; modules/platform/topology/source/numa_mapper.cpp
Small_NUMAMapper.ARemoteNodeIsFartherThanALocalOne | keep | 9 | numa_mapper: two different nodes are farther apart than a local node before any SLIT table is loaded, the ACPI fallback of 21; the SLIT path after map_distances is untested and is an add | tests/platform/topology/test_numa_mapper.cpp; modules/platform/topology/include/SushiRuntime/topology/numa_mapper.hpp; modules/platform/topology/source/numa_mapper.cpp
Small_NUMAMapper.MappingPlacesEveryDevice | rewrite | 1 | a for loop with EXPECT in the body; the test has two promises (an empty list is left alone; every device gets a node) to split; the mapping reads the real hwloc topology and SYCL, so it stays medium | tests/platform/topology/test_numa_mapper.cpp; modules/platform/topology/include/SushiRuntime/topology/numa_mapper.hpp; modules/platform/topology/source/numa_mapper.cpp
```

### `tests/platform/topology/test_remapping_topology_provider.cpp`

```
TopologyAbstractionTests.RemappingProviderSplitsRealDevicesAcrossFakeNodes | rewrite | 1 | a for loop with EXPECTs in the body; the test reads the real hwloc and SYCL devices through HwlocTopologyProvider (question 8); rewrite over a fake inner TopologyProvider, which the module lacks as an in-memory adapter | tests/platform/topology/test_remapping_topology_provider.cpp; modules/platform/topology/include/SushiRuntime/topology/remapping_topology_provider.hpp; modules/platform/topology/source/remapping_topology_provider.cpp
```

### `tests/platform/topology/test_topology_log_category.cpp`

```
Small_TopologyLogCategory.TheCategoryIsNamedAfterTheModule | delete | 1 | static_assert run as a test with SUCCEED(); a log category name is trivial code | tests/platform/topology/test_topology_log_category.cpp; modules/platform/topology/include/SushiRuntime/topology/topology_log_category.hpp
```

### `tests/platform/topology/test_topology_provider.cpp`

```
Small_TopologyProvider.AProviderAnswersThroughTheInterface | rewrite | 1 | TwoNodeProvider is a stand-in written by the test for TopologyProvider, a port the module owns; the rewrite needs an in-memory TopologyProvider adapter in the module, which does not exist | tests/platform/topology/test_topology_provider.cpp; modules/platform/topology/include/SushiRuntime/topology/topology_provider.hpp
Small_TopologyProvider.ADistanceModelAnswersThroughTheInterface | rewrite | 1 | TwoNodeDistances is a stand-in written by the test for NUMADistanceModel; the rewrite uses MatrixDistanceModel, which is the in-memory adapter that already lives in the port's module | tests/platform/topology/test_topology_provider.cpp; modules/platform/topology/include/SushiRuntime/topology/topology_provider.hpp; modules/platform/topology/include/SushiRuntime/topology/matrix_distance_model.hpp
```

### `tests/platform/topology/test_topology_view.cpp`

```
Small_TopologyView.CoreMapMatchesSyntheticProvider | rewrite | 1 | FixedTopologyProvider is a stand-in written by the test for TopologyProvider; the rewrite needs an in-memory TopologyProvider adapter in the module, which does not exist | tests/platform/topology/test_topology_view.cpp; modules/platform/topology/include/SushiRuntime/topology/topology_view.hpp; modules/platform/topology/include/SushiRuntime/topology/topology_provider.hpp
Small_TopologyView.CoreMapIsReadOnceAtConstruction | rewrite | 1 | FixedTopologyProvider with a read counter is a spy for TopologyProvider; the rewrite needs the same in-memory TopologyProvider adapter, with its read count kept by the adapter | tests/platform/topology/test_topology_view.cpp; modules/platform/topology/include/SushiRuntime/topology/topology_view.hpp; modules/platform/topology/include/SushiRuntime/topology/topology_provider.hpp
Small_TopologyView.CoreMapIsEmptyWhenProviderSuppliesNone | rewrite | 1 | FixedTopologyProvider is a stand-in for TopologyProvider; the rewrite needs the in-memory TopologyProvider adapter | tests/platform/topology/test_topology_view.cpp; modules/platform/topology/include/SushiRuntime/topology/topology_view.hpp; modules/platform/topology/include/SushiRuntime/topology/topology_provider.hpp
Small_TopologyView.DistancesMatchMatrixIncludingDiagonal | rewrite | 1 | FixedTopologyProvider is a stand-in for TopologyProvider, and nested loops with EXPECT in the body; the rewrite uses the in-memory TopologyProvider adapter and a row table of pairs | tests/platform/topology/test_topology_view.cpp; modules/platform/topology/include/SushiRuntime/topology/topology_view.hpp; modules/platform/topology/include/SushiRuntime/topology/matrix_distance_model.hpp
Small_TopologyView.OutOfRangeIDsGetTheModelsAnswer | rewrite | 1 | FixedTopologyProvider is a stand-in for TopologyProvider; the rewrite needs the in-memory TopologyProvider adapter | tests/platform/topology/test_topology_view.cpp; modules/platform/topology/include/SushiRuntime/topology/topology_view.hpp; modules/platform/topology/include/SushiRuntime/topology/matrix_distance_model.hpp
Medium_TopologyViewDeathTest.ANullDistanceModelStops | rewrite | 1 | FixedTopologyProvider is a stand-in for TopologyProvider as the argument; the rewrite passes the in-memory TopologyProvider adapter | tests/platform/topology/test_topology_view.cpp; modules/platform/topology/include/SushiRuntime/topology/topology_view.hpp
Medium_TopologyViewDeathTest.ANullInnerProviderStops | keep | 9 | topology_view: a null inner provider stops the process naming the remapping provider; the constructor touches no device, and a stop is seen only by a death test, so medium is the smallest size | tests/platform/topology/test_topology_view.cpp; modules/platform/topology/include/SushiRuntime/topology/remapping_topology_provider.hpp; modules/platform/topology/source/remapping_topology_provider.cpp
Medium_TopologyViewDeathTest.ARaggedDistanceMatrixStopsWithItsRow | keep | 9 | matrix_distance_model: a ragged matrix stops the process naming the row and both sizes; a stop is seen only by a death test | tests/platform/topology/test_topology_view.cpp; modules/platform/topology/include/SushiRuntime/topology/matrix_distance_model.hpp
Small_TopologyView.CoreMapMatchesRemappingProvider | rewrite | 1 | a for loop with EXPECTs in the body; the test reads the real hwloc topology through RemappingTopologyProvider (question 8); rewrite over an in-memory inner provider | tests/platform/topology/test_topology_view.cpp; modules/platform/topology/include/SushiRuntime/topology/topology_view.hpp; modules/platform/topology/include/SushiRuntime/topology/remapping_topology_provider.hpp
Small_TopologyView.HwlocProviderYieldsOneValidNodePerLogicalCore | rewrite | 1 | a for loop with EXPECTs in the body; the test reads the real hwloc topology, so the Small_ label is wrong; rewrite over an in-memory provider and a row table | tests/platform/topology/test_topology_view.cpp; modules/platform/topology/include/SushiRuntime/topology/topology_view.hpp; modules/platform/topology/include/SushiRuntime/topology/hwloc_topology_provider.hpp
```

### `tests/surface/api/data/test_buffer.cpp`

```
Medium_DataHandlesDeathTest.BufferStopsOnAWindowPastTheEnd | resize | 8 | window check is the inline require_window; a death test reaches it only through rt.buffer, the predicate can be split from the stop as the seam | tests/surface/api/data/test_buffer.cpp; modules/surface/api/include/SushiRuntime/api/data/buffer.hpp
Medium_DataHandlesDeathTest.StateStopsOnAWindowPastTheEnd | merge | 4 | same promise as Medium_DataHandlesDeathTest.BufferStopsOnAWindowPastTheEnd over State; merge into one row table with it | tests/surface/api/data/test_buffer.cpp; modules/surface/api/include/SushiRuntime/api/data/buffer.hpp
Medium_DataHandlesDeathTest.WindowWhoseEndWrapsStops | merge | 4 | same promise with wrapping inputs; merge into the rows of Medium_DataHandlesDeathTest.BufferStopsOnAWindowPastTheEnd | tests/surface/api/data/test_buffer.cpp; modules/surface/api/include/SushiRuntime/api/data/buffer.hpp
Medium_DataHandles.EmptyWindowAtTheEndIsAccepted | resize | 8 | acceptance is the same host predicate as require_window; the seam of BufferStopsOnAWindowPastTheEnd serves it; the value read stays on the device | tests/surface/api/data/test_buffer.cpp; modules/surface/api/include/SushiRuntime/api/data/buffer.hpp
Medium_DataHandles.FittingWindowRoundTrips | rewrite | 1 | control flow in body: range-for loop sets the host elements; runtime made in body; the only round-trip promise of Buffer in this chunk | tests/surface/api/data/test_buffer.cpp
Medium_DataHandlesDeathTest.NullOrShortSourceWithACountStops | split | 3 | two promises: a null source stops (Buffer and State) and a short source stops (Buffer); each its own death test | tests/surface/api/data/test_buffer.cpp; modules/surface/api/include/SushiRuntime/api/data/buffer.hpp
Medium_DataHandlesDeathTest.OverflowingElementCountStopsBeforeAllocating | resize | 8 | checked_byte_count is inline and stops on values; seam: split the overflow predicate from SR_VERIFY so a small test checks it | tests/surface/api/data/test_buffer.cpp; modules/surface/api/include/SushiRuntime/api/data/buffer.hpp
Medium_DataHandles.StateAndBufferAnswerTheSharedQueriesAlike | split | 3 | metadata agreement and region offset and length are two promises | tests/surface/api/data/test_buffer.cpp; modules/surface/api/include/SushiRuntime/api/data/buffer.hpp
Medium_DataHandles.TriviallyCopyableStructRoundTrips | rewrite | 1 | bare tolerance literal 1e-6f in EXPECT_NEAR, while the test's own comment says the copy is bitwise; values are dyadic so an exact comparison is the oracle; only round-trip test of the struct | tests/surface/api/data/test_buffer.cpp
Medium_DataHandlesDeathTest.HostOnADeviceResidentHandleStops | merge | 4 | same promise as Medium_DeviceResidentDeathTest.HostOnADeviceBufferStops in the same file; merge into rows over handle kind | tests/surface/api/data/test_buffer.cpp
Medium_DataHandlesDeathTest.IndexingADeviceResidentHandleStops | resize | 8 | residency check reached only through a device-resident handle; seam: a host predicate; the test exists only when SR_ENABLE_ASSERTIONS is set | tests/surface/api/data/test_buffer.cpp; modules/surface/api/include/SushiRuntime/api/data/buffer.hpp
Medium_DataHandlesDeathTest.DeviceIndexThatNamesNoDeviceStops | resize | 8 | the check compares the index with the device count read through rt.advanced(); seam: a predicate over index and count | tests/surface/api/data/test_buffer.cpp; modules/surface/api/include/SushiRuntime/api/data/buffer.hpp
Medium_DataHandles.AlignedAllocationStartsOnAMultipleOfTheAlignment | rewrite | 1 | control flow in body: loop over residencies and if with continue; a probe that fails skips the residency and the test still passes, which Determinism rule 2 forbids; runtime made in body | tests/surface/api/data/test_buffer.cpp
Medium_DataHandlesDeathTest.AlignmentThatIsNotAPowerOfTwoStops | resize | 8 | power-of-two check reached only through rt.buffer and rt.state; seam: a host predicate | tests/surface/api/data/test_buffer.cpp; modules/surface/api/include/SushiRuntime/api/data/buffer.hpp
Medium_DeviceResident.BufferRoundTrip | rewrite | 1 | control flow in body: ASSERT loop over the output; runtime made in body; the device promise is the kernel over the device buffer | tests/surface/api/data/test_buffer.cpp
Medium_DeviceResident.SelectiveReadback | rewrite | 1 | control flow in body: loop over the window; runtime made in body; name states a feature, not a promise | tests/surface/api/data/test_buffer.cpp
Medium_DeviceResident.StateStepsResident | rewrite | 1 | control flow in body: ASSERT loop; runtime made in body | tests/surface/api/data/test_buffer.cpp
Medium_DeviceResident.SharedDefaultUnchanged | rewrite | 1 | a default value is checked (forbidden kind) and loops are in the body; the host-indexing promise is real and has no other test here | tests/surface/api/data/test_buffer.cpp
Medium_DeviceResident.HostSpanIndexesSharedBuffer | rewrite | 1 | control flow in body: two loops; runtime made in body; the span promise is real and has no other test here | tests/surface/api/data/test_buffer.cpp
Medium_DeviceResidentDeathTest.HostOnADeviceBufferStops | merge | 4 | same promise as Medium_DataHandlesDeathTest.HostOnADeviceResidentHandleStops in the same file | tests/surface/api/data/test_buffer.cpp
Medium_DeviceResident.HostBufferIsWrittenByAKernelAndReadOnTheHost | rewrite | 1 | control flow in body: two loops; runtime made in body | tests/surface/api/data/test_buffer.cpp
Medium_Buffer.AdoptingConstructorExposesTheAdoptedAddress | rewrite | 1 | spy: CountingOwner records the releases of the AllocationOwner port of foundation core; the rewrite needs an in-memory AllocationOwner adapter in that core module, and none exists; the test needs no device, so the adapter makes it small | tests/surface/api/data/test_buffer.cpp; modules/foundation/core/include/SushiRuntime/core/allocation.hpp; tests/foundation/core/test_allocation.cpp (name search)
Medium_Buffer.DestructionReleasesTheAdoptedAllocationOnce | rewrite | 1 | same spy, same missing in-memory AllocationOwner adapter in foundation core | tests/surface/api/data/test_buffer.cpp; modules/foundation/core/include/SushiRuntime/core/allocation.hpp; tests/foundation/core/test_allocation.cpp (name search)
Medium_Buffer.MovingAnAdoptedBufferReleasesTheAllocationOnce | rewrite | 1 | same spy, same missing adapter | tests/surface/api/data/test_buffer.cpp; modules/foundation/core/include/SushiRuntime/core/allocation.hpp
Medium_BufferDeathTest.AdoptingATooSmallAllocationStops | rewrite | 1 | same spy, same missing adapter; the stop is a host check that needs no device | tests/surface/api/data/test_buffer.cpp; modules/foundation/core/include/SushiRuntime/core/allocation.hpp
Medium_BufferDeathTest.AdoptingACountThatOverflowsItsByteSizeStops | rewrite | 1 | same spy, same missing adapter | tests/surface/api/data/test_buffer.cpp; modules/foundation/core/include/SushiRuntime/core/allocation.hpp
```

### `tests/surface/api/data/test_state.cpp`

```
Medium_StateMove.ARuleFollowsAStateMovedAfterAdd | rewrite | 8 | device run is the promise, so medium is the right size; runtime made in body, which the size does not allow; rebuild on the shared runtime base | tests/surface/api/data/test_state.cpp
Medium_StateMove.ARuleFollowsAStateItsVectorReallocated | rewrite | 1 | control flow in body: while loop that pushes until the address changes, which depends on the allocator; runtime made in body | tests/surface/api/data/test_state.cpp
Medium_StateMove.ACoupledRuleFollowsItsMovedStates | rewrite | 1 | control flow in body: ASSERT loop; runtime made in body | tests/surface/api/data/test_state.cpp
Medium_StateMove.ADynamicStateRuleFollowsAMovedState | rewrite | 1 | change detector: compile_count asserts how the graph compiles; keep the moved-state value check; runtime made in body | tests/surface/api/data/test_state.cpp
Medium_StateMove.TheViewFollowsAdvanceMoveAndDestruction | delete | 1 | forbidden kind: includes api/detail/handle_access.hpp and asserts Detail::StateView pointers; the public promises (moved State keeps stepping) are rows ARuleFollowsAStateMovedAfterAdd and ADynamicStateRuleFollowsAMovedState | tests/surface/api/data/test_state.cpp; modules/surface/api/include/SushiRuntime/api/detail/handle_access.hpp
Medium_StateMove.AStateAssignedOverRefusesTheRun | resize | 8 | the refusal is has_dangling_resource checked in the run path of step_driver.hpp before work starts; seam: expose the check so a host-only graph can see it | tests/surface/api/data/test_state.cpp; modules/surface/api/include/SushiRuntime/api/graph/plumbing/step_driver.hpp
Medium_StateMove.ADynamicStateRuleReplayedAfterAStepReadsTheCurrentField | rewrite | 1 | change detector: asserts plan node index, predecessor_count and reads base; control flow: ASSERT loop; keep the value check after the replay | tests/surface/api/data/test_state.cpp
```

### `tests/surface/api/detail/test_assertion_logging.cpp`

```
Medium_AssertDeathTest.LogsTheFailedCheckAtFatal | keep | 9 | a failed check writes a FATAL record with its text; the death test is the boundary (stderr and abort), no seam to the handler | tests/surface/api/detail/test_assertion_logging.cpp; modules/surface/api/source/assertion_logging.cpp; modules/surface/api/include/SushiRuntime/api/detail/assertion_logging.hpp; modules/foundation/core/source/assert.cpp
Medium_AssertDeathTest.TheDefaultHandlerStillWritesItsLineAfterTheLog | keep | 9 | chaining to the previous handler, a different promise from the format of the row above; the regex matches the line assert.cpp writes | tests/surface/api/detail/test_assertion_logging.cpp; modules/surface/api/source/assertion_logging.cpp; modules/foundation/core/source/assert.cpp
Small_AssertHandler.InstallingTwiceIsHarmless | rewrite | 1 | forbidden kind: SUCCEED() only; installs the process-wide handler from a small test; the promise (second install is a no-op) has no other test; rewrite as a medium death test or behind a seam | tests/surface/api/detail/test_assertion_logging.cpp; modules/surface/api/source/assertion_logging.cpp
```

### `tests/surface/api/detail/test_before_step_hooks.cpp`

```
Medium_BeforeStepHooks.HooksRunInOrderWithTheCount | split | 3 | two promises: order and count of the calls, and a moved object keeps its hooks; both are host-only, so both are small (the Medium label is wrong) | tests/surface/api/detail/test_before_step_hooks.cpp; modules/surface/api/include/SushiRuntime/api/detail/before_step_hooks.hpp
```

### `tests/surface/api/detail/test_callable_checks.cpp`

```
Small_CallableChecks.EachLaunchTakesItsIndex | rewrite | 1 | forbidden kind: static_assert run as a test with SUCCEED(); the index contract has no run-time test; rewrite with EXPECT on the trait values | tests/surface/api/detail/test_callable_checks.cpp; modules/surface/api/include/SushiRuntime/api/detail/callable_checks.hpp
```

### `tests/surface/api/detail/test_callable_traits.cpp`

```
Small_CallableTraits.AMutablePerElementKernelIsRejected | keep | 9 | the const call decides, so mutable is rejected; the plain kernel is the control of the same contrast | tests/surface/api/detail/test_callable_traits.cpp; modules/surface/api/include/SushiRuntime/api/detail/callable_traits.hpp
Small_CallableTraits.AMutableRangeKernelIsRejected | keep | 9 | range kernel judged by its const call; mutable rejected | tests/surface/api/detail/test_callable_traits.cpp; modules/surface/api/include/SushiRuntime/api/detail/callable_traits.hpp
Small_CallableTraits.AMutableRawKernelIsAccepted | keep | 9 | raw kernel is invoked non-const by raw_node, so mutable is accepted | tests/surface/api/detail/test_callable_traits.cpp; modules/surface/api/include/SushiRuntime/api/detail/callable_traits.hpp
Small_CallableTraits.ACoupledKernelMustTakeItsInputsAsConst | keep | 9 | one contract: coupled kernels take their inputs const; the four asserts are the two contrasts of that contract | tests/surface/api/detail/test_callable_traits.cpp; modules/surface/api/include/SushiRuntime/api/detail/callable_checks.hpp
```

### `tests/surface/api/detail/test_device_fault.cpp`

```
Small_DeviceFault.AFieldPastAUint32IsStoredSaturated | rewrite | 1 | control flow in body: if with GTEST_SKIP on the size of size_t; only test of the saturation promise; gate with a static_assert or a fixed 64-bit check | tests/surface/api/detail/test_device_fault.cpp; modules/surface/api/include/SushiRuntime/api/detail/device_fault.hpp
```

### `tests/surface/api/detail/test_device_pass.cpp`

```
Small_DevicePass.AnswersFalseOnlyForDpcppWithoutFsycl | rewrite | 6 | oracle is a copy of the header's own preprocessor condition, so it restates the code under test; rewrite with a named oracle: the -fsycl state the build gives this unit | tests/surface/api/detail/test_device_pass.cpp; modules/surface/api/include/SushiRuntime/api/detail/device_pass.hpp
```

### `tests/surface/api/detail/test_dropped_outcome.cpp`

```
Small_DroppedOutcome.LogsTheErrorAndWhereItWasDropped | resize | 8 | LogCapture installs a process-wide sink from a small test; seam: log_dropped_outcome takes a sink or logger port with an in-memory adapter | tests/surface/api/detail/test_dropped_outcome.cpp; modules/surface/api/include/SushiRuntime/api/detail/dropped_outcome.hpp; modules/foundation/test_runner/include/SushiRuntime/test_runner/log_capture.hpp
```

### `tests/surface/api/detail/test_emit_captured.cpp`

```
Medium_EmitCaptured.TheKernelReceivesEachPointer | rewrite | 1 | control flow in body: EXPECT loop; runtime made in body; the same promise also appears in tests/surface/api/graph/plumbing/test_builder_surface.cpp (CountLaunchWithCapturesPassesPointers, name search), and the unit's own file stays by the ruling | tests/surface/api/detail/test_emit_captured.cpp; tests/surface/api/detail/description_runner.hpp; modules/surface/api/include/SushiRuntime/api/detail/emit_captured.hpp; modules/surface/api/include/SushiRuntime/api/detail/emit_launch.hpp; tests/surface/api/graph/plumbing/test_builder_surface.cpp (name search)
```

### `tests/surface/api/detail/test_emit_device_sized.cpp`

```
Medium_EmitDeviceSized.OnlyTheLiveIndicesRun | rewrite | 1 | control flow in body: EXPECT loop; runtime made in body; the live-width guard is device-only logic inline in the launch, so the seam is a host-callable guard, a question 8 step after the rewrite; graph test tests/surface/api/graph/test_device_sized.cpp (OnlyItemsBelowTheLiveWidthRun, name search) has the same promise, and the unit's own file stays | tests/surface/api/detail/test_emit_device_sized.cpp; tests/surface/api/detail/description_runner.hpp; modules/surface/api/include/SushiRuntime/api/detail/emit_device_sized.hpp; tests/surface/api/graph/test_device_sized.cpp (name search)
```

### `tests/surface/api/detail/test_emit_dynamic.cpp`

```
Medium_EmitDynamic.TheKernelRunsOverTheLiveCount | rewrite | 1 | control flow in body: EXPECT loop; runtime made in body; window resolution is host-callable in late_window.hpp, so only the wiring needs the device | tests/surface/api/detail/test_emit_dynamic.cpp; tests/surface/api/detail/description_runner.hpp; modules/surface/api/include/SushiRuntime/api/detail/emit_dynamic.hpp; modules/surface/api/include/SushiRuntime/api/detail/late_window.hpp; tests/surface/api/graph/test_dynamic_graph.cpp (name search)
```

### `tests/surface/api/detail/test_emit_host.cpp`

```
Medium_EmitHost.AHostNodeRunsItsFunction | split | 3 | two promises: the description fields and the once-run of the worker function; neither needs a device; run_description calls the worker without touching the queue | tests/surface/api/detail/test_emit_host.cpp; tests/surface/api/detail/description_runner.hpp; modules/surface/api/include/SushiRuntime/api/detail/emit_host.hpp
```

### `tests/surface/api/detail/test_emit_late_count_resolver.cpp`

```
Medium_EmitLateCountResolver.TheFillReceivesTheLiveCounts | rewrite | 8 | the device single_task reads the counts and calls fill: the device run is the promise, so medium is right; runtime made in body; host resolution is small-tested in test_late_count_resolution.cpp | tests/surface/api/detail/test_emit_late_count_resolver.cpp; tests/surface/api/detail/description_runner.hpp; modules/surface/api/include/SushiRuntime/api/detail/emit_late_count_resolver.hpp; modules/surface/api/include/SushiRuntime/api/detail/late_count_resolution.hpp
```

### `tests/surface/api/detail/test_emit_launch.cpp`

```
Medium_LaunchReach.ReachSaturatesAtTheLargestSize | split | 3 | three promises: rank-1 reach inside the extent, wrapping reach for an extent and an nd-range, and saturated_reach; all host-only, so small | tests/surface/api/detail/test_emit_launch.cpp; modules/surface/api/include/SushiRuntime/api/detail/emit_launch.hpp
Medium_LaunchReach.AnExtentWhoseEndWrapsIsRefusedAtAdd | split | 3 | two promises: the refusal decision (launch_refusal, host-callable, small) and the rule not recorded after refusal (needs a graph, medium); graph test tests/surface/api/graph/plumbing/test_builder_surface.cpp (ALaunchPastItsStateIsRefusedAtAdd, name search) has the same promise, the unit's own file stays | tests/surface/api/detail/test_emit_launch.cpp; modules/surface/api/include/SushiRuntime/api/detail/emit_launch.hpp; tests/surface/api/graph/plumbing/test_builder_surface.cpp (name search)
```

### `tests/surface/api/detail/test_emit_state.cpp`

```
Medium_StateEmitters.ACoupledRuleReadsItsInputsThroughConstPointers | rewrite | 1 | control flow in body: ASSERT loop; runtime made in body; graph run is the device promise | tests/surface/api/detail/test_emit_state.cpp; modules/surface/api/include/SushiRuntime/api/detail/emit_state.hpp; tests/surface/api/graph/plumbing/test_builder_surface.cpp (name search)
Medium_StateEmitters.StateCoupledAndUntrackedNodesReportTheirLaunchWidth | rewrite | 1 | spy: RunRecorder wraps RecordingObserver from the foundation test_runner module, not the module of the RunObserver port; the rewrite needs an in-memory RunObserver adapter in the report module; control flow: range-for loop over events; runtime made in body | tests/surface/api/detail/test_emit_state.cpp; tests/surface/api/run_recorder.hpp; modules/foundation/test_runner/include/SushiRuntime/test_runner/recording_observer.hpp; modules/foundation/report/include/SushiRuntime/report/run_observer.hpp; modules/surface/api/include/SushiRuntime/api/detail/emit_state.hpp
Medium_StateEmitters.AStateNodeBuiltFromItsViewRunsTheRule | rewrite | 1 | control flow in body: ASSERT loop; runtime made in body | tests/surface/api/detail/test_emit_state.cpp; modules/surface/api/include/SushiRuntime/api/detail/emit_state.hpp
Medium_StateEmitters.ACoupledNodeBuiltFromItsViewsRunsTheRule | rewrite | 1 | control flow in body: ASSERT loop; runtime made in body | tests/surface/api/detail/test_emit_state.cpp; modules/surface/api/include/SushiRuntime/api/detail/emit_state.hpp
Medium_StateEmitters.ADynamicStateNodeBuiltFromItsViewRunsTheLiveWindow | rewrite | 1 | control flow in body: ASSERT loop; runtime made in body; window resolution is host-callable in late_window.hpp | tests/surface/api/detail/test_emit_state.cpp; modules/surface/api/include/SushiRuntime/api/detail/emit_state.hpp; modules/surface/api/include/SushiRuntime/api/detail/late_window.hpp
```

### `tests/surface/api/detail/test_emit_task.cpp`

```
Medium_EmitTask.ATaskNodeRunsOnTheQueue | split | 3 | two promises: description fields (host) and the queue work running (device) | tests/surface/api/detail/test_emit_task.cpp; tests/surface/api/detail/description_runner.hpp; modules/surface/api/include/SushiRuntime/api/detail/emit_task.hpp
```

### `tests/surface/api/detail/test_emit_tracked.cpp`

```
Medium_EmitTracked.ALaunchRunsOncePerIndex | rewrite | 1 | control flow in body: loop writing and loop checking; runtime made in body; sibling graph tests have the same promise, the unit's own file stays | tests/surface/api/detail/test_emit_tracked.cpp; tests/surface/api/detail/description_runner.hpp; modules/surface/api/include/SushiRuntime/api/detail/emit_tracked.hpp; modules/surface/api/include/SushiRuntime/api/detail/emit_launch.hpp
Medium_EmitTracked.ARangeKernelGetsTheWholeRange | split | 3 | two promises: the width field (host) and the range kernel receiving the whole range (device) | tests/surface/api/detail/test_emit_tracked.cpp; tests/surface/api/detail/description_runner.hpp; modules/surface/api/include/SushiRuntime/api/detail/emit_tracked.hpp
Medium_EmitTracked.ARawNodeKeepsTheStatedWidth | split | 3 | two promises: the stated width (host) and the raw command group running (device) | tests/surface/api/detail/test_emit_tracked.cpp; tests/surface/api/detail/description_runner.hpp; modules/surface/api/include/SushiRuntime/api/detail/emit_tracked.hpp
```

### `tests/surface/api/detail/test_emit_untracked.cpp`

```
Medium_EmitUntracked.AnUntrackedLaunchNamesItsDevice | rewrite | 1 | control flow in body: loop checks the output; runtime made in body | tests/surface/api/detail/test_emit_untracked.cpp; tests/surface/api/detail/description_runner.hpp; modules/surface/api/include/SushiRuntime/api/detail/emit_untracked.hpp; modules/surface/api/include/SushiRuntime/api/detail/emit_launch.hpp
Medium_EmitUntracked.RangeAndRawForms | split | 3 | name has "and": the range form and the raw form are two promises on host-only fields; small | tests/surface/api/detail/test_emit_untracked.cpp; modules/surface/api/include/SushiRuntime/api/detail/emit_untracked.hpp
```

### `tests/surface/api/detail/test_graph_id.cpp`

```
Small_GraphId.IdsAreNonZeroDistinctAndMovedFromReportsZero | split | 3 | name has "and": non-zero distinct identities and the moved-from zero are two promises | tests/surface/api/detail/test_graph_id.cpp; modules/surface/api/include/SushiRuntime/api/detail/graph_id.hpp
Small_GraphId.MoveAssignmentHandsTheIdentityOver | keep | 9 | move assignment is a separate promise from move construction and checks the identity and the zero | tests/surface/api/detail/test_graph_id.cpp; modules/surface/api/include/SushiRuntime/api/detail/graph_id.hpp
Small_GraphId.SeveralThreadsNeverTakeTheSameId | rewrite | 1 | control flow in body: two loops; threads in a small test, which the size forbids; the promise is a process-wide counter, which belongs in a medium test under ThreadSanitizer | tests/surface/api/detail/test_graph_id.cpp; modules/surface/api/include/SushiRuntime/api/detail/graph_id.hpp
```

### `tests/surface/api/detail/test_handle_access.cpp`

```
Medium_HandleAccess.ABufferKeyIsItsData | resize | 8 | key and liveness are host reads; a runtime is made only to get a buffer; seam: an adopted block through HandleAccess::construct, as the adoption tests in test_buffer.cpp do | tests/surface/api/detail/test_handle_access.cpp; modules/surface/api/include/SushiRuntime/api/detail/handle_access.hpp; tests/surface/api/detail/description_runner.hpp
Medium_HandleAccess.AStateReadsCurrentAndWritesNext | resize | 8 | read and write keys are host reads; the runtime is needed only to make the State; same seam as ABufferKeyIsItsData | tests/surface/api/detail/test_handle_access.cpp; modules/surface/api/include/SushiRuntime/api/detail/handle_access.hpp; tests/surface/api/detail/description_runner.hpp
```

### `tests/surface/api/detail/test_late_controls.cpp`

```
Small_LateControls.IsHeldByPointerOnly | delete | 1 | forbidden kinds: static_assert run as a test and a default-value check on a shared_ptr; no run-time behaviour is stated, and the headers that hold the pointer compile against the same shape | tests/surface/api/detail/test_late_controls.cpp; modules/surface/api/include/SushiRuntime/api/detail/late_controls.hpp
```

### `tests/surface/api/detail/test_late_count_resolution.cpp`

```
Small_LateCountResolution.HostCountsResolveOnTheHost | keep | 9 | one promise: fixed and host counts resolve to their live values on the host | tests/surface/api/detail/test_late_count_resolution.cpp; modules/surface/api/include/SushiRuntime/api/detail/late_count_resolution.hpp
Small_LateCountResolution.AHostCountAboveTheCapacityResolvesToZero | keep | 9 | a host count above capacity records a late-count fault and resolves to zero; the fault words match the record layout of device_fault.hpp | tests/surface/api/detail/test_late_count_resolution.cpp; modules/surface/api/include/SushiRuntime/api/detail/late_count_resolution.hpp; modules/surface/api/include/SushiRuntime/api/detail/device_fault.hpp
```

### `tests/surface/api/detail/test_late_window.cpp`

```
Small_LateWindow.AWindowInsideTheCapacityResolves | keep | 9 | a window inside capacity resolves to itself | tests/surface/api/detail/test_late_window.cpp; modules/surface/api/include/SushiRuntime/api/detail/late_window.hpp
Small_LateWindow.NoHostCountTakesTheRestOfTheCapacity | keep | 9 | rest_of_capacity takes the capacity from base | tests/surface/api/detail/test_late_window.cpp; modules/surface/api/include/SushiRuntime/api/detail/late_window.hpp
Small_LateWindow.AWindowPastTheCapacityIsAFault | keep | 9 | a window past capacity records the window fault words | tests/surface/api/detail/test_late_window.cpp; modules/surface/api/include/SushiRuntime/api/detail/late_window.hpp; modules/surface/api/include/SushiRuntime/api/detail/device_fault.hpp
Small_LateWindow.TheWindowIsReadFromTheBindings | split | 3 | two promises: an unbound window reads as rest of capacity, and bound count and base are read back | tests/surface/api/detail/test_late_window.cpp; modules/surface/api/include/SushiRuntime/api/detail/late_window.hpp
```

### `tests/surface/api/detail/test_node_description.cpp`

```
Small_NodeDescription.DefaultsDescribeAnEmptyKernel | delete | 1 | forbidden kind: default-value test; the @brief says device 0 while the assertion checks that no device is set, a mismatch to fix in the file if it stays | tests/surface/api/detail/test_node_description.cpp; modules/surface/api/include/SushiRuntime/api/detail/node_description.hpp
```

### `tests/surface/api/detail/test_node_spec.cpp`

```
Small_NodeSpec.KeepsItsDescription | rewrite | 1 | NodeSpec: holds the description it was built from and starts with an empty stamp; reaches Detail::NodeSpec and Detail::NodeDescription (Detail symbols, Q1), and no clean test of the promise exists in the module; the rewrite checks the description through the public recorder and plan | tests/surface/api/detail/test_node_spec.cpp; modules/surface/api/include/SushiRuntime/api/detail/node_spec.hpp; tests/surface/api/detail/README.md (grep: NodeSpec only here)
```

### `tests/surface/api/detail/test_node_stamp.cpp`

```
Small_NodeStamp.CopiesWhatTheOptionsState | rewrite | 1 | NodeStamp: copies the operation, placement, determinism, width and parameters of the options; Detail::NodeStamp and Detail::K_MAX_NODE_PARAMETERS (Q1), no clean test of the copy promise; the rewrite checks the copied values through a public plan (PlanView nodes) | tests/surface/api/detail/test_node_stamp.cpp; modules/surface/api/include/SushiRuntime/api/detail/node_stamp.hpp; tests/surface/api/graph/test_plan_view.cpp (grep: public plan node checks)
Small_NodeStamp.OneParameterPastTheLimitIsRefused | rewrite | 1 | parameters_refusal: nothing at the limit, an InvalidGraph error one past it; Detail::parameters_refusal (Q1); the same promise is checked publicly by graph/test_node_options.cpp::MoreParametersThanANodeCarriesAreRefused. The Q4 ruling names this unit-module test as the one that stays, so it is rewritten and the public one is deleted (see that record). The Q1 rule would delete this test instead; the two rules conflict here and the owner decides | tests/surface/api/detail/test_node_stamp.cpp; modules/surface/api/include/SushiRuntime/api/detail/node_stamp.hpp; tests/surface/api/graph/test_node_options.cpp
```

### `tests/surface/api/detail/test_plan.cpp`

```
Small_Plan.IsHeldByPointerOnly | delete | 1 | the test has no promise to keep: a static_assert that Plan is a class, and EXPECT_EQ of a nullptr initialised to nullptr (an assertion on a literal, a forbidden kind); the surface holds Plan by pointer through the forward declaration, which the compiler already enforces | tests/surface/api/detail/test_plan.cpp; modules/surface/api/include/SushiRuntime/api/detail/plan.hpp
```

### `tests/surface/api/detail/test_step_machinery.cpp`

```
Medium_StepMachinery.OneViewPerState | rewrite | 1 | StepMachinery::view_for returns the same view for the same State; Detail::StepMachinery (Q1), a runtime made in the body; the rewrite checks the view through the public State API | tests/surface/api/detail/test_step_machinery.cpp; modules/surface/api/include/SushiRuntime/api/detail/step_machinery.hpp
Medium_StepMachinery.AFaultRecordBecomesAnError | rewrite | 1 | StepMachinery: a clean fault record reports nothing and a window fault record becomes an error (two outcomes, split at the rewrite); Detail::StepMachinery and Detail::record_device_fault (Q1); no clean test in the module; the rewrite goes through a public faulting kernel | tests/surface/api/detail/test_step_machinery.cpp; modules/surface/api/include/SushiRuntime/api/detail/step_machinery.hpp; tests/surface/api/detail/test_device_fault.cpp (grep only)
```

### `tests/surface/api/detail/test_step_run.cpp`

```
Medium_StepRun.ThePredicateForwardsTheCount | rewrite | 1 | StepPredicate: forwards the completed count to the caller's callable and returns its answer; Detail::StepPredicate (Q1); the promise is pure host logic, so the rewrite is small through run_until, which the step driver test already exercises | tests/surface/api/detail/test_step_run.cpp; modules/surface/api/include/SushiRuntime/api/detail/step_run.hpp
Medium_StepRun.AnIdleRuntimeHasNoRunInProgress | rewrite | 1 | step_run: run_in_progress is false on an idle runtime and cancel_run on it changes nothing; Detail::run_in_progress, Detail::cancel_run and Detail::ContextHandleAccess (Q1, and a context reach); the rewrite checks the public run state through a public run handle | tests/surface/api/detail/test_step_run.cpp; modules/surface/api/include/SushiRuntime/api/detail/step_run.hpp; tests/surface/context_access/test_context_access.cpp (grep only)
```

### `tests/surface/api/graph/benchmark_dynamic_graph.cpp`

```
Performance_DynamicGraph.ManyRegionsStreamingMutations | split | 3 | two promises: each mutation of a 256-region graph recomposes exactly once; every region ends with the count its runs give it; the timing it records (RecordProperty) is a benchmark measurement and moves to a Benchmark_ suite. The suite name Performance_ has no size prefix and is not Benchmark_, so the size rule of testing fails it at build | tests/surface/api/graph/benchmark_dynamic_graph.cpp
Performance_RecomposeChurn.AllocationsAndTimePerCommit | split | 3 | two promises: each dirty commit recomposes exactly once and every region stays uniform; the allocation counting and the printed timings are a benchmark. The file replaces the process-wide operator new (a process-wide reach, Q8, for the benchmark part), so every test linked into the binary runs with the counter. Suite name Performance_ has no size, as above | tests/surface/api/graph/benchmark_dynamic_graph.cpp
```

### `tests/surface/api/graph/benchmark_graph.cpp`

```
Performance_ChainDispatch.PerNodeCostVsChainLength | split | 3 | two promises (the measurement is the benchmark): a chain of N dependent kernels settles to N and compiles once, checked inside measure_chain; the sweep itself states no promise, and its only check (ms per step > 0) cannot fail when the code is wrong. Suite name Performance_ has no size | tests/surface/api/graph/benchmark_graph.cpp
Performance_RootFanout.InjectionCostVsRootCount | split | 3 | two promises (the measurement is the benchmark): every independent root runs once per step, checked inside measure_fanout; the sweep's own check (ms per step > 0) cannot fail when the code is wrong. Suite name Performance_ has no size | tests/surface/api/graph/benchmark_graph.cpp
```

### `tests/surface/api/graph/plumbing/test_plan_cache.cpp`

```
Medium_PlanCache.CompilesOnlyWhenTheGraphChanges | rewrite | 1 | PlanCache: a plan is compiled once and again only when the graph changes; the RunRecorder spy (test_runner RecordingObserver) is forbidden; the rewrite needs the RunObserver in-memory adapter in modules/foundation/report, or drops the observer part | tests/surface/api/graph/plumbing/test_plan_cache.cpp; tests/surface/api/run_recorder.hpp; tests/surface/api/graph/plumbing/test_plan_view.cpp (grep: compile reuse, clean)
Medium_PlanCache.ARuleChangeDuringACompileRecompilesOnTheNextRun | resize | 8 | PlanCache: the plan keeps the generation of the rule set it compiled with, so a change during a compile recompiles the next run (a decision on generation numbers, small; the test makes a runtime in its body; seam: a rule generation value) | tests/surface/api/graph/plumbing/test_plan_cache.cpp; tests/surface/api/hooked_copy_task.hpp (named, not read)
Medium_PlanCache.Regression_TheCoordinatorSeesTheRulesTheCompileRead | rewrite | 1 | PlanCache: prepare() receives the rules the compile read; RuleRecordingCoordinator is a spy on OffloadCoordinator (a repository port) and the test reaches Internal::ContextAccess; the rewrite needs an in-memory OffloadCoordinator adapter in the task module | tests/surface/api/graph/plumbing/test_plan_cache.cpp
Medium_PlanCache.Regression_ADynamicGraphPreparesTheNodesOfEveryRebuiltRegion | rewrite | 1 | PlanCache: a rebuilt region is prepared again; NodeCountingCoordinator is a spy on OffloadCoordinator; the rewrite needs the in-memory OffloadCoordinator adapter | tests/surface/api/graph/plumbing/test_plan_cache.cpp
Medium_PlanCache.Regression_InstallingOrRemovingACoordinatorRecompiles | rewrite | 1 | PlanCache: installing or removing a coordinator makes the plan stale; DecliningCoordinator is a stub of OffloadCoordinator written by the test; the rewrite needs the in-memory declining OffloadCoordinator adapter | tests/surface/api/graph/plumbing/test_plan_cache.cpp
```

### `tests/surface/api/graph/plumbing/test_region_composer.cpp`

```
Medium_BoundaryAccess.AnEarlierWriterOrdersAReaderItsPredecessorHalfCovers | rewrite | 1 | BoundaryAccess: a reader of [15,20) is ordered after the earlier writer of that half (observable as the sum, total 5*1+5*0+5*7); the test reads the plan through Detail::DynamicGraphNodeEventsTestAccess and a dynamic_graph_plan_access.hpp internal header, and it installs a RunRecorder it never reads; the rewrite keeps the sum and drops the plan edges and the recorder | tests/surface/api/graph/plumbing/test_region_composer.cpp; tests/surface/api/dynamic_graph_plan_access.hpp (named, not read); tests/surface/api/run_recorder.hpp
Medium_BoundaryAccess.AReaderItsPredecessorCoversGainsNoEdge | rewrite | 1 | BoundaryAccess: a reader its predecessor fully covers gains no edge (the edge counts are plan internals, Q5); same Detail:: access and RunRecorder as the test above; the rewrite checks the sum that the ordering makes correct | tests/surface/api/graph/plumbing/test_region_composer.cpp; tests/surface/api/run_recorder.hpp; tests/surface/api/dynamic_graph_plan_access.hpp
Medium_BoundaryAccess.AnEarlierReaderOrdersAWriterBehindAReadingPredecessor | rewrite | 1 | BoundaryAccess: a writer behind a reading predecessor is ordered after the earlier reader (a plan edge, Q5 internal); Detail:: access and an unread RunRecorder; the rewrite checks the values early and late hold the old reads | tests/surface/api/graph/plumbing/test_region_composer.cpp; tests/surface/api/run_recorder.hpp; tests/surface/api/dynamic_graph_plan_access.hpp
Medium_RegionCommit.AThrowingRegionBuildKeepsTheEdgesOfTheOtherRegions | rewrite | 1 | RegionCommit: a commit whose region build throws composes nothing and leaves the other regions' results; the test reads plan edges through Detail::DynamicGraphNodeEventsTestAccess, installs a RunRecorder it never reads and uses RefusingFill, a test double of a task; the rewrite checks the retry and the values, and drops the edge reads | tests/surface/api/graph/plumbing/test_region_composer.cpp; tests/surface/api/refusing_fill.hpp (named, not read); tests/surface/api/run_recorder.hpp
Medium_RegionCommit.Regression_ARuleChangeDuringACommitComposesOnTheNextRun | resize | 8 | RegionCommit: a rule change made inside a commit makes the next run compose again (decided on the generation value, so small; the test makes a runtime in its body; seam: a rule generation value, as in PlanCache) | tests/surface/api/graph/plumbing/test_region_composer.cpp; tests/surface/api/hooked_copy_task.hpp (named, not read)
```

### `tests/surface/api/graph/plumbing/test_step_driver.cpp`

```
Medium_RunDriver.GraphReportsAThrowingHostTaskAsKernelFailed | merge | 4 | RunDriver: a throwing host task is KernelFailed from every run entry; the same promise as DynamicGraphReportsAThrowingHostTaskAsKernelFailed on the other graph type, so one parametrised row over Graph and DynamicGraph replaces both | tests/surface/api/graph/plumbing/test_step_driver.cpp
Medium_RunDriver.DynamicGraphReportsAThrowingHostTaskAsKernelFailed | merge | 4 | same promise as GraphReportsAThrowingHostTaskAsKernelFailed on DynamicGraph; the merged row replaces both | tests/surface/api/graph/plumbing/test_step_driver.cpp
Medium_RunDriver.AThrowingBuildIsCompileFailed | resize | 8 | RunDriver: a throwing plan build is CompileFailed from run and run_async (decided at build on the host, so small through a seam: a refusing copy in an in-memory task port; the test makes a runtime in its body) | tests/surface/api/graph/plumbing/test_step_driver.cpp; tests/surface/api/refusing_fill.hpp (named, not read)
Medium_RunDriver.EveryTryEntryRunsOnBothGraphs | resize | 8 | RunDriver: every run entry runs on both graph types and each step runs once (a count of steps decided on values, small through a seam; the test makes a runtime in its body) | tests/surface/api/graph/plumbing/test_step_driver.cpp
Medium_RunDriver.ARunWhileAStepIsPendingIsInvalidState | split | 3 | two promises: a sync and an async run are refused InvalidState while a step is pending; a second wait on a settled handle is InvalidState (the second is RunHandle's WaitingTwiceThrows promise, so one of the two stays) | tests/surface/api/graph/plumbing/test_step_driver.cpp
Medium_RunDriver.AddRunObserverReturnsAHandleThatRemovesIt | rewrite | 1 | RunDriver: a RunObserverHandle removes its observer on reset; RecordingObserver is a spy on RunObserver, and the test has no clean test of the promise; the rewrite needs the RunObserver in-memory adapter in modules/foundation/report | tests/surface/api/graph/plumbing/test_step_driver.cpp; modules/foundation/test_runner/include/SushiRuntime/test_runner/recording_observer.hpp (located, not read)
Medium_RunDriver.AnObserverRebuildsNothing | rewrite | 1 | RunDriver: adding or removing observers rebuilds no plan or composition; two RecordingObserver spies and a count check; the rewrite needs the RunObserver in-memory adapter, and the plan-count checks can stay with a clean test | tests/surface/api/graph/plumbing/test_step_driver.cpp
Medium_RunDriver.AnObserverOnADynamicGraphComposesNothingAndTimesItsNodes | rewrite | 1 | RunDriver: an observer added to a DynamicGraph composes nothing and times the very next run; RunRecorder is a spy on RunObserver and the timing is a device boundary; the rewrite needs the RunObserver in-memory adapter for the composition part, and the timing stays medium with the base fixture | tests/surface/api/graph/plumbing/test_step_driver.cpp; tests/surface/api/run_recorder.hpp
Medium_RunDriver.MovingAGraphWithAPendingStepEndsTheStep | split | 3 | two promises (the name joins them with And): a move waits for a pending step and ends the step the handle holds; the destination runs after the move with both sides freed; the debug-build branch is a GTEST_SKIP, reported as skipped | tests/surface/api/graph/plumbing/test_step_driver.cpp
Medium_RunDriver.OffloadedCountIsOfThisRun | rewrite | 1 | RunDriver: a report counts the dispatches a coordinator took in its own run; AcceptingCoordinator is a stub of OffloadCoordinator written by the test, and the test reaches Internal::ContextAccess; the rewrite needs an in-memory accepting OffloadCoordinator adapter in the task module | tests/surface/api/graph/plumbing/test_step_driver.cpp
Medium_RunDriver.AFailedChainedRunOfOneStepAdvancesNoState | rewrite | 5 | RunDriver: a failed chained run of one step advances no State; the test skips unless the counted step takes the Chain layer, so it asserts a layer, the inside (Q5), and a skip would hide a changed layer; the rewrite drops the layer guard and keeps the State check on the base fixture's device | tests/surface/api/graph/plumbing/test_step_driver.cpp
Medium_RunDriver.AFailedChainedRunOfSeveralStepsAdvancesAllButTheLast | rewrite | 5 | RunDriver: a failed run of three steps advances the State two times; the same layer guard and skip as the test above (Q5), and the count of advanced steps is an inside detail of the chain; the rewrite checks the State on the base fixture's device | tests/surface/api/graph/plumbing/test_step_driver.cpp
Medium_RunDriver.ABudgetPastTheClocksRangeRunsUntilCancelled | rewrite | 7 | RunDriver: a run_for budget the clock cannot hold runs until cancelled; two budget values pass through one helper, a loop over inputs in the body (Q7); the rewrite is a row table over the two budgets, on the base fixture's device | tests/surface/api/graph/plumbing/test_step_driver.cpp
Medium_RunDriver.AStepHookFeedsEveryReplayOfOnePlan | split | 3 | two promises: step hooks run before each replay, in the order added, and feed the replay; a hook does not rebuild the plan, and a new run counts its steps from zero | tests/surface/api/graph/plumbing/test_step_driver.cpp
Medium_RunDriver.AStepHookFeedsEveryReplayOfADynamicGraph | merge | 4 | same promise as AStepHookFeedsEveryReplayOfOnePlan on a DynamicGraph; merged into one row over the two graph types with that test's first part | tests/surface/api/graph/plumbing/test_step_driver.cpp
Medium_RunDriver.AStepHookMovesWithItsGraph | split | 3 | two promises: a moved graph keeps its step hooks; the source of a move has none | tests/surface/api/graph/plumbing/test_step_driver.cpp
Medium_RunDriver.AStepHookLargerThanTheInlineStorageRuns | resize | 8 | RunDriver: a step hook larger than the inline storage is held and called (the storage choice is a decision on sizes, small through the step machinery; the test makes a runtime in its body) | tests/surface/api/graph/plumbing/test_step_driver.cpp
Medium_RunDriver.AnErrorOnlyTheAsynchronousHandlerSeesFailsTheRunOnBothLayers | split | 3 | two promises (under the SYCL_IMPLEMENTATION_ONEAPI guard, so absent from other builds): an error only the asynchronous handler sees fails the run on both layers; the error does not outlive the run in the queue | tests/surface/api/graph/plumbing/test_step_driver.cpp
```

### `tests/surface/api/graph/test_access.cpp`

```
Medium_Access.RegionsKeepTheirOrder | split | 3 | two promises: Access keeps the order of the reads and writes it is given; bare regions carry no liveness observer (the header line no allocation is watched) | tests/surface/api/graph/test_access.cpp; modules/surface/api/include/SushiRuntime/api/graph/access.hpp
Medium_Access.HandlesBringTheirObservers | resize | 8 | Access: a Reads or Writes handle brings the liveness observers of its buffer (the test makes a runtime and two buffers in its body, which medium never allows; the seam is a buffer handle over an in-memory port) | tests/surface/api/graph/test_access.cpp; modules/surface/api/include/SushiRuntime/api/graph/access.hpp
```

### `tests/surface/api/graph/test_builder_surface.cpp`

```
Medium_BuilderSurface.CountLaunchWithCapturesPassesPointers | rewrite | 8 | BuilderSurface: a count launch hands the kernel the pointer of each capture; the boundary is a device kernel, so medium is right, but the test makes a runtime in its body (rewrite on the base fixture's device) | tests/surface/api/graph/plumbing/test_builder_surface.cpp
Medium_BuilderSurface.NdRangeOriginShiftsTheIDTheKernelReceives | rewrite | 8 | BuilderSurface: the kernel receives the origin-shifted id of an NdRange; the origin and count asserts repeat launch/test_nd_range.cpp (Q4 for those lines), the device kernel is the promise; the runtime is made in the body (rewrite on the base fixture's device) | tests/surface/api/graph/plumbing/test_builder_surface.cpp; tests/surface/api/launch/test_nd_range.cpp
Medium_BuilderSurface.StateRuleOverACountUpdatesAPrefix | rewrite | 8 | BuilderSurface: add_rule over a count updates the first count elements and no more; a device boundary, medium is right, the runtime is made in the body (rewrite on the base fixture's device) | tests/surface/api/graph/plumbing/test_builder_surface.cpp
Medium_BuilderSurface.ALaunchPastItsStateIsRefusedAtAdd | resize | 8 | BuilderSurface: a launch that reaches past its State is refused at add() and records nothing (a decision on the extent and the State size, so small; the test makes a runtime in its body; seam: the rule check on values) | tests/surface/api/graph/plumbing/test_builder_surface.cpp
Medium_BuilderSurface.CoupledRulesReadEveryStateNamed | split | 3 | two promises: a whole-field coupled rule reads the State it names; a launch-shaped coupled rule reads its prefix and the second State | tests/surface/api/graph/plumbing/test_builder_surface.cpp
Medium_BuilderSurface.ACoupledInputSmallerThanTheOutputIsRefusedAtAdd | resize | 8 | BuilderSurface: a coupled input smaller than its output is refused at add() and records nothing (a decision on counts, small; runtime in body; seam: the count check on values) | tests/surface/api/graph/plumbing/test_builder_surface.cpp
Medium_BuilderSurface.DynamicTakesItsCapacityBeforeItsAccess | rewrite | 8 | BuilderSurface: a Dynamic add takes its capacity before its access; the live count is a device boundary, medium is right, the runtime is made in the body (rewrite on the base fixture's device) | tests/surface/api/graph/plumbing/test_builder_surface.cpp
Medium_BuilderSurface.UntrackedKernelRunsOnTheDeviceOfTheBufferNamed | split | 3 | two promises: add_untracked with On(buffer) runs on the buffer's device; add_untracked with On(device) runs on that device | tests/surface/api/graph/plumbing/test_builder_surface.cpp
Medium_BuilderSurfaceDeathTest.PlacementOnADeviceTheRuntimeLacksStops | rewrite | 8 | BuilderSurface: both add_untracked forms stop on an On() naming a device the runtime lacks; the test forks a process (EXPECT_DEATH) and makes a runtime in its body; the rewrite keeps one death test per form on the base fixture | tests/surface/api/graph/plumbing/test_builder_surface.cpp
Medium_BuilderSurface.PlacementWatchesTheHandleItNames | resize | 8 | BuilderSurface: a graph watches the handle an On() names and reports DanglingResource after it is freed (decided by liveness observers, small; the test makes a runtime and buffers in its body; seam: a liveness observer on values) | tests/surface/api/graph/plumbing/test_builder_surface.cpp
Medium_BuilderSurface.ExtendRecordsOneNamedRule | split | 3 | two promises: extend calls its callable once and counts what it records as one rule with two nodes; the recorded kernels add their results and the extension is not replayed at a build | tests/surface/api/graph/plumbing/test_builder_surface.cpp
Medium_BuilderSurface.RefusalsNameTheCallTheUserWrote | rewrite | 7 | BuilderSurface: each add() overload's refusal names the call the user wrote; four calls of one promise in one body, a sign of several results of different calls (Q7); the rewrite is one parametrised row per overload, on a small seam | tests/surface/api/graph/plumbing/test_builder_surface.cpp
Medium_BuilderSurface.ARefusedAddIsKeptAndReturnedByTheNextRun | split | 3 | two promises: the first refusal is kept and a later one does not replace it; a run returns the refusal and runs nothing until clear() | tests/surface/api/graph/plumbing/test_builder_surface.cpp
```

### `tests/surface/api/graph/test_device_sized.cpp`

```
Medium_DeviceSizedLaunch.OnlyItemsBelowTheLiveWidthRun | split | 3 | two promises: a device-sized launch runs exactly the items below the live width, at every width; a live width change does not recompile the graph (the compile count check) | tests/surface/api/graph/test_device_sized.cpp
Medium_DeviceSizedLaunch.ANullLiveWidthRunsEveryItemAndTheArityPicksTheWords | split | 3 | two promises: a null live width runs every item; the kernel arity decides whether the live words are passed | tests/surface/api/graph/test_device_sized.cpp
Medium_DeviceSizedLaunch.ZeroCapacityLaunchRunsNothingAndReleasesItsSuccessor | split | 3 | two promises (the name joins them with And): a capacity of zero runs no item; the successor of an empty launch still runs | tests/surface/api/graph/test_device_sized.cpp
Medium_DeviceSizedLaunch.DeviceSizedRuleChainMatchesSchedulerAsTheCountsChange | rewrite | 5 | the test asserts which layer ran (Chain, Scheduler), a detail of the inside; the oracle is the scheduler run of the same library, not a host reference (Q6); the rewrite checks the digests against a host reference of the marking rule and drops the layer assertion; the runtimes are made in a helper, which medium never allows | tests/surface/api/graph/test_device_sized.cpp
Medium_DeviceSizedLaunch.SizedFromDeviceChainMatchesSchedulerAsTheCountChanges | rewrite | 5 | same defects as DeviceSizedRuleChainMatchesSchedulerAsTheCountsChange: layer assertions (the inside) and the scheduler as oracle; the rewrite uses a host reference of sized_from_device and the base fixture's device | tests/surface/api/graph/test_device_sized.cpp
Medium_DeviceSizedLaunch.DeviceCountOverCapacityFailsAChainedRun | split | 3 | two promises: a device count over its capacity fails the run; a later graph with a count inside the capacity runs clean on the same runtime (the second also asserts the Chain layer, a Q5 defect to drop) | tests/surface/api/graph/test_device_sized.cpp
```

### `tests/surface/api/graph/test_dispatch_events.cpp`

```
Medium_DispatchEvents.ABodyHandingOverTwoEventsIsTimedAcrossBoth | rewrite | 1 | DispatchEvents: a body that hands over two events is timed from the earliest start to the latest end; RecordingObserver is a spy on RunObserver; the rewrite needs the RunObserver in-memory adapter in modules/foundation/report; merges with the chain variant below | tests/surface/api/graph/test_dispatch_events.cpp; modules/foundation/test_runner/include/SushiRuntime/test_runner/recording_observer.hpp (located, not read)
Medium_DispatchEvents.ABodyHandingOverTwoEventsIsTimedAcrossBothOnTheChain | rewrite | 1 | same promise as ABodyHandingOverTwoEventsIsTimedAcrossBoth on the in-order chain; spy on RunObserver; merge into the row of the pool variant | tests/surface/api/graph/test_dispatch_events.cpp
Medium_DispatchEvents.ABodyHandingOverNoneIsTimedAsBefore | rewrite | 1 | DispatchEvents: a body that hands over no event is timed by the event it returns; spy on RunObserver; the rewrite needs the RunObserver in-memory adapter; merges with the chain variant below | tests/surface/api/graph/test_dispatch_events.cpp
Medium_DispatchEvents.ABodyHandingOverNoneIsTimedAsBeforeOnTheChain | rewrite | 1 | same promise on the chain; spy on RunObserver; merge into the row of ABodyHandingOverNoneIsTimedAsBefore | tests/surface/api/graph/test_dispatch_events.cpp
Medium_DispatchEvents.HandingAnEventOverOutsideABodyDoesNothing | rewrite | 1 | DispatchEvents: handing an event over outside a body does nothing; the sole check is EXPECT_NO_THROW, which sees no effect; the rewrite asserts an observable effect (no event in a later run record) | tests/surface/api/graph/test_dispatch_events.cpp; modules/surface/api/include/SushiRuntime/api/graph/dispatch_events.hpp (named, not read)
```

### `tests/surface/api/graph/test_dynamic_graph.cpp`

```
Medium_DynamicGraph.IndependentRegionsRunAndComposeOnce | split | 3 | two promises (the name joins them with And): two independent regions advance their own buffers over a multi-step run; an unmutated multi-step run composes once | tests/surface/api/graph/test_dynamic_graph.cpp
Medium_DynamicGraph.CrossRegionDependencyOrders | merge | 4 | same promise as ThreeRegionChainThreadsValue (a consumer region ordered after a producer region with a lower key), in one parametrised row with that test; the rewrite keeps two regions and three | tests/surface/api/graph/test_dynamic_graph.cpp
Medium_DynamicGraph.DropRegionStopsItsWork | resize | 8 | DynamicGraph: a dropped region stops and the surviving region keeps running (which regions run is decided on the region set, small through a seam; the test makes a runtime in its body) | tests/surface/api/graph/test_dynamic_graph.cpp
Medium_DynamicGraph.AddRegionBetweenSteps | resize | 8 | DynamicGraph: a region added between steps recomposes once and runs from the next run (a decision on the region set and the compose count, small through a seam; runtime in body) | tests/surface/api/graph/test_dynamic_graph.cpp
Medium_DynamicGraph.SingleRecomposePerMutation | split | 3 | two promises: building all regions composes once; one mutation recomposes exactly once (the values check of the 32 regions is a third outcome for the split) | tests/surface/api/graph/test_dynamic_graph.cpp
Medium_DynamicGraph.FindRegionReadsWithoutRecomposing | split | 3 | two promises: a lookup is not a mutation (no recompose); an absent key is not created by a lookup | tests/surface/api/graph/test_dynamic_graph.cpp
Medium_DynamicGraph.ThreeRegionChainThreadsValue | merge | 4 | same promise as CrossRegionDependencyOrders with a third region; merged into one row over the number of regions | tests/surface/api/graph/test_dynamic_graph.cpp
Medium_DynamicGraph.ReadReadSharingStaysConsistent | resize | 8 | DynamicGraph: two regions reading one producer's buffer both see its writes (ordering decided by dependencies, small through a plan; runtime in body) | tests/surface/api/graph/test_dynamic_graph.cpp
Medium_DynamicGraph.StreamInConsumerRewiresProducer | split | 3 | two promises: a consumer streamed in beside an existing producer sees its writes; that streaming recomposes once | tests/surface/api/graph/test_dynamic_graph.cpp
Medium_DynamicGraph.DropProducerLeavesConsumerRunnable | resize | 8 | DynamicGraph: dropping the producer leaves the consumer runnable with the values it had (a decision on the region set and the dependency, small through a seam; runtime in body); the comment says the exact value of y is not asserted, but the body asserts y == 2i, so the comment is stale | tests/surface/api/graph/test_dynamic_graph.cpp
Medium_DynamicGraph.DisjointRangeRegionsAreNotOrdered | rewrite | 8 | DynamicGraph: regions writing disjoint ranges of one buffer run concurrently; the test is a thread rendezvous with a 500 ms clock deadline, a thread boundary, so medium is right; the runtime is made in the body (rewrite on the base fixture's device); skipped below two hardware threads, reported as skipped | tests/surface/api/graph/test_dynamic_graph.cpp
Medium_DynamicGraph.OverlappingRangeRegionsStillSerialize | rewrite | 8 | DynamicGraph: regions writing overlapping ranges are ordered; a thread rendezvous with a 200 ms clock deadline, a thread boundary, medium right; runtime in body (rewrite on the base fixture's device) | tests/surface/api/graph/test_dynamic_graph.cpp
Medium_DynamicGraph.WholeBufferReaderOrdersAfterBothHalfWriters | resize | 8 | DynamicGraph: a whole-buffer reader runs after both half writers (the edge is decided on the regions' ranges, small through a plan; runtime in body) | tests/surface/api/graph/test_dynamic_graph.cpp
Medium_DynamicGraphNodeEvents.NotRequestedRecordsNothing | rewrite | 1 | DynamicGraph: a runtime with no observer records nothing; RunRecorder spy with stop(); the same promise as graph ProfilingNotRequestedLeavesNodeEventsEmpty and Observability ProfilingOffLeavesBreakdownEmpty, so the three merge into one rewritten test | tests/surface/api/graph/test_dynamic_graph.cpp; tests/surface/api/run_recorder.hpp
Medium_DynamicGraphNodeEvents.RequestedGraphProducesRecordsMatchingRenumberedIndex | rewrite | 1 | DynamicGraph: observed records carry the renumbered registration indices; RunRecorder spy and Detail::DynamicGraphNodeEventsTestAccess (Q1); the rewrite needs the RunObserver in-memory adapter and the public report | tests/surface/api/graph/test_dynamic_graph.cpp; tests/surface/api/dynamic_graph_plan_access.hpp (named, not read); tests/surface/api/run_recorder.hpp
Medium_DynamicGraphNodeEvents.IndexDenseAndOrderedByRegionThenAddOrder | rewrite | 1 | DynamicGraph: node_index is dense and ordered by region key then add order; RunRecorder spy and the Detail plan access (Q1); the rewrite needs the RunObserver in-memory adapter | tests/surface/api/graph/test_dynamic_graph.cpp; tests/surface/api/dynamic_graph_plan_access.hpp (named, not read); tests/surface/api/run_recorder.hpp
Medium_DynamicGraphNodeEvents.IndexStaysDenseAfterDropAndReAdd | rewrite | 1 | DynamicGraph: the index stays dense after a drop and a re-add; RunRecorder spy and the Detail plan access (Q1); the rewrite needs the RunObserver in-memory adapter | tests/surface/api/graph/test_dynamic_graph.cpp; tests/surface/api/dynamic_graph_plan_access.hpp (named, not read); tests/surface/api/run_recorder.hpp
Medium_DynamicGraphNodeEvents.AddingAnObserverRecomposesNothing | rewrite | 1 | DynamicGraph: adding an observer recomposes nothing; RunRecorder spy; the same promise as graph GraphProfilingSwitch.AddingAnObserverRecompilesNothingAndSwitches, a merge candidate; needs the RunObserver in-memory adapter | tests/surface/api/graph/test_dynamic_graph.cpp; tests/surface/api/run_recorder.hpp
Medium_LayerEquivalence.DynamicRegionsMatchOneStaticGraph | rewrite | 5 | DynamicGraph vs static Graph: four regions leave the bytes one static graph leaves; the test asserts the Chain and Scheduler layers (the inside, Q5), and its oracle is a static run of the same library (Q6); the rewrite drops the layer asserts and compares with a host reference | tests/surface/api/graph/test_dynamic_graph.cpp
Medium_LayerEquivalence.DroppedAndReaddedRegionMatchesStaticGraphs | rewrite | 5 | DynamicGraph: a region dropped and recorded again matches static graphs per stretch; layer asserts are the inside (Q5); the rewrite keeps the bytes against a host reference | tests/surface/api/graph/test_dynamic_graph.cpp
Medium_LayerEquivalence.HostTasksAmongKernelsMatchAcrossLayers | rewrite | 5 | host tasks between kernels: the same bytes on both layers; layer asserts are the inside (Q5) and the oracle is the same library on the other layer (Q6); the rewrite compares against a host reference | tests/surface/api/graph/test_dynamic_graph.cpp
Medium_LayerEquivalence.AsyncStepsMatchBlockingRun | rewrite | 5 | run_async steps leave the bytes run(N) leaves; layer asserts are the inside (Q5); the blocking run is the oracle, the code under test (Q6); the rewrite uses a host reference | tests/surface/api/graph/test_dynamic_graph.cpp
Medium_LayerEquivalence.AsyncStepsOfADynamicGraphMatchBlockingRun | rewrite | 6 | DynamicGraph run_async steps match its blocking run; the oracle is the blocking run of the same graph, the code under test (Q6); the rewrite names a host reference of the workload as the oracle | tests/surface/api/graph/test_dynamic_graph.cpp
Medium_LayerEquivalence.OffsetLaunchFormsMatchAcrossLayers | rewrite | 5 | offset 2-D launch forms (Extent, Extent.at, In/InOut, NdRange, State over Extent) match a host reference; the layer asserts are the inside (Q5); the rewrite keeps the host-reference comparison, which already exists in this test | tests/surface/api/graph/test_dynamic_graph.cpp
Medium_LayerEquivalence.UntrackedOffsetLaunchesMatchAcrossLayers | rewrite | 5 | untracked Extent and NdRange launches match the host reference; the scheduler layer assertion is the inside (Q5); the rewrite keeps the host-reference comparison | tests/surface/api/graph/test_dynamic_graph.cpp
Medium_LayerEquivalence.PlacedUntrackedLaunchesMatchAcrossLayers | rewrite | 5 | untracked kernels placed with On() match the host reference; the scheduler layer assertion is the inside (Q5); the rewrite keeps the host-reference comparison | tests/surface/api/graph/test_dynamic_graph.cpp
Medium_DynamicGraphPlacement.Regression_ARuleChangeReplacesARegionsNodeWithoutARecord | rewrite | 1 | DynamicGraph: a rule change between two runs re-places a region's node without a re-record; reaches Internal::ContextAccess (context_access header, Q1) and the device queue set; the test also holds four promises (refusal, remap, unchanged composes nothing, remap to device one) that the rewrite splits | tests/surface/api/graph/test_dynamic_graph.cpp
```

### `tests/surface/api/graph/test_graph.cpp`

```
Medium_NativeCPU.HostTaskRunsAfterKernel | rewrite | 8 | Graph: a host task ordered after a kernel sees the kernel's output (a device-and-thread boundary, medium is right; the test makes a runtime in its body, so the rewrite uses the base fixture's device) | tests/surface/api/graph/test_graph.cpp
Medium_NativeCPU.KernelRunsAfterHostTask | rewrite | 8 | Graph: a kernel ordered after a host task sees the host's writes (boundary; runtime in body; rewrite on the base fixture's device) | tests/surface/api/graph/test_graph.cpp
Medium_NativeCPU.UntrackedHostTaskRunsEveryStep | rewrite | 8 | Graph: an untracked host task runs once per step (boundary; runtime in body; rewrite on the base fixture's device) | tests/surface/api/graph/test_graph.cpp
Medium_NativeCPU.ThrowingHostTaskKeepsRuntimeUsable | split | 3 | two promises: a throwing host task fails its run; the pool stays re-armable for a later graph on the same runtime | tests/surface/api/graph/test_graph.cpp
Medium_NodeEventRecords.ProfilingNotRequestedLeavesNodeEventsEmpty | rewrite | 1 | Graph: a runtime with no observer hands out no node event records; RunRecorder is a spy on RunObserver (stop() is called before the run); the same promise as Observability.ProfilingOffLeavesBreakdownEmpty, so the rewrite merges the two; needs the RunObserver in-memory adapter | tests/surface/api/graph/test_graph.cpp; tests/surface/api/run_recorder.hpp
Medium_NodeEventRecords.RequestedGraphRecordsOnePerDispatch | rewrite | 1 | Graph: one record per dispatch, submit <= start <= end; the records come from RunRecorder, a spy on RunObserver; the rewrite needs the RunObserver in-memory adapter for the record count and keeps the device timing check medium | tests/surface/api/graph/test_graph.cpp; tests/surface/api/run_recorder.hpp
Medium_NodeEventRecords.NodeIndexIsRegistrationOrder | rewrite | 1 | Graph: node_index follows registration order; RunRecorder spy plus Detail::NodeEventRecordsTestAccess (a Detail symbol, Q1); the rewrite needs the RunObserver in-memory adapter and drops the Detail access | tests/surface/api/graph/test_graph.cpp; tests/surface/api/run_recorder.hpp
Medium_NodeEventRecords.HostNodeLandsAfterItsKernelOnTheSteadyClock | rewrite | 1 | Graph: a host record lands after its kernel on the steady clock; RunRecorder spy and its pairing helper, a test-side clock conversion (a process-wide reach through the observer); the rewrite needs the RunObserver in-memory adapter | tests/surface/api/graph/test_graph.cpp; tests/surface/api/run_recorder.hpp
Medium_RunReportReuse.ReuseReportsWhatTheValueFormReports | rewrite | 8 | Graph: the caller-owned report overload fills what the value form fills; a device run, boundary, medium right; runtime made in the body (rewrite on the base fixture's device) | tests/surface/api/graph/test_graph.cpp
Medium_RunReportReuse.SteadyStateRunsDoNotReallocateTheReport | rewrite | 5 | Graph: a steady-state run refills the report in place; the test asserts the data pointer and capacity of the report's vector, the inside (Q5); the rewrite keeps the one-row-per-run count, which is the public promise | tests/surface/api/graph/test_graph.cpp
Medium_RunReportReuse.EachRunOverwritesTheLastOne | rewrite | 8 | Graph: a later run overwrites the report of an earlier one (boundary; runtime in body; rewrite on the base fixture's device) | tests/surface/api/graph/test_graph.cpp
Medium_RunReportReuse.ARejectedRunLeavesNoStaleNumbers | rewrite | 8 | Graph: a rejected run clears the caller's report and marks it unsuccessful (the refusal is host logic, so resize is also open; the test makes a runtime and runs a device graph, so rewrite here, with a seam as the alternative) | tests/surface/api/graph/test_graph.cpp
Medium_RunReportReuse.TryRunReturnsTheCallersReport | rewrite | 8 | Graph: run(RunReport&) returns the report it was given (runtime in body; rewrite on the base fixture's device) | tests/surface/api/graph/test_graph.cpp
Medium_RunReportReuse.AsyncWaitFillsTheCallersReport | rewrite | 8 | RunHandle: wait(RunReport&) fills the caller's report across ticks (a thread boundary, medium right; runtime in body; rewrite on the base fixture's device) | tests/surface/api/graph/test_graph.cpp
Medium_RunReportReuse.DynamicGraphRunsIntoACallersReport | rewrite | 5 | DynamicGraph: a run into a caller's report keeps the vector's data pointer (Q5, the inside) and one row; the rewrite keeps the row count and the values, on the base fixture's device | tests/surface/api/graph/test_graph.cpp
Medium_Cancellation.CancelStopsRunUntilFromAnotherThread | rewrite | 8 | Graph: cancel from another thread stops run_until promptly (a thread boundary, medium right; runtime in body; the test uses a 30 s progress deadline and a 5 s cancel deadline; rewrite on the base fixture's device) | tests/surface/api/graph/test_graph.cpp
Medium_Cancellation.NextRunIsNotCancelledAfterReset | rewrite | 8 | Graph: a cancellation does not carry into the next run (thread boundary; runtime in body; rewrite on the base fixture's device) | tests/surface/api/graph/test_graph.cpp
Medium_ChainLayer.ChainMatchesForcedSchedulerBitForBit | rewrite | 5 | Graph: the chain layer and the scheduler give the same bytes; the test asserts which layer ran (Chain, Scheduler), the inside (Q5), and its oracle is the scheduler run of the same library, the code under test (Q6); the rewrite drops the layer assertions and checks the bytes against a host reference | tests/surface/api/graph/test_graph.cpp
Medium_ChainLayer.NonChainablePlanMatchesChainBitForBit | rewrite | 5 | Graph: a native host task changes no output byte; the layer assertions are the inside (Q5) and the chained run is the oracle (Q6); the rewrite keeps the byte comparison against a host reference | tests/surface/api/graph/test_graph.cpp
Medium_ChainLayer.ANamedNodeRunsOnTheChainWithNoClusterAttached | rewrite | 5 | Graph: a named node with no cluster attached gives the right values; the layer assertion is the inside (Q5); the rewrite keeps the values and drops the Chain expectation | tests/surface/api/graph/test_graph.cpp
Medium_ChainLayer.RunUntilSeesEveryStepsResult | rewrite | 5 | Graph: run_until stops on each step's device result; the Chain layer assertion is the inside (Q5); the rewrite keeps the step count and the counter | tests/surface/api/graph/test_graph.cpp
Medium_ChainLayer.ProfiledChainRecordsEveryDispatch | rewrite | 1 | Graph: a profiled chain records each dispatch once per step and per node; RunRecorder is a spy on RunObserver; the rewrite needs the RunObserver in-memory adapter and drops the layer assertions (Q5) | tests/surface/api/graph/test_graph.cpp; tests/surface/api/run_recorder.hpp
Medium_CompletionLatch.ManyStepReplayReachesCorrectState | rewrite | 8 | Graph: 5000 replayed steps reach the right state (boundary, long; runtime in body; rewrite on the base fixture's device) | tests/surface/api/graph/test_graph.cpp
Medium_CompletionLatch.ReRunsCleanlyOnTheSamePlan | rewrite | 8 | Graph: three runs on one plan accumulate exactly (boundary; runtime in body; rewrite on the base fixture's device) | tests/surface/api/graph/test_graph.cpp
Medium_CompletionLatch.SingleStepRunsRepeatedlyWithoutLeak | rewrite | 3 | Graph: 200 single-step runs each complete with the right values; the name promises no leak, which the body cannot observe; the rewrite renames it to what is checked and moves the leak check to the sanitizer runs | tests/surface/api/graph/test_graph.cpp
Medium_CrossContextGuard.NodeSpanningTwoContextsFailsCleanly | split | 3 | two promises: a node that reads one device context and writes another fails the run with InvalidGraph; the runtime stays usable for a co-located graph afterwards (skipped below two devices, reported as skipped) | tests/surface/api/graph/test_graph.cpp
Medium_Determinism.TheFloatingPointContractReachesConsumerCode | delete | 1 | a test of trivial code (a constant, a macro of the test translation unit, Q1's forbidden kind); it checks the build flags of the test target, not a promise of the public interface; OutputIsByteEqualAcrossWorkerCounts and RepeatedConfigurationsAgreeByteForByte test the determinism promise | tests/surface/api/graph/test_graph.cpp
Medium_Determinism.HostArithmeticIsNotReassociated | keep | 9 | the test checks the toolchain's floating-point behaviour, not a promise of the repository's interface; by the procedure it is kept (literal oracles, host only, small), and the owner may decide whether a toolchain probe belongs elsewhere | tests/surface/api/graph/test_graph.cpp
Medium_Determinism.OutputIsByteEqualAcrossWorkerCounts | rewrite | 8 | Graph: the output bytes do not depend on the worker count (a pool boundary, medium right); the worker count is fixed at runtime creation, so the rewrite needs one runtime per count, which medium forbids in a body: an owner decision (the rule of one device per process) | tests/surface/api/graph/test_graph.cpp
Medium_Determinism.RepeatedConfigurationsAgreeByteForByte | rewrite | 8 | Graph: two runs of one configuration agree byte for byte; runtimes made in the helper (run_workload) and the body; rewrite on the base fixture's device | tests/surface/api/graph/test_graph.cpp
Medium_Determinism.TheRebalancerDoesNotChangeTheBytes | rewrite | 8 | Graph: the background rebalancer changes no output byte; the rebalancer is set at runtime creation, so the rewrite needs an owner decision as for the worker-count test | tests/surface/api/graph/test_graph.cpp
Medium_Determinism.ReplayDoesNotDriftAcrossSteps | split | 3 | two promises: a replayed rule's result after many steps equals its result after one; a replay does not recompile | tests/surface/api/graph/test_graph.cpp
Medium_GraphProfilingSwitch.TwoGraphsBothRecordWhileAnObserverIsAdded | rewrite | 1 | Graph: an observer added to a runtime turns timing on for every graph of it; RunRecorder spy and Internal::ContextAccess reach (a process-wide instance, Q8 as well); the rewrite needs the RunObserver in-memory adapter and a seam for the profiling-queue state | tests/surface/api/graph/test_graph.cpp; tests/surface/api/run_recorder.hpp
Medium_GraphProfilingSwitch.AddingAnObserverRecompilesNothingAndSwitches | rewrite | 1 | Graph: adding or removing an observer recompiles nothing; RunRecorder spy; the compile-count part can stay in a clean test; the rewrite needs the RunObserver in-memory adapter | tests/surface/api/graph/test_graph.cpp; tests/surface/api/run_recorder.hpp
Medium_GraphProfilingSwitch.NUMAFallbackBranchUsesProfilingQueue | rewrite | 1 | Graph: a node without declared Reads or Writes still dispatches on a profiling queue; the name names an internal branch (Q5), RunRecorder spy and Internal::ContextAccess reach; the rewrite keeps the public promise (node events recorded) and needs the RunObserver in-memory adapter | tests/surface/api/graph/test_graph.cpp; tests/surface/api/run_recorder.hpp
Medium_Observability.ProfilingOffLeavesBreakdownEmpty | rewrite | 1 | Graph: with no observer the run has no breakdown; RunRecorder spy with stop(); the same promise as NodeEventRecords.ProfilingNotRequestedLeavesNodeEventsEmpty, so the two merge into one rewritten test | tests/surface/api/graph/test_graph.cpp; tests/surface/api/run_recorder.hpp
Medium_Observability.ProfilingOnCapturesPerNodeTiming | rewrite | 1 | Graph: each kernel node is dispatched once per step with ordered timing; RunRecorder spy; the rewrite needs the RunObserver in-memory adapter | tests/surface/api/graph/test_graph.cpp; tests/surface/api/run_recorder.hpp
Medium_Observability.NamingTheLastNodeReachesTheNodeTable | rewrite | 1 | Graph: a name given through name() reaches the node table; RunRecorder spy; the rewrite needs the RunObserver in-memory adapter | tests/surface/api/graph/test_graph.cpp; tests/surface/api/run_recorder.hpp
Medium_Observability.EveryAddShapeCanBeNamed | rewrite | 1 | Graph: each of five add() shapes can be named (a loop over shapes in one body, Q7, so the rewrite is one row per shape); RunRecorder spy; the rewrite needs the RunObserver in-memory adapter | tests/surface/api/graph/test_graph.cpp; tests/surface/api/run_recorder.hpp
Medium_Observability.UnnamedNodeStillReportsTheDefaultLabel | rewrite | 1 | Graph: an unnamed node reports the documented default label; RunRecorder spy; the rewrite needs the RunObserver in-memory adapter | tests/surface/api/graph/test_graph.cpp; tests/surface/api/run_recorder.hpp
Medium_Observability.ProfilingOnPopulatesWorkerSplit | rewrite | 1 | Graph: a profiled scheduler run has one event per dispatch; RunRecorder spy; the name says worker split, which the body never checks (the test names a promise it does not test), so the rewrite renames it or adds the worker-split check; needs the RunObserver in-memory adapter | tests/surface/api/graph/test_graph.cpp; tests/surface/api/run_recorder.hpp
Medium_Observability.ExplicitTransfersAreAccounted | rewrite | 8 | Graph: explicit host-to-device and device-to-host copies are counted per direction (a device transfer, medium right; runtime in body; rewrite on the base fixture's device; the two directions are two promises for the rewrite to split) | tests/surface/api/graph/test_graph.cpp
Medium_Observability.TransferBytesAttributedToTheRunWindow | rewrite | 8 | Graph: bytes a host node moves during a run land in the runtime's counters for that run (boundary; runtime in body; rewrite on the base fixture's device) | tests/surface/api/graph/test_graph.cpp
```

### `tests/surface/api/graph/test_node_options.cpp`

```
Medium_NodeOptionsWidth.EachNodeOfARuleReportsItsStatedWidth | rewrite | 1 | Graph: each node of a rule reports the width it stated, not a buffer's size; RunRecorder spy; the rewrite needs the RunObserver in-memory adapter; no clean test of the same promise exists | tests/surface/api/graph/test_node_options.cpp; tests/surface/api/run_recorder.hpp; tests/surface/api/graph/test_recorder.cpp (grep: launched_width checks, spies)
Medium_NodeOptionsWidth.ResolverAndDeviceSizedNodeReportTheirStatedWidths | rewrite | 1 | Graph: a resolver and a device-sized node report the widths they stated; RunRecorder spy; the rewrite needs the RunObserver in-memory adapter | tests/surface/api/graph/test_node_options.cpp; tests/surface/api/run_recorder.hpp
Medium_NodeOptionsWidth.ANodeReportsTheNameAndTheOperationItStated | rewrite | 1 | Graph: an observed run reports the name and operation a node stated; RunRecorder spy; the rewrite needs the RunObserver in-memory adapter; the name is also checked by spies in test_graph.cpp Observability (merge candidates) | tests/surface/api/graph/test_node_options.cpp; tests/surface/api/run_recorder.hpp
Medium_NodeOptionsWidth.ANodeNameReplacesTheNameOfItsRule | rewrite | 1 | Graph: a node's own name replaces its rule's name in the table; RunRecorder spy; the rewrite needs the RunObserver in-memory adapter | tests/surface/api/graph/test_node_options.cpp; tests/surface/api/run_recorder.hpp; tests/surface/api/graph/README.md (grep only, line 246)
Medium_NodeOptionsWidth.MoreParametersThanANodeCarriesAreRefused | delete | 4 | Graph: more parameters than a node carries are refused with InvalidGraph and nothing is recorded; the parameter-limit promise is also held by detail/test_node_stamp.cpp::OneParameterPastTheLimitIsRefused, which is the unit's own module test and stays under the Q4 ruling; the graph-level "records nothing" check is added as an Adds row. The medium test makes a runtime in its body | tests/surface/api/graph/test_node_options.cpp; tests/surface/api/detail/test_node_stamp.cpp
```

### `tests/surface/api/graph/test_plan_view.cpp`

```
Medium_PlanView.CompileThenRunCompilesOnce | split | 3 | three promises at three points: compile() runs nothing; a current plan is not built again; run() uses the plan compile() built | tests/surface/api/graph/test_plan_view.cpp
Medium_PlanView.CompileReturnsTheRecordersRefusal | rewrite | 6 | Graph::compile and plan return the refusal the recorder kept; the oracle is graph.refusal() itself, the code under test, so the expected message must be named from the contract (rule 0, the empty State) | tests/surface/api/graph/test_plan_view.cpp
Medium_PlanView.CompileOfAnEmptyGraphIsRefused | resize | 8 | Graph::compile on an empty graph is InvalidGraph (a decision on the recorded count, so small; the test makes a runtime in its body, which medium never allows; seam: a recorder without a device) | tests/surface/api/graph/test_plan_view.cpp
Medium_PlanView.ADynamicGraphCompiledThenRunComposesOnce | split | 3 | two promises: compile() on a dynamic graph runs nothing; compile then run composes once | tests/surface/api/graph/test_plan_view.cpp
Medium_PlanView.AChainOfThreeKernelsReportsItsPredecessorsAndRegions | split | 3 | two promises: PlanNode predecessor counts and regions per node; PlanNode name, operation, placement and determinism carried from the options | tests/surface/api/graph/test_plan_view.cpp
Medium_PlanView.AViewOutlivesItsGraph | resize | 8 | PlanView keeps its name and region bases after the graph is destroyed (ownership decided on values; the test makes a runtime, which medium never allows; seam: a plan built without a device) | tests/surface/api/graph/test_plan_view.cpp
```

### `tests/surface/api/graph/test_recorder.cpp`

```
Medium_RecorderSemantics.ReadingARegionBetweenRunsDoesNotRecompose | resize | 8 | Recorder: reading a region between runs does not rebuild it (a decision on the compose count, small through a seam; the test makes a runtime in its body) | tests/surface/api/graph/test_recorder.cpp
Medium_RecorderSemantics.ARecordingThroughAHeldRegionRecomposes | resize | 8 | Recorder: a recording made through a held region rebuilds that region (compose count and the second rule's values; the decision is on generations, small through a seam) | tests/surface/api/graph/test_recorder.cpp
Medium_RecorderSemantics.ClearingARegionReplacesItsWork | resize | 8 | Recorder: clearing a region replaces its rules on the next build (a decision on recorded rules, small through a seam; runtime in body) | tests/surface/api/graph/test_recorder.cpp
Medium_RecorderSemantics.ADropBetweenRunsRecomposes | resize | 8 | DynamicGraph: a drop between runs recomposes and the dropped region does not run (a decision on the region set, small through a seam; runtime in body) | tests/surface/api/graph/test_recorder.cpp
Medium_RecorderSemantics.AThreeNodeRuleIsOneRuleWithOneName | rewrite | 1 | Recorder: three nodes of one rule are one rule with one name and one table row; RunRecorder spy; the rewrite needs the RunObserver in-memory adapter (the rule and registration checks are host logic and can stay small) | tests/surface/api/graph/test_recorder.cpp; tests/surface/api/run_recorder.hpp
Medium_RecorderSemantics.ARegionRuleReportsItsRegistrationAndName | rewrite | 1 | DynamicGraph region: a rule reports its registration and name; RunRecorder spy and Recorder registration reads (Q1 for the spy); the rewrite needs the RunObserver in-memory adapter | tests/surface/api/graph/test_recorder.cpp; tests/surface/api/run_recorder.hpp
Medium_RecorderSemantics.NameLastOwnsItsText | rewrite | 1 | Recorder: name() copies the text and the last name wins; RunRecorder spy; the rewrite needs the RunObserver in-memory adapter (the text-ownership check can move to a small test of the name store) | tests/surface/api/graph/test_recorder.cpp; tests/surface/api/run_recorder.hpp
Medium_RecorderSemantics.ANullOrEmptyNameLeavesTheDefault | rewrite | 1 | Recorder: a null name leaves the default name; RunRecorder spy; the rewrite needs the RunObserver in-memory adapter | tests/surface/api/graph/test_recorder.cpp; tests/surface/api/run_recorder.hpp
Medium_RecorderSemantics.MovedFromGraphIsEmpty | split | 3 | three promises: a moved-from graph is empty and refuses a run; the plan moves with the taken graph; a moved-from graph records and runs again | tests/surface/api/graph/test_recorder.cpp
Medium_RecorderSemantics.MovedFromDynamicGraphHasNoRegions | split | 3 | three promises: a moved-from dynamic graph has no regions and refuses a run; the composed plan moves with the taken graph; a moved-from graph takes regions and runs again | tests/surface/api/graph/test_recorder.cpp
Medium_RecorderSemantics.OneObserverPerAllocation | rewrite | 5 | Recorder: one observer serves every node over one buffer; the test reads watched_allocation_count, an inside detail (Q5); the rewrite keeps the output check (the sum of sixteen increments) on the base fixture's device | tests/surface/api/graph/test_recorder.cpp
Medium_RecorderSemantics.ADroppedBufferIsStillDetected | rewrite | 5 | Recorder: a buffer freed after recording makes the next run DanglingResource; the test also asserts watched_allocation_count (an inside detail, Q5); the rewrite keeps the DanglingResource check on the base fixture's device | tests/surface/api/graph/test_recorder.cpp
Medium_RecorderSemantics.ADroppedBufferIsStillDetectedInARegion | resize | 8 | DynamicGraph region: a freed buffer in a region is DanglingResource on the next run (liveness decided on values, small through a seam; runtime in body) | tests/surface/api/graph/test_recorder.cpp
Medium_RecorderSemantics.ARecorderWhoseGraphMovedNeverRepeatsAGeneration | split | 3 | three promises: the moved work keeps its generation; a generation the plan was built at never names a new rule; a move assignment does not step the generation back (the generation values are inside numbering, Q5, for the split parts) | tests/surface/api/graph/test_recorder.cpp
Medium_RecorderSemantics.GraphClearLeavesAGraphThatRecordsAndRunsAgain | split | 3 | three promises: clear forgets the rules and their registration; a cleared graph refuses an empty run; the cleared rule does not run again and a new rule does | tests/surface/api/graph/test_recorder.cpp
Medium_RecorderSemantics.Regression_GraphClearFreesWhatItsCompiledPlanCaptured | resize | 8 | Graph: clear frees what the compiled plan's task bodies captured (object lifetime decided on values, small through a seam; runtime in body) | tests/surface/api/graph/test_recorder.cpp
Medium_RecorderSemantics.AClearInsideAnExtensionStartsANewRule | resize | 8 | Recorder: a clear inside an extension leaves one new rule that runs (a decision on recorded rules, small through a seam; runtime and worker thread in body) | tests/surface/api/graph/test_recorder.cpp
Medium_RecorderSemantics.ATaskOfTwoCommandGroupsIsOrderedByItsAccess | rewrite | 8 | Recorder: a task of two command groups runs after its producer and before its reader (a device ordering boundary, medium right; runtime in body; rewrite on the base fixture's device) | tests/surface/api/graph/test_recorder.cpp
Medium_RecorderSemantics.ANamedRuleTakesAtMostEightParameters | rewrite | 7 | Recorder: the parameter limit of eight is refused through add() and add_rule() and eight are kept; the same limit through two entry points is a loop of inputs in the body, so the rewrite is rows over the two entries | tests/surface/api/graph/test_recorder.cpp
Medium_RecorderSemantics.AnExtensionThatRecordsNothingAddsNoRule | rewrite | 1 | Recorder: an extension that records nothing adds no rule and keeps the first rule's name; RunRecorder spy (and the generation read is an inside number, Q5); the rewrite needs the RunObserver in-memory adapter | tests/surface/api/graph/test_recorder.cpp; tests/surface/api/run_recorder.hpp
Medium_RecorderScratch.AConsumerSeesWhatPassedThroughTheScratch | rewrite | 8 | Recorder: a consumer of a rule's output sees what passed through its scratch (a device boundary, medium right; runtime in body; rewrite on the base fixture's device) | tests/surface/api/graph/test_recorder.cpp
Medium_RecorderScratch.EachRunReadsItsInputAsItStands | split | 3 | two promises: each run of a replayed rule reads its input as it stands; the replayed rule compiles once | tests/surface/api/graph/test_recorder.cpp
Medium_RecorderScratch.ScratchSurvivesARecompile | rewrite | 5 | Recorder: scratch a kernel captured stays valid after a recompile; the compile-count assertions are plan-internal (Q5); the rewrite keeps the two-plan value checks on the base fixture's device | tests/surface/api/graph/test_recorder.cpp
Medium_RecorderScratch.ScratchIsFreedWithTheRecorder | rewrite | 5 | Recorder: scratch returns to the runtime when the graph dies; the test reads the allocator's live_count through Internal::ContextAccess and memory/usm_allocator.hpp (inside, Q5); the rewrite needs an observable effect of the free, or an allocator seam in the memory module | tests/surface/api/graph/test_recorder.cpp; tests/surface/api/run_recorder.hpp (not used here)
Medium_RecorderScratch.RegionsRecordRulesWithScratch | split | 3 | two promises: region rules passing values through scratch are right across rounds; renaming a region's rule rebuilds that region once (compose count and node count) | tests/surface/api/graph/test_recorder.cpp
Medium_RecorderScratch.AScratchRequestNoDeviceCanSatisfyReturnsAllocationFailed | split | 3 | two promises: a scratch request no device can hold returns AllocationFailed; a small request on the same recorder is granted (the granted part is a second promise) | tests/surface/api/graph/test_recorder.cpp
Medium_RegistrationIndex.NodeIndexIsRegistrationOrder | rewrite | 1 | Graph: a node's registration index follows call order, not compiled order; the test uses the internal TaskGraph and its compile() (a private header, graph/task_graph.hpp, Q1); the public premise is the node index in a report, which RuleRegistration tests cover only through a spy; the rewrite goes through the public Graph | tests/surface/api/graph/test_recorder.cpp
Medium_RegistrationIndex.ReservedIndexSharedAcrossCallsGroupsNodes | rewrite | 1 | TaskGraph: nodes added with a reserved index share one registration index (internal TaskGraph, private header, Q1); the rule-scope promise is the public one; the rewrite is the Recorder-level rule scope test | tests/surface/api/graph/test_recorder.cpp
Medium_RegistrationIndex.RuleScopeGroupsItsNodesUnderOneIndex | rewrite | 1 | Recorder rule scope: the nodes of one scope share one registration index; reaches Detail::RegistrationIndexTestAccess (a Detail symbol); merges with RuleScopeGroupsResolverAndDeviceSizedNodesUnderOneIndex (same promise, other node kinds); the rewrite drops the Detail access and reads the index through the public report | tests/surface/api/graph/test_recorder.cpp
Medium_RegistrationIndex.RuleScopeGroupSurvivesRebuild | rewrite | 1 | Recorder rule scope: the group survives a rebuild and the indices stay dense; Detail::RegistrationIndexTestAccess (Detail symbol); the rewrite reads the indices through the public report | tests/surface/api/graph/test_recorder.cpp
Medium_RegistrationIndex.RuleScopeGroupsResolverAndDeviceSizedNodesUnderOneIndex | rewrite | 1 | Recorder rule scope with a resolver and a device-sized kernel: one index; Detail::RegistrationIndexTestAccess; merges with RuleScopeGroupsItsNodesUnderOneIndex into one row over node kinds | tests/surface/api/graph/test_recorder.cpp
Medium_RuleRegistration.ARuleCarriesNoIndexUntilTheGraphCompiles | resize | 8 | Graph: a rule reports no registration index before the first run (a decision on recorded rules, small through a seam; the test makes a runtime in its body) | tests/surface/api/graph/test_recorder.cpp
Medium_RuleRegistration.ARuleScopeTakesOneIndexOfItsOwn | rewrite | 1 | Graph: a plain add takes an index of its own, a rule scope takes one shared index, and its nodes report it; RunRecorder spy; the rewrite needs the RunObserver in-memory adapter | tests/surface/api/graph/test_recorder.cpp; tests/surface/api/run_recorder.hpp
Medium_RuleRegistration.EveryNodeEventNamesARecordedRule | rewrite | 1 | Graph: every node event names a rule the caller recorded; RunRecorder spy; the rewrite needs the RunObserver in-memory adapter | tests/surface/api/graph/test_recorder.cpp; tests/surface/api/run_recorder.hpp
Medium_RuleRegistration.ARulesNodesReportOneIndexOnEveryStep | rewrite | 1 | Graph: a rule's nodes report one index on every step; RunRecorder spy; the rewrite needs the RunObserver in-memory adapter | tests/surface/api/graph/test_recorder.cpp; tests/surface/api/run_recorder.hpp
Medium_RuleRegistration.AVerbReturnsThePositionOfTheRuleItRecorded | rewrite | 1 | Recorder: the position a verb returns is the one rule_registration takes; RunRecorder spy finds nodes by launched width; the rewrite needs the RunObserver in-memory adapter | tests/surface/api/graph/test_recorder.cpp; tests/surface/api/run_recorder.hpp
Medium_RecorderPlacementScope.NestedScopesStampTheInnermostClassAndKey | split | 3 | three promises: the innermost open placement scope stamps its class and key; a node's own class and key beat the scope's; closing a scope restores the outer one | tests/surface/api/graph/test_recorder.cpp
Medium_RecorderPlacementScope.WithinReturnsWhatTheCallableReturns | resize | 8 | Recorder::within returns the callable's result (a decision on values, small through a seam; the test makes a runtime in its body) | tests/surface/api/graph/test_recorder.cpp
Medium_RecorderPlacementScope.AThrowingCallableClosesItsScope | resize | 8 | Recorder::within closes its scope when the callable throws (a decision on the scope stack, small through a seam; the test makes a runtime in its body) | tests/surface/api/graph/test_recorder.cpp
Medium_RecorderPlacementScopeDevice.OnAScopeRunsAnUntrackedVerbOnTheLocalDevice | resize | 8 | Recorder: On(scope) with an absent device resolved Local runs the verb (the placement is decided on the rules, small through a seam; the device run is a boundary kept in a medium test; the test makes a runtime in its body) | tests/surface/api/graph/test_recorder.cpp
Medium_RecorderPlacementScopeDevice.OnAScopeNoRuleMatchesRecordsOnDeviceZero | rewrite | 3 | Recorder: a scope with no matching rule records on device zero; the body checks only that no refusal is kept and one rule is recorded, so the name promises a device the body never checks; the rewrite adds the device check | tests/surface/api/graph/test_recorder.cpp
Medium_RecorderPlacementScopeDevice.OnAScopeThatFailsRefusesTheVerb | rewrite | 7 | Recorder: a Fail rule refuses the verb, names the rule and the device and records no node; the same check runs for two verb entry points in one body (rows over the two verbs, Q7) | tests/surface/api/graph/test_recorder.cpp
Medium_RecorderPlacementScopeDevice.ARefusedScopeEndsTheRun | resize | 8 | Graph: a refusal kept by a Fail rule ends the run with InvalidArgument (a decision on the kept refusal, small through a seam; the test makes a runtime in its body) | tests/surface/api/graph/test_recorder.cpp
Medium_RecorderPlacementScopeDevice.AScopeOpenedBeforeTheRulesFollowsTheNewSet | split | 3 | two promises: a scope opened before a rule set is read against the new set at record time; the cleared set leaves nothing to refuse | tests/surface/api/graph/test_recorder.cpp
Medium_RecorderPlacementScopeDevice.AScopeBufferAndItsTrackedNodesLandOnDeviceOne | rewrite | 5 | Recorder: a scope's buffer and the tracked nodes writing it land on device 1; the plan node write-base checks are plan internals (Q5); the rewrite keeps the device index and the values, and the skip stays for a one-device machine | tests/surface/api/graph/test_recorder.cpp
Medium_RecorderPlacedTask.ATaskOverBorrowedMemoryRunsOnTheDeviceItStates | rewrite | 8 | Recorder: a placed task over borrowed memory runs on the device it states (a device boundary, medium right; runtime in body; rewrite on the base fixture's device) | tests/surface/api/graph/test_recorder.cpp
Medium_RecorderPlacedTaskDeathTest.ATaskPlacedPastTheDeviceCountStops | rewrite | 8 | Recorder: a task placed on a device the runtime lacks stops the process; the test forks (EXPECT_DEATH) and makes a runtime in its body; the rewrite keeps one death test on the base fixture | tests/surface/api/graph/test_recorder.cpp
Medium_RecorderPlacedTask.AStatedDeviceInTheOwnersContextWinsOverTheOwner | rewrite | 1 | Recorder: a stated device in the owner's context wins over the owner; reaches Internal::ContextAccess and the device queue set (internal, context_access header, Q1); the rewrite checks the device through the public API | tests/surface/api/graph/test_recorder.cpp; tests/surface/api/run_recorder.hpp (not used)
Medium_RecorderPlacedTask.AStatedDeviceInAnotherContextIsRefused | rewrite | 1 | Recorder: a stated device in another context is refused; Internal::ContextAccess reach through other_device (Q1); the rewrite reads the refusal through the public run result | tests/surface/api/graph/test_recorder.cpp
Medium_RecorderPlacedTask.ATaskThatStatesNoDeviceRunsOnTheOwner | rewrite | 1 | Recorder: a task stating no device runs on the owner of its memory; Internal::ContextAccess in the assertion (Q1); the rewrite checks the device through the public API | tests/surface/api/graph/test_recorder.cpp
Medium_RecorderRuledTask.Regression_ARuleNamingDeviceOneRunsTheNodeThere | rewrite | 1 | Recorder: a rule naming device one runs its node there; Internal::ContextAccess in the assertion (Q1); the rewrite checks the device through the public API; the test skips below two devices | tests/surface/api/graph/test_recorder.cpp
Medium_RecorderRuledTask.Regression_ARuleNamingAnAbsentDeviceFollowsItsPolicy | rewrite | 1 | Recorder: a rule naming an absent device follows its if_absent policy; Internal::ContextAccess in the assertion (Q1) and two runs in one test; the rewrite splits the Local and Fail cases | tests/surface/api/graph/test_recorder.cpp
```

### `tests/surface/api/graph/test_region.cpp`

```
Medium_RegionEmitter.ExtensionTasksOrderAgainstANeighbourRegion | resize | 8 | Region extension: its tasks run in order and precede a consumer region (order decided by dependencies on values, so small through a plan; the test makes a runtime and runs kernels in its body) | tests/surface/api/graph/test_region.cpp
Medium_RegionEmitter.AnExtensionIsOneRule | split | 3 | two promises: an extension counts as one rule with its tasks as nodes (structure); the tasks it records run in order on the device | tests/surface/api/graph/test_region.cpp
Medium_RegionEmitter.AnExtensionRunsOnceWhenItIsCalled | split | 3 | two promises: extend calls the extension once, and a build, a replay or a neighbour rebuild does not call it again; a region recorded again after drop is extended again | tests/surface/api/graph/test_region.cpp
Medium_RegionEmitter.AStaticGraphTakesAnExtension | resize | 8 | a static graph accepts an extension beside an add() and both run (the size check is a decision on the recorded count, small; the kernel values are a medium boundary; the test makes a runtime in its body) | tests/surface/api/graph/test_region.cpp
```

### `tests/surface/api/graph/test_run_handle.cpp`

```
Medium_AsyncStep.SubmitReturnsBeforeTheStepFinishes | split | 3 | two promises: run_async returns before the step finishes; wait consumes the handle and the step's write is visible | tests/surface/api/graph/test_run_handle.cpp
Medium_AsyncStep.MatchesTheSynchronousResult | split | 3 | two promises: an asynchronous replay ends in the same State as a synchronous run; replaying asynchronously does not recompile | tests/surface/api/graph/test_run_handle.cpp
Medium_AsyncStep.StateAdvancesOnWaitNotOnSubmit | rewrite | 8 | RunHandle: the State advances on wait, not on submit; the boundary is a thread handoff, so medium is the right size, but the test makes a runtime in its body; the rewrite uses the base fixture's device | tests/surface/api/graph/test_run_handle.cpp
Medium_AsyncStep.DestroyingAnUnwaitedHandleStillFinishesTheStep | split | 3 | two promises: destroying an unwaited handle finishes the step; the graph stays runnable afterwards | tests/surface/api/graph/test_run_handle.cpp
Medium_AsyncStep.WaitingTwiceThrows | rewrite | 3 | the name says the second wait throws, the body checks that it returns an error value; the promise (a second wait is refused) is real, so the rewrite renames it and checks the InvalidState code, as the expect_refused helper in the same file does | tests/surface/api/graph/test_run_handle.cpp
Medium_AsyncStep.ASecondRunIsRejectedWhileAStepIsInFlight | split | 3 | two promises: a synchronous and an asynchronous run are refused while a step is in flight; the graph runs again once the step has settled | tests/surface/api/graph/test_run_handle.cpp
Medium_AsyncStep.TaskExceptionSurfacesOnWaitAndLeavesTheGraphUsable | split | 3 | two promises (the name joins them with And): a task exception surfaces from wait() as an error; the graph accepts a later run | tests/surface/api/graph/test_run_handle.cpp
Medium_AsyncStep.TryWaitReportsFailureAsAValue | rewrite | 8 | RunHandle: wait reports a task failure as a value; a thread boundary, so medium is the right size, but the test makes a runtime in its body (rewrite on the base fixture's device); the test has no TEST doc comment, only a line comment | tests/surface/api/graph/test_run_handle.cpp
Medium_AsyncStep.ReadyTracksTheStep | rewrite | 8 | RunHandle: ready is false while a step is held open and true once it has settled; a thread boundary, medium is right, the runtime is made in the body (rewrite on the base fixture's device) | tests/surface/api/graph/test_run_handle.cpp
Medium_AsyncStep.DynamicGraphRunsAsynchronously | split | 3 | two promises: a dynamic graph's asynchronous step runs its regions; a region added between steps is committed by the asynchronous path, and one composition is made per mutation | tests/surface/api/graph/test_run_handle.cpp
Medium_AsyncStep.AnotherGraphIsRefusedWhileAStepIsPending | split | 3 | two promises: other graphs' runs (sync, counted, async) are refused while a step is pending, and their tasks do not run; the runtime is free once the step has settled | tests/surface/api/graph/test_run_handle.cpp
Medium_AsyncStep.CancelReachesOnlyTheGraphItIsCalledOn | split | 3 | three promises: cancel on an idle graph does not stop another graph's step; cancel stops its own pending step; cancel with no run in flight does not mark the next run | tests/surface/api/graph/test_run_handle.cpp
Medium_AsyncStep.AGraphDestroyedWithAPendingStepEndsTheStep | split | 3 | two promises: a graph destroyed with a pending step waits for it and the handle reports it ended; the runtime is left free for another graph | tests/surface/api/graph/test_run_handle.cpp
Medium_AsyncStep.ADynamicGraphDestroyedWithAPendingStepEndsTheStep | rewrite | 8 | a dynamic graph destroyed with a pending step waits for it and the handle reports it ended; a thread boundary, medium is right, the runtime is made in the body (rewrite on the base fixture's device) | tests/surface/api/graph/test_run_handle.cpp
Medium_AsyncStep.ARegionDroppedWhileAStepIsPendingFinishesTheStep | split | 3 | several promises: drop removes the region from the count; the pending step finishes the dropped task; the next run does not run it and composes once | tests/surface/api/graph/test_run_handle.cpp
Medium_AsyncStep.ARegionDroppedAndRecordedAgainWhileAStepIsPendingRunsItsNewWork | split | 3 | two promises: the pending step runs the recording it started with; the next run runs the recording made while the step was pending | tests/surface/api/graph/test_run_handle.cpp
```

### `tests/surface/api/interop/test_host_export.cpp`

```
Medium_InteropHostExport.AlignsAndRoundsTheRegion | resize | 8 | exported host region is aligned and rounded to the requested alignment (rounding is a decision on values, small through a pure helper; the test makes a runtime in its body) | tests/surface/api/interop/test_host_export.cpp; tests/surface/api/interop/README.md
Medium_InteropHostExport.TheRegistryRoutesTheExportedAddress | rewrite | 1 | the USM registry routes an exported address to its device; reaches Internal::ContextAccess and memory/usm_allocator.hpp (internal, Q1); the rewrite checks the routing through a public write and read of the exported buffer | tests/surface/api/interop/test_host_export.cpp
Medium_InteropHostExport.AKernelWritesWhatTheHostReads | rewrite | 8 | an exported buffer written by a kernel is visible on the host (a device boundary, medium right; runtime in body; rewrite on the base fixture's device) | tests/surface/api/interop/test_host_export.cpp
Medium_InteropHostExport.ImportOnAQueueWithoutTheExtensionReturnsUnsupported | resize | 8 | import_device refuses with Unsupported on a device without device_import (the refusal is a decision on the capability flag, small through a seam; the test has a skip guard, and the runtime is made in the body) | tests/surface/api/interop/test_host_export.cpp
Medium_InteropHostExportDeathTest.AnAlignmentThatIsNotAPowerOfTwoStops | resize | 8 | export_host stops on an alignment that is not a power of two (a precondition decided on values, small through a seam; the death test forks and makes a runtime in its body) | tests/surface/api/interop/test_host_export.cpp
Medium_InteropHostExportDeathTest.ASizeThatOverflowsWhenRoundedStops | resize | 8 | export_host stops when the byte size overflows on rounding (a precondition decided on values, small through a seam; the death test forks and makes a runtime in its body) | tests/surface/api/interop/test_host_export.cpp
Medium_InteropHostExport.AnExportNoDeviceCanSatisfyReturnsAllocationFailed | split | 3 | two promises: an export no device can hold returns AllocationFailed; the runtime stays usable for a later export | tests/surface/api/interop/test_host_export.cpp
```

### `tests/surface/api/interop/test_interop.cpp`

```
Medium_InteropImport.HostExportReportsWhetherTheQueueOffersHostUSM | rewrite | 1 | capabilities().host_export equals the device's usm_host_allocations aspect; reaches Internal::ContextAccess (Q1) and loops over devices with a for loop in the body (control flow, Q1); the rewrite is a row table over devices with the public capability only | tests/surface/api/interop/test_interop.cpp
Medium_InteropHostExport.AnExportIsHostResidentAndALateCountAcceptsIt | rewrite | 1 | an export is host resident and sized_from_device takes it; the second part has no assertion, only the survival of a process stop (a forbidden kind, no assertion); the rewrite asserts what sized_from_device returns; the skip guard is also control flow | tests/surface/api/interop/test_interop.cpp
Medium_InteropImport.ADeviceWithoutImportIsRefusedByItsIndex | resize | 8 | import_device on a device without device_import is refused Unsupported and names the index (a decision on capabilities, small through a seam; runtime in body; skip guard) | tests/surface/api/interop/test_interop.cpp
Medium_InteropImport.ADeviceThatImportsReportsItsUUID | rewrite | 1 | a device that imports reports a UUID; for loop over devices with an if in the body (control flow, Q1); the rewrite is a row table over the devices that import | tests/surface/api/interop/test_interop.cpp
Medium_InteropImport.ADefaultDescriptorIsRefusedForItsUnstatedKind | resize | 8 | a default descriptor is refused InvalidAccess for its unstated kind (decision on the descriptor, small through a seam; runtime in body; skip guard) | tests/surface/api/interop/test_interop.cpp
Medium_InteropImport.ANullWin32HandleIsRefusedByName | resize | 8 | a null Win32 handle is refused InvalidAccess by name (a decision on the handle value, small through a seam; runtime in body; compiled on this Windows platform) | tests/surface/api/interop/test_interop.cpp
Medium_InteropImport.AFileDescriptorAboveIntIsRefusedWithItsValue | resize | 8 | a file descriptor above INT_MAX is refused InvalidAccess with its value (a decision on the handle value, small through a seam; runtime in body); this TEST is under the non-Windows branch and is not compiled on this platform, so no run result exists for it here | tests/surface/api/interop/test_interop.cpp
Medium_InteropImport.ZeroElementsImportNothingAndReturnAnEmptyBuffer | resize | 8 | a zero-element import returns an empty buffer (a decision on the count, small through a seam; runtime in body; skip guard) | tests/surface/api/interop/test_interop.cpp
Medium_InteropImport.AHandleKindOfAnotherPlatformIsRefusedByName | resize | 8 | a handle kind of another platform is refused InvalidAccess by name (a decision on the kind, small through a seam; runtime in body; skip guard) | tests/surface/api/interop/test_interop.cpp
Medium_InteropImport.ARegionPastTheAllocationIsRefusedWithItsNumbers | split | 3 | two promises: a region past the allocation is refused with its numbers; an offset past the allocation does not wrap the subtraction | tests/surface/api/interop/test_interop.cpp
Medium_InteropImport.AnotherDevicesUUIDIsRefusedWithBothUUIDs | resize | 8 | a descriptor naming another device's UUID is refused with both UUIDs (a decision on the descriptor, small through a seam; runtime in body; two skip guards) | tests/surface/api/interop/test_interop.cpp
Medium_InteropBorrow.ABorrowedBufferViewsTheCallersMemory | split | 3 | two promises: a borrowed buffer views the caller's memory and its writes reach it; the handle releases its share of the owner when destroyed | tests/surface/api/interop/test_interop.cpp
Medium_InteropBorrow.ARunNamingABorrowedBufferWhoseOwnerLetGoIsRefused | split | 3 | two promises: a run naming a borrowed buffer runs while the owner lives; after the owner lets go the run is refused DanglingResource | tests/surface/api/interop/test_interop.cpp
Medium_InteropBorrow.DroppingTheBorrowedHandleLeavesTheRunValid | resize | 8 | a run whose borrowed handle was dropped stays valid (liveness decided on values, small through a seam; runtime in body) | tests/surface/api/interop/test_interop.cpp
Medium_InteropBorrow.ARangeCopyReachesBorrowedMemoryTheRuntimeDidNotAllocate | rewrite | 1 | a range write and read reach borrowed shared memory the runtime did not allocate; reaches Internal::ContextAccess to get the queue for sycl::malloc_shared (Q1, private header); the rewrite uses the base fixture's queue through a public call | tests/surface/api/interop/test_interop.cpp
Medium_InteropBorrow.AMismatchedResidencyIsRefusedWithBothNames | rewrite | 1 | a borrow whose residency differs from the memory's is refused naming both; Internal::ContextAccess reach for the queue (Q1) and a skip guard; the rewrite takes the queue from the base fixture | tests/surface/api/interop/test_interop.cpp
Medium_InteropBorrow.ARuntimeBufferBorrowedAsAnotherResidencyIsRefusedByItsRecord | rewrite | 1 | a runtime buffer borrowed as another residency is refused by its record, and the same residency is agreed; Internal::ContextAccess reach (Q1) and a skip guard; two promises, split at the rewrite | tests/surface/api/interop/test_interop.cpp
```

### `tests/surface/api/launch/test_extent.cpp`

```
Small_Extent.CountIsTheProductOfTheSizes | keep | 9 | Extent: count is the product of the sizes, ranks 1 to 3 (its static_assert pins the N-size deduction guide, which is a second promise with no separate test; the body is not only a static_assert) | tests/surface/api/launch/test_extent.cpp; modules/surface/api/include/SushiRuntime/api/launch/extent.hpp; tests/surface/api/graph/test_dynamic_graph.cpp (grep for .at/count: no other count() check)
Small_Extent.AtMovesTheOrigin | keep | 9 | Extent: at keeps the sizes, moves the origin and returns a copy | tests/surface/api/launch/test_extent.cpp; modules/surface/api/include/SushiRuntime/api/launch/extent.hpp; tests/surface/api/graph/test_dynamic_graph.cpp (grep: graph-level launch at origin, a different promise)
```

### `tests/surface/api/launch/test_nd_range.cpp`

```
Small_NdRange.CountIsTheGlobalSize | split | 3 | two promises: NdRange: count is the product of the global sizes; NdRange: to_nd_range keeps the global and local ranges | tests/surface/api/launch/test_nd_range.cpp; modules/surface/api/include/SushiRuntime/api/launch/nd_range.hpp
Small_NdRange.AtMovesTheOrigin | keep | 9 | NdRange: at keeps the global and local ranges and moves the origin | tests/surface/api/launch/test_nd_range.cpp; modules/surface/api/include/SushiRuntime/api/launch/nd_range.hpp; tests/surface/api/graph/plumbing/test_builder_surface.cpp (grep: builder use of .at, a different promise)
```

### `tests/surface/api/runtime/test_advanced_controls.cpp`

```
Medium_PerfControls.RebalancerIsOffUnlessRequested | resize | 8 | Off by default and on when asked is decided on RuntimeConfig values, with no device needed. Seam: the mapping of RuntimeConfig onto the settings, which runtime/README.md describes as one hand-written function; a small test checks the flag there. Q4: this file is the unit's own; its rebalancer-on half is duplicated in test_runtime.cpp EveryFieldReachesTheRuntime, which loses that half. | tests/surface/api/runtime/test_advanced_controls.cpp; tests/surface/api/runtime/README.md; tests/surface/api/runtime/test_runtime.cpp
Medium_PerfControls.RebalancerToggleKeepsExecutionCorrect | rewrite | 1 | Loops fill and check 128 elements in the body, and three runs with toggles are in one test. | tests/surface/api/runtime/test_advanced_controls.cpp; tests/surface/api/runtime/README.md
```

### `tests/surface/api/runtime/test_errors.cpp`

```
Medium_ErrorIsolation.ThrowingKernelLeavesRuntimeReusable | delete | 1 | Loops in the body (control flow). The same promise is in RuntimeSurvivesRepeatedFailures of this file, which covers round 0 as well. | tests/surface/api/runtime/test_errors.cpp; tests/surface/api/runtime/README.md
Medium_ErrorIsolation.ThrowingNodeIsReportedByNameAndRegistrationIndex | resize | 8 | The error text is built from the node name, the index and the exception text, decided on values. A host-only graph would show it; the runtime is not needed. Seam to build: the error composition, which the runtime currently reaches only through a device. | tests/surface/api/runtime/test_errors.cpp; tests/surface/api/runtime/README.md
Medium_ErrorIsolation.RuntimeSurvivesRepeatedFailures | rewrite | 1 | Loops over rounds and elements in the body. Its promise is real and no longer tested once ThrowingKernelLeavesRuntimeReusable is deleted. | tests/surface/api/runtime/test_errors.cpp; tests/surface/api/runtime/README.md
```

### `tests/surface/api/runtime/test_placement_scope.cpp`

```
Small_PlacementScope.AccessorsReturnTheClassAndTheKeyItWasBuiltWith | delete | 1 | Trivial getters. Covered through the public overloads by Medium_RuntimePlacementScope.TheStringAndClassOverloadsGiveEqualClasses in this file. | tests/surface/api/runtime/test_placement_scope.cpp
Small_PlacementScope.ANoneKeyStaysNone | delete | 1 | Trivial getter. The none key is checked through both overloads in BothOverloadsKeepAKeyOfZeroAndANoneKey. | tests/surface/api/runtime/test_placement_scope.cpp
Small_PlacementScope.AKeyOfZeroIsKeptAsZero | delete | 1 | Trivial getter. The zero key is checked through both overloads in BothOverloadsKeepAKeyOfZeroAndANoneKey. | tests/surface/api/runtime/test_placement_scope.cpp
Small_PlacementScope.ACopyReportsTheSameClassAndKey | delete | 1 | Trivial: the copy is the defaulted copy constructor. The nothrow copy is stated by the static_assert at file scope. | tests/surface/api/runtime/test_placement_scope.cpp
Medium_RuntimePlacementScope.TheStringAndClassOverloadsGiveEqualClasses | resize | 8 | Class equality from a name is decided on the string. The runtime serves only as the overload's context. Seam: the name-to-class mapping in core (placement_class.hpp, located by Glob, not read), tested small. | tests/surface/api/runtime/test_placement_scope.cpp; modules/surface/api/include/SushiRuntime/api/runtime/placement_scope.hpp; modules/foundation/core/include/SushiRuntime/core/placement_class.hpp (located only)
Medium_RuntimePlacementScope.BothOverloadsKeepAKeyOfZeroAndANoneKey | resize | 8 | Key values pass through decided on values; no device is needed. Seam: the class and key forwarding, tested small. | tests/surface/api/runtime/test_placement_scope.cpp; modules/surface/api/include/SushiRuntime/api/runtime/placement_scope.hpp
Medium_RuntimePlacementScope.ANameWithoutADotIsAClass | resize | 8 | Name parsing is logic on a string. Seam: the core class parse, tested small. Q4 not verified: the core placement-class tests were not read, so a duplicate there would make this a delete. | tests/surface/api/runtime/test_placement_scope.cpp; modules/surface/api/include/SushiRuntime/api/runtime/placement_scope.hpp
Medium_RuntimePlacementScope.AMalformedNameStopsTheProcess | resize | 8 | Four malformed names in one death test, which are rows of one promise. The name check decides on the string and needs no runtime. Seam: the name check, tested small. | tests/surface/api/runtime/test_placement_scope.cpp; modules/surface/api/include/SushiRuntime/api/runtime/placement_scope.hpp
Medium_PlacementScopeDevice.AScopeWithoutARuntimeGivesDeviceZeroAndNoBuffer | split | 3 | Two results that can break apart: device() gives 0, and buffer() returns InvalidState. Split into one test each, both small. | tests/surface/api/runtime/test_placement_scope.cpp; modules/surface/api/include/SushiRuntime/api/runtime/placement_scope.hpp; modules/surface/api/source/placement_scope.cpp
Medium_PlacementScopeDevice.ANoRuleScopeGivesDeviceZero | rewrite | 7 | A check sits between two actions: device() is read, rules are set, device() is read again. | tests/surface/api/runtime/test_placement_scope.cpp
Medium_PlacementScopeDevice.ARuleWithAPresentDeviceGivesThatDevice | rewrite | 7 | Four rule sets, each followed by a check: several actions in one test. Each rule becomes its own row, then resized to the small seam (see LocalGivesDeviceZeroForAnAbsentDevice). | tests/surface/api/runtime/test_placement_scope.cpp
Medium_PlacementScopeDevice.LocalGivesDeviceZeroForAnAbsentDevice | resize | 8 | Local on an absent device is decided on the rule and NodeFacts. Seam: Detail::local_device_under (source-private, inline, in modules/surface/api/source/local_rule_device.hpp) and resolve_device (anonymous namespace, modules/surface/api/source/placement_scope.cpp:56) move into the placement module, so a small test passes a NodeFacts. | tests/surface/api/runtime/test_placement_scope.cpp; modules/surface/api/source/placement_scope.cpp; modules/surface/api/source/local_rule_device.hpp; modules/foundation/placement/include/SushiRuntime/placement/placement_target_matching.hpp
Medium_PlacementScopeDevice.FailNamesTheRuleAndTheDevice | resize | 8 | The message is built from the rule, the target and the machine facts, decided on values. Seam: absent_refusal in local_rule_device.hpp, moved with resolve_device. | tests/surface/api/runtime/test_placement_scope.cpp; modules/surface/api/source/local_rule_device.hpp; modules/surface/api/source/placement_scope.cpp
Medium_PlacementScopeDevice.FailNamesAnUnnamedRuleByItsPosition | delete | 4 | Same promise as Small_PlacementTargetMatching.DescriptionsNameTheRuleAndEveryTargetPart: describe_rule names an unnamed rule by its position, and this path only calls it. | tests/surface/api/runtime/test_placement_scope.cpp; tests/foundation/placement/test_placement_target_matching.cpp; modules/foundation/placement/include/SushiRuntime/placement/placement_target_matching.hpp
Medium_PlacementScopeDevice.Regression_WaitGivesDeviceZeroWithoutWaiting | rewrite | 1 | Wall-clock measurement with steady_clock, to prove that a wait did not happen (forbidden kind). A Wait decision on values needs no clock; the rewrite checks the value and leaves time to the layer that waits. | tests/surface/api/runtime/test_placement_scope.cpp; tests/surface/api/runtime/README.md
Medium_PlacementScopeDevice.AnotherMachineMakesTheTargetAbsent | split | 3 | Three promises: another name is absent, another label is absent, and Local falls back. The matching half of each is in Regression_ADeviceOnlyTargetNamesOnlyAMachineWithTheDevice and MachinePartMatchesByNameOrLabel of tests/foundation/placement/test_placement_target_matching.cpp; the split keeps only the scope-level result. | tests/surface/api/runtime/test_placement_scope.cpp; tests/foundation/placement/test_placement_target_matching.cpp
Medium_PlacementScopeDevice.ALabelTheMachineCarriesMakesTheTargetPresent | resize | 8 | The config label reaches the machine facts, and the scope's decision on them is a value. Seam: resolve_device over a NodeFacts built with the label. The label match itself is covered by MachinePartMatchesByNameOrLabel, so the scope-level result is what stays. | tests/surface/api/runtime/test_placement_scope.cpp; modules/surface/api/source/placement_scope.cpp; tests/foundation/placement/test_placement_target_matching.cpp
Medium_PlacementScopeDevice.AKindOrMemoryNoDeviceHasIsAbsent | delete | 1 | Control flow in the body (a loop over devices and an if that skips). The absent-kind and absent-memory promise is in DevicePartPicksByIndexKindOrMemory of tests/foundation/placement/test_placement_target_matching.cpp. | tests/surface/api/runtime/test_placement_scope.cpp; tests/foundation/placement/test_placement_target_matching.cpp
Medium_PlacementScopeDevice.AScopeOpenedBeforeTheRulesFollowsTheNewSet | rewrite | 7 | Several set and clear calls, each followed by a check. | tests/surface/api/runtime/test_placement_scope.cpp
Medium_PlacementScopeDevice.LocalPlacesTheBufferOnDeviceZero | resize | 8 | The device index comes from resolve_device, decided on values. The allocation is a device boundary, and the one medium test that keeps it is ABufferLandsOnTheDeviceTheRuleNames. | tests/surface/api/runtime/test_placement_scope.cpp; modules/surface/api/source/placement_scope.cpp
Medium_PlacementScopeDevice.FailRefusesTheBuffer | resize | 8 | The refusal comes from device(), decided on values. The docstring says the buffer allocates nothing, and the test does not check that: the add row for the allocation claim covers it. | tests/surface/api/runtime/test_placement_scope.cpp; modules/surface/api/source/placement_scope.cpp
Medium_PlacementScopeDevice.ABufferLandsOnTheDeviceTheRuleNames | rewrite | 1 | An if-statement in the body skips on a machine with one device (control flow). This is the only test of device 1 selection. | tests/surface/api/runtime/test_placement_scope.cpp; tests/surface/api/runtime/README.md
```

### `tests/surface/api/runtime/test_runtime.cpp`

```
Medium_RuntimeEntry.OneStructConfiguresAndTheViewOutlivesTheRuntime | split | 3 | Two promises: the worker count reaches the pool; the view outlives its runtime. | tests/surface/api/runtime/test_runtime.cpp; tests/surface/api/runtime/README.md
Medium_RuntimeEntry.DevicesListsEveryDeviceInOrder | rewrite | 1 | Internal::ContextAccess and the engine header runtime_context.hpp in a surface test (forbidden kind), plus a loop over devices in the body. Rewrite through advanced().queue(i), which ACallerWaitsAndCopiesThroughTheDeviceQueue already uses. | tests/surface/api/runtime/test_runtime.cpp; tests/surface/api/runtime/README.md
Medium_RuntimeEntry.TopologyReportsTheLocalMachine | rewrite | 1 | Loops over devices and labels in the body. The promise is real and tested only here. | tests/surface/api/runtime/test_runtime.cpp; tests/surface/api/runtime/README.md
Medium_RuntimeEntry.TopologyNamesTheMachineByItsHostNameWhenTheConfigGivesNone | split | 3 | Two promises: an empty node_name gives the host name; the default labels are empty. The oracle for the name is only non-empty, which is weak: the split reads the host name from the OS. | tests/surface/api/runtime/test_runtime.cpp; tests/surface/api/runtime/README.md
Medium_RuntimePlacementRules.ASecondCompileReportsTheWinnerOfTheNewRules | split | 3 | Three promises: a new set changes the winner the plan reports; an unchanged set does not rebuild; clearing gives none. | tests/surface/api/runtime/test_runtime.cpp; tests/surface/api/runtime/README.md
Medium_RuntimePlacementRules.ChangingTheRulesDuringARunIsRefusedAsInvalidState | split | 3 | Three promises: set is refused during a run; clear is refused during a run; both succeed after the run. The gate also waits on a 2 s wall-clock deadline in a helper. | tests/surface/api/runtime/test_runtime.cpp; tests/surface/api/runtime/README.md
Medium_RuntimePlacementRules.Regression_ARuleChangeRacingARunStartNeverLandsInsideTheRun | rewrite | 1 | A loop of 2000 runs with a check per run in the body (control flow). The race belongs in a medium concurrency test run under TSan, with the loop in a helper. | tests/surface/api/runtime/test_runtime.cpp; tests/surface/api/runtime/README.md
Medium_RuntimeEntry.CreateRefusesALabelKeyThatBreaksARule | rewrite | 1 | A loop over a case table in the body. The same promise over three inputs is one parametrised test. | tests/surface/api/runtime/test_runtime.cpp; tests/surface/api/runtime/README.md
Medium_RuntimeEntry.DefaultConfigCreates | delete | 1 | A loop in the body. Default-off rebalancer is in RebalancerIsOffUnlessRequested; a graph run on a default-like config is in ASingleWorkerStillExecutesTheWholeGraph. | tests/surface/api/runtime/test_runtime.cpp; tests/surface/api/runtime/test_advanced_controls.cpp; tests/surface/api/runtime/test_runtime_config.cpp
Medium_RuntimeEntry.EveryFieldReachesTheRuntime | rewrite | 1 | Internal::ContextAccess and runtime_settings.hpp (engine header) in a surface test. The promise that every field reaches the settings is tested only here. | tests/surface/api/runtime/test_runtime.cpp; tests/surface/api/runtime/README.md
Medium_RuntimeEntry.AMovedRuntimeKeepsItsContext | rewrite | 1 | Internal::ContextAccess in a surface test (forbidden kind). Owner ruling needed on how bridge promises are tested; see the report. | tests/surface/api/runtime/test_runtime.cpp
Medium_RuntimeEntry.ACallerWaitsAndCopiesThroughTheDeviceQueue | rewrite | 1 | A loop checks each word in the body (control flow). It names no Internal:: type, which the README asks for. | tests/surface/api/runtime/test_runtime.cpp; tests/surface/api/runtime/README.md
Medium_RuntimeEntryDeathTest.AQueueOfAnUnknownDeviceStops | resize | 8 | The bounds check decides on an index and a count. Seam: the index check of the device list, tested small. | tests/surface/api/runtime/test_runtime.cpp; tests/surface/api/runtime/README.md
Medium_RuntimeEntry.ARequestNoDeviceCanSatisfyReturnsAllocationFailed | split | 3 | Three promises: buffer returns AllocationFailed with its count in the message; state does the same; a failed allocation leaves the runtime usable. The README notes no seam makes an allocation fail. | tests/surface/api/runtime/test_runtime.cpp; tests/surface/api/runtime/README.md
Medium_RuntimeEntry.LiveAllocationCountFollowsABuffer | rewrite | 7 | Checks between actions: after a buffer, after a state, after the scope exit. | tests/surface/api/runtime/test_runtime.cpp; tests/surface/api/runtime/README.md
Medium_Runtime.AThreadOutlivingTheRuntimeTearsDownCleanly | rewrite | 1 | SUCCEED() with no assertion (forbidden kind), and a loop in the thread body. The promise is real and tested only here; it belongs under the sanitizer lanes. | tests/surface/api/runtime/test_runtime.cpp; tests/surface/api/runtime/README.md
Medium_LoggingDefaults.CreateIsSilentAtTheDefaultLevel | rewrite | 8 | Captures the process stdout and stderr file descriptors and sets the process-wide logger level. A medium test may not reach those (the table does not list them). Seam: an injected log sink for device discovery, so the test reads records instead of streams. | tests/surface/api/runtime/test_runtime.cpp; tests/surface/api/runtime/README.md
Medium_LoggingDefaults.DeviceLineArrivesThroughTheSinkAtInfo | rewrite | 1 | A loop over records in the body (control flow), and a process-wide sink. | tests/surface/api/runtime/test_runtime.cpp; tests/surface/api/runtime/README.md
```

### `tests/surface/api/runtime/test_runtime_config.cpp`

```
Medium_CoTenancy.DefaultsToOneWorkerPerCoreFromCoreZero | resize | 8 | The default pool size is decided on the core count. Seam: a pool-sizing function that takes a count, tested small. hardware_concurrency is read here. | tests/surface/api/runtime/test_runtime_config.cpp; tests/surface/api/runtime/README.md
Medium_CoTenancy.ReservesTheCoresTheHostAskedFor | rewrite | 1 | An if-statement skips on fewer than four cores (control flow). The placement promise is only here. | tests/surface/api/runtime/test_runtime_config.cpp; tests/surface/api/runtime/README.md
Medium_CoTenancy.ASingleWorkerStillExecutesTheWholeGraph | keep | 9 | Promise row: a graph executes with one worker. The graph runs on worker threads on one machine, so medium is the right size; the count check is a setup check. | tests/surface/api/runtime/test_runtime_config.cpp; tests/surface/api/runtime/README.md
Medium_CoTenancy.FloatPolicyRunsUnpinned | resize | 8 | Pool shape and pinning are decided on config values. The graph run is the promise of ASingleWorkerStillExecutesTheWholeGraph. Seam: the pool-resolution function, tested small. | tests/surface/api/runtime/test_runtime_config.cpp; tests/surface/api/runtime/README.md
Medium_CoTenancy.AnOutOfRangeCoreRangeIsClampedNotIgnored | resize | 8 | The clamp is decided on the first core, the count and the machine's core count. Seam: the clamp function, tested small. | tests/surface/api/runtime/test_runtime_config.cpp; tests/surface/api/runtime/README.md
Medium_DeviceSelection.RequireFp64AdmitsOnlyFp64Devices | rewrite | 1 | A loop over nodes in the body (control flow), and Internal::Topology::HardwareDiscovery (engine symbol) in a surface test. | tests/surface/api/runtime/test_runtime_config.cpp; tests/surface/api/runtime/README.md
Medium_DeviceSelection.RequireFp64StillConstructsOnAConformingLane | rewrite | 1 | An if/else on the lane in the body: two outcomes in one test (control flow). | tests/surface/api/runtime/test_runtime_config.cpp; tests/surface/api/runtime/README.md
Medium_DeviceSelection.AnEmptyFilterFailsWithUnsupported | resize | 8 | The Unsupported error and the filter's name come from an empty topology list. Seam: the empty-list check in RuntimeContext, which the README names; not traced in source. | tests/surface/api/runtime/test_runtime_config.cpp; tests/surface/api/runtime/README.md
Medium_DeviceSelection.DefaultFilterIsUnaffected | delete | 4 | Same promise as DevicesListsEveryDeviceInOrder (test_runtime.cpp), which asserts at least one device. | tests/surface/api/runtime/test_runtime_config.cpp; tests/surface/api/runtime/test_runtime.cpp
Small_RuntimeConfig.NamesNoMachineAndCarriesNoLabelByDefault | delete | 1 | Trivial default values. Empty labels are checked through a runtime by TopologyNamesTheMachineByItsHostNameWhenTheConfigGivesNone. | tests/surface/api/runtime/test_runtime_config.cpp
Medium_ErrorTypes.AFilterThatExcludesEveryDeviceReturnsUnsupported | delete | 1 | EXPECT_NO_THROW around a call that returns a result and cannot throw (forbidden kind). The same promise, with the message, is AnEmptyFilterFailsWithUnsupported. | tests/surface/api/runtime/test_runtime_config.cpp
```

### `tests/surface/api/test_api_log_category.cpp`

```
Small_ApiLogCategory.TheCategoryIsNamedAfterTheModule | delete | 1 | A static_assert with SUCCEED(), on a log category constant (trivial code, forbidden kinds). | tests/surface/api/test_api_log_category.cpp; tests/surface/api/README.md
```

### `tests/surface/api/test_fluent.cpp`

```
Medium_FluentAPI.TryRunSuccessPath | delete | 1 | Loops in the body. RunReportFieldsAfterMultiStep checks the successful report and the task count again. | tests/surface/api/test_fluent.cpp; tests/surface/api/README.md
Medium_FluentAPI.RunReportFieldsAfterMultiStep | rewrite | 1 | Loop fills the buffer in the body. | tests/surface/api/test_fluent.cpp; tests/surface/api/README.md
Medium_FluentAPI.RunForRunsAtLeastOneStep | rewrite | 1 | Loop fills in the body. The step count after run_for(0) is the promise. | tests/surface/api/test_fluent.cpp; tests/surface/api/README.md
Medium_FluentAPI.RunForRunsMultipleStepsWithinBudget | rewrite | 1 | Loop in the body, and a 50 ms wall-clock budget: the test depends on time. | tests/surface/api/test_fluent.cpp; tests/surface/api/README.md
Medium_FluentAPI.RawHandlerKernelAdd | rewrite | 1 | Loops fill and check in the body. detail/test_emit_untracked.cpp (possible duplicate) was not read. | tests/surface/api/test_fluent.cpp; tests/surface/api/README.md
Medium_FluentAPI.RangeKernelAdd | rewrite | 1 | Loops fill and check in the body. | tests/surface/api/test_fluent.cpp; tests/surface/api/README.md
Medium_FluentAPI.PointerTrackedAddOrdering | rewrite | 1 | Loops in the body. The ordering promise is shared with ReadsWritesWrappersOrdering, which uses the wrapper overload. | tests/surface/api/test_fluent.cpp; tests/surface/api/README.md
Medium_FluentAPI.TrackedRangeKernelOrdering | rewrite | 1 | Loops fill and check in the body. | tests/surface/api/test_fluent.cpp; tests/surface/api/README.md
Medium_FluentAPI.TrackedRawKernelOrdering | rewrite | 1 | Loops fill and check in the body. | tests/surface/api/test_fluent.cpp; tests/surface/api/README.md
Medium_FluentAPI.TrackedNdRangeOrdering | rewrite | 1 | Loops fill and check in the body. | tests/surface/api/test_fluent.cpp; tests/surface/api/README.md
Medium_FluentAPI.SubRegionDisjointWritesThenWholeRead | rewrite | 1 | Loops in the body. The promise is sub-region identity, with three actions. | tests/surface/api/test_fluent.cpp; tests/surface/api/README.md
Medium_FluentAPI.BufferAllocateUseAndReadBack | rewrite | 1 | Loops in the body. The buffer lifecycle may duplicate data/test_buffer.cpp (not read). | tests/surface/api/test_fluent.cpp; tests/surface/api/README.md
Medium_FluentAPI.BufferMoveLeavesSenderEmpty | resize | 8 | The moved-from state is decided on the handle, but a buffer must allocate device memory to exist. Seam: a handle with no allocation, which the code does not offer. | tests/surface/api/test_fluent.cpp; tests/surface/api/README.md
Medium_FluentAPI.GraphSizeTracksAdds | rewrite | 7 | Checks between adds (size after each add). | tests/surface/api/test_fluent.cpp; tests/surface/api/README.md
Medium_FluentAPI.ContextAccessorIsConsistent | rewrite | 1 | Internal::ContextAccess and engine header runtime_context.hpp in a surface test (forbidden kind). | tests/surface/api/test_fluent.cpp; tests/surface/api/README.md
Medium_FluentAPI.RunZeroStepsIsNoOp | rewrite | 1 | Loops in the body. | tests/surface/api/test_fluent.cpp; tests/surface/api/README.md
Medium_FluentHardening.Extent2DPerElementMap | rewrite | 1 | Nested loops in the body. launch/test_extent.cpp (not read) may cover the same iteration. | tests/surface/api/test_fluent.cpp; tests/surface/api/README.md
Medium_FluentHardening.NdRange2DTiledKernel | rewrite | 1 | Loops in the body. launch/test_nd_range.cpp (not read) may overlap. | tests/surface/api/test_fluent.cpp; tests/surface/api/README.md
Medium_FluentHardening.State2DSteppedUpdate | rewrite | 1 | Loops in the body. | tests/surface/api/test_fluent.cpp; tests/surface/api/README.md
Medium_FluentHardening.VariadicCoupledThreeInputs | rewrite | 1 | Loops in the body. | tests/surface/api/test_fluent.cpp; tests/surface/api/README.md
Medium_FluentHardening.ReadsWritesWrappersOrdering | rewrite | 1 | Loops in the body. Ordering overlaps PointerTrackedAddOrdering; the wrapper form is the one to keep. | tests/surface/api/test_fluent.cpp; tests/surface/api/README.md
Medium_FluentHardening.InOutCapturedArguments | rewrite | 1 | Loops in the body. | tests/surface/api/test_fluent.cpp; tests/surface/api/README.md
Medium_FluentHardening.InteriorOnlyHaloUpdate | rewrite | 1 | Nested loops in the body. | tests/surface/api/test_fluent.cpp; tests/surface/api/README.md
Medium_FluentLifecycle.StateReplayIncrement | rewrite | 1 | A loop with a break and a flag scan in the body (control flow). | tests/surface/api/test_fluent.cpp; tests/surface/api/README.md
Medium_FluentLifecycle.ReplayAcrossCalls | rewrite | 1 | Loops in the body. | tests/surface/api/test_fluent.cpp; tests/surface/api/README.md
Medium_FluentLifecycle.RunUntilStopsOnCondition | rewrite | 1 | Loop fills in the body. | tests/surface/api/test_fluent.cpp; tests/surface/api/README.md
Medium_FluentLifecycle.CoupledFieldsAreJacobi | rewrite | 1 | Loops in the body. | tests/surface/api/test_fluent.cpp; tests/surface/api/README.md
Medium_FluentLifecycle.MutationRecompiles | rewrite | 1 | Loops in the body. | tests/surface/api/test_fluent.cpp; tests/surface/api/README.md
Medium_FluentLifecycle.EmptyGraphIsInvalid | resize | 8 | The empty check decides on the node count. Seam: the empty check on a graph with no runtime, tested small (not present in code). | tests/surface/api/test_fluent.cpp; tests/surface/api/README.md
Medium_FluentLifecycle.DanglingResourceIsReported | resize | 8 | The refusal is decided on the recorder's handle liveness, but the buffer must allocate device memory. Seam: recorder liveness on a handle without a device (not present in code). | tests/surface/api/test_fluent.cpp; tests/surface/api/README.md
```

### `tests/surface/api/test_sushiruntime.cpp`

```
Medium_SteadyStateAllocation.TheCounterObservesTheCallingThread | keep | 9 | Promise row: the allocation counter sees the calling thread. It checks the instrument before any claim made with it. | tests/surface/api/test_sushiruntime.cpp; tests/surface/api/README.md
Medium_SteadyStateAllocation.ReuseTicksDoNotAllocatePerTick | rewrite | 1 | If-statements in the body on the counter and the AdaptiveCpp skip (control flow). Three claims in one test. A count of allocations is a measurement, which the testing table puts under benchmark. | tests/surface/api/test_sushiruntime.cpp; tests/surface/api/README.md
Medium_SteadyStateAllocation.AsyncWaitIntoAReusedReportIsAmortised | rewrite | 1 | Loops in the body (warm-up and the measure lambda). A measurement, benchmark kind. | tests/surface/api/test_sushiruntime.cpp; tests/surface/api/README.md
Medium_SteadyStateAllocation.ProfiledReuseTicksDoNotAllocatePerTick | rewrite | 1 | An if-statement on the counter in the body (control flow). A measurement, benchmark kind. | tests/surface/api/test_sushiruntime.cpp; tests/surface/api/README.md
Medium_SteadyStateAllocation.SkippedUnderSanitizer | delete | 1 | No assertion: GTEST_SKIP only (forbidden kind). | tests/surface/api/test_sushiruntime.cpp; tests/surface/api/README.md
```

### `tests/surface/api/vocabulary/test_captures.cpp`

```
Medium_Captures.EachCaptureNamesItsBuffer | rewrite | 1 | static_assert run as a test in the body (forbidden kind). The type flags move to file scope; the pointer and key check stays. | tests/surface/api/vocabulary/test_captures.cpp; tests/surface/api/vocabulary/README.md
Medium_Captures.InOutReadsAndWrites | rewrite | 1 | static_assert and SUCCEED only (forbidden kinds). The InOut flags move to file scope. | tests/surface/api/vocabulary/test_captures.cpp; tests/surface/api/vocabulary/README.md
```

### `tests/surface/api/vocabulary/test_coupled.cpp`

```
Medium_Coupled.NamesTheStatesItWasGiven | rewrite | 1 | static_assert on type deduction in the body (forbidden kind). The type check moves to file scope; the address check stays. | tests/surface/api/vocabulary/test_coupled.cpp; tests/surface/api/vocabulary/README.md
```

### `tests/surface/api/vocabulary/test_dependencies.cpp`

```
Medium_Dependencies.KeysFollowTheHandle | rewrite | 1 | Detail:: symbols (read_key_of, write_key_of, liveness_of) in a surface test (forbidden kind). Owner ruling on bridge tests. | tests/surface/api/vocabulary/test_dependencies.cpp; tests/surface/api/vocabulary/README.md; modules/surface/api/include/SushiRuntime/api/vocabulary/dependencies.hpp
Medium_Dependencies.ReadsAndWritesCollectOneKeyEach | keep | 9 | Promise row: Reads collects one key per handle in argument order. keys is a public member, documented as "Read regions, in argument order" (dependencies.hpp:102). | tests/surface/api/vocabulary/test_dependencies.cpp; tests/surface/api/vocabulary/README.md; modules/surface/api/include/SushiRuntime/api/vocabulary/dependencies.hpp
```

### `tests/surface/api/vocabulary/test_dynamic.cpp`

```
Medium_LateBindings.DynamicSizePerElement | rewrite | 1 | Loops in the body (k and i). | tests/surface/api/vocabulary/test_dynamic.cpp; tests/surface/api/vocabulary/README.md
Medium_LateBindings.EnableTogglePerElement | rewrite | 1 | Loops in the body. | tests/surface/api/vocabulary/test_dynamic.cpp; tests/surface/api/vocabulary/README.md
Medium_LateBindings.DisabledNodeStillReleasesSuccessors | rewrite | 1 | Loops in the body. | tests/surface/api/vocabulary/test_dynamic.cpp; tests/surface/api/vocabulary/README.md
Medium_LateBindings.DynamicSizeState | rewrite | 1 | Loop fills in the body. | tests/surface/api/vocabulary/test_dynamic.cpp; tests/surface/api/vocabulary/README.md
Medium_LateBindings.ComposedWhenAndSized | rewrite | 1 | Loops in the body. | tests/surface/api/vocabulary/test_dynamic.cpp; tests/surface/api/vocabulary/README.md
Medium_LateBindings.BaseOffsetIteratesASlice | rewrite | 1 | Loops in the body. | tests/surface/api/vocabulary/test_dynamic.cpp; tests/surface/api/vocabulary/README.md
Medium_LateBindings.BaseWithoutASizeRunsToTheEnd | delete | 1 | Loops in the body. Rest-of-capacity resolution is tested small in Small_LateWindow.NoHostCountTakesTheRestOfTheCapacity. | tests/surface/api/vocabulary/test_dynamic.cpp; tests/surface/api/detail/test_late_window.cpp
Medium_LateBindings.BaseAndSizeBothFromTheDevice | rewrite | 1 | Loop check in the body. | tests/surface/api/vocabulary/test_dynamic.cpp; tests/surface/api/vocabulary/README.md
Medium_LateBindings.WindowPastCapacityFailsTheRun | split | 3 | Two promises: a window past capacity fails with InvalidGraph; a later window that fits runs. The fault half is Small_LateWindow.AWindowPastTheCapacityIsAFault in detail/test_late_window.cpp, so the fault piece is deleted and the recovery piece stays. | tests/surface/api/vocabulary/test_dynamic.cpp; tests/surface/api/detail/test_late_window.cpp
Medium_LateBindingsDeathTest.BasedAtDeviceStopsOnAnUnreadableSlot | rewrite | 7 | Three death calls and one success call, each a different result: rows of one promise. | tests/surface/api/vocabulary/test_dynamic.cpp; tests/surface/api/vocabulary/README.md
Medium_LateBindingsDeathTest.TheMemberSetterStopsOnASharedSlot | merge | 4 | Same promise as Medium_DeviceCountDeathTest.MemberSettersStopOnASlotThatIsNotHostResident in this file: member setters check residency and index. Merge into one death test with rows (Device, Shared, out of range). | tests/surface/api/vocabulary/test_dynamic.cpp; tests/surface/api/vocabulary/README.md
Medium_DeviceCount.ConsumerIteratesTheCountTheProducerJustWrote | delete | 1 | Loops in the body. The producer-consumer ordering is CounterIsRegisteredAsAReadAutomatically. Before this test is deleted, its three multipliers and its compile-once check move into that test as rows. | tests/surface/api/vocabulary/test_dynamic.cpp
Medium_DeviceCount.CounterIsRegisteredAsAReadAutomatically | rewrite | 1 | Loops in the body. It keeps the producer-edge promise, and receives the rows from ConsumerIterates. | tests/surface/api/vocabulary/test_dynamic.cpp; tests/surface/api/vocabulary/README.md
Medium_DeviceCount.ChainedProducerCountsInOneComposition | rewrite | 1 | Loops in the body. | tests/surface/api/vocabulary/test_dynamic.cpp; tests/surface/api/vocabulary/README.md
Medium_DeviceCount.WhenComposesWithDeviceSize | rewrite | 1 | Loop fills in the body. | tests/surface/api/vocabulary/test_dynamic.cpp; tests/surface/api/vocabulary/README.md
Medium_DeviceCount.CountOverCapacityFailsTheRun | split | 3 | Two promises: an over-capacity count fails the run; a clean graph runs afterwards. This file is the unit's own; the same promise for chained graphs is DeviceCountOverCapacityFailsAChainedRun in tests/surface/api/graph/test_device_sized.cpp (other chunk). | tests/surface/api/vocabulary/test_dynamic.cpp; tests/surface/api/graph/test_device_sized.cpp
Medium_DeviceCountDeathTest.ACounterThatIsNotHostResidentStops | split | 3 | Two promises: residency refusal, and index refusal; plus one valid call. | tests/surface/api/vocabulary/test_dynamic.cpp; tests/surface/api/vocabulary/README.md
Medium_DeviceCount.ReadsTheCountAtTheGivenIndex | rewrite | 1 | Loops in the body. | tests/surface/api/vocabulary/test_dynamic.cpp; tests/surface/api/vocabulary/README.md
Medium_DeviceCount.AcceptsABaseSlotAtTheGivenIndex | rewrite | 1 | EXPECT_NO_THROW around a call that stops the process on a bad index (forbidden kind). The claim needs a run to check the window. | tests/surface/api/vocabulary/test_dynamic.cpp; tests/surface/api/vocabulary/README.md
Medium_DeviceCountDeathTest.MemberSettersStopOnASlotThatIsNotHostResident | merge | 4 | Same promise as TheMemberSetterStopsOnASharedSlot in this file. Merge into one death test with rows. | tests/surface/api/vocabulary/test_dynamic.cpp; tests/surface/api/vocabulary/README.md
Medium_DeviceSlotLiveness.ADestroyedCountBufferRefusesTheRun | split | 3 | Two promises: the recorder watches two allocations; a run after destruction is refused with DanglingResource. | tests/surface/api/vocabulary/test_dynamic.cpp; tests/surface/api/graph/test_recorder.cpp (not read)
Medium_DeviceSlotLiveness.ADestroyedBaseBufferRefusesTheRun | merge | 4 | Same promise as ADestroyedCountBufferRefusesTheRun and ADestroyedCountBufferRefusesAStateRule: a destroyed slot buffer refuses the run. Merge into one test with rows. | tests/surface/api/vocabulary/test_dynamic.cpp
Medium_DeviceSlotLiveness.ADestroyedCountBufferRefusesAStateRule | merge | 4 | Same promise as ADestroyedBaseBufferRefusesTheRun. Merge into one test with rows. | tests/surface/api/vocabulary/test_dynamic.cpp
```

### `tests/surface/api/vocabulary/test_late_count.cpp`

```
Medium_LateCount.FixedResolvesToItsCount | delete | 4 | Same promise, at small size, in tests/surface/api/detail/test_late_count_resolution.cpp: HostCountsResolveOnTheHost checks the fixed count's live value. | tests/surface/api/vocabulary/test_late_count.cpp; tests/surface/api/detail/test_late_count_resolution.cpp
Medium_LateCount.HostProviderBelowCapacity | delete | 4 | Same promise, at small size, in detail/test_late_count_resolution.cpp: HostCountsResolveOnTheHost checks the host provider's value. | tests/surface/api/vocabulary/test_late_count.cpp; tests/surface/api/detail/test_late_count_resolution.cpp
Medium_LateCount.ChangingHostCountKeepsOneCompile | rewrite | 7 | Checks between runs, one per provider change. | tests/surface/api/vocabulary/test_late_count.cpp; tests/surface/api/vocabulary/README.md
Medium_LateCount.HostCountAboveCapacityFailsTheRunWithInvalidGraph | split | 3 | Two promises: fails with InvalidGraph naming both numbers; a later valid count runs. The zero-fill piece is Small_LateCountResolution.AHostCountAboveTheCapacityResolvesToZero, so only the first two stay. | tests/surface/api/vocabulary/test_late_count.cpp; tests/surface/api/detail/test_late_count_resolution.cpp
Medium_LateCount.DeviceSlotResolvesFromHostResidentBuffer | resize | 8 | The live value read from a host-resident slot is decided on values. Seam: late_count_source and read_live_count in the detail layer, which the small sibling exercises for host counts but not for slots. | tests/surface/api/vocabulary/test_late_count.cpp; tests/surface/api/detail/test_late_count_resolution.cpp
Medium_LateCount.DeviceCountAboveCapacityFailsTheRunOnBothLayers | rewrite | 1 | A loop over force_scheduler in the body (control flow). Rows, one per layer. | tests/surface/api/vocabulary/test_late_count.cpp; tests/surface/api/vocabulary/README.md
Medium_LateCountDeathTest.ANonHostDeviceSlotStopsAtConstruction | merge | 4 | Same promise as Medium_LateBoundVocabularyDeathTest.FromDeviceStopsOnASlotThatIsNotHostResident in this file. Merge into one death test with rows. | tests/surface/api/vocabulary/test_late_count.cpp
Medium_LateCount.ResolverSeesTheCountItsPredecessorJustWrote | rewrite | 7 | Two runs, each with a check after it. | tests/surface/api/vocabulary/test_late_count.cpp; tests/surface/api/vocabulary/README.md
Medium_LateBoundVocabularyDeathTest.FromDeviceStopsOnAnIndexPastTheSlot | split | 3 | Two promises: an index past the slot stops; the last index is accepted with its capacity. | tests/surface/api/vocabulary/test_late_count.cpp; tests/surface/api/vocabulary/README.md
Medium_LateBoundVocabularyDeathTest.FromDeviceStopsOnASlotThatIsNotHostResident | merge | 4 | Same promise as ANonHostDeviceSlotStopsAtConstruction in this file. Merge into one death test with rows (Device, Shared). | tests/surface/api/vocabulary/test_late_count.cpp
Medium_LateBoundVocabularyDeathTest.FromHostStopsOnAnEmptyProvider | resize | 8 | No runtime is built here; the empty-provider check decides on the callable. The test is labelled medium and needs no device, so it is a small death test. | tests/surface/api/vocabulary/test_late_count.cpp; modules/surface/api/include/SushiRuntime/api/vocabulary/late_count.hpp (grep only)
Medium_LateBoundVocabularyDeathTest.EveryFactoryStopsOnACapacityPastAUint32 | rewrite | 1 | An if-statement skips on a narrow size_t (control flow). Three factory death calls and one accepted call are rows of one promise (also Q7). | tests/surface/api/vocabulary/test_late_count.cpp; tests/surface/api/vocabulary/README.md
Medium_LateBoundVocabulary.CapacityReportsThePlanSize | delete | 1 | Trivial getters. The last factory's capacity is checked in FromDeviceStopsOnAnIndexPastTheSlot after its split. | tests/surface/api/vocabulary/test_late_count.cpp
Medium_LateBoundVocabulary.ARecordedCopyKeepsTheHostProvider | rewrite | 7 | Checks between two runs, with the provider changed between them. | tests/surface/api/vocabulary/test_late_count.cpp; tests/surface/api/vocabulary/README.md
Medium_LateBoundVocabulary.FromDeviceReadsTheWordAtItsIndex | rewrite | 1 | Loops fill the slots in the body. | tests/surface/api/vocabulary/test_late_count.cpp; tests/surface/api/vocabulary/README.md
Medium_LateBoundVocabulary.TheSizeSourceSetLastWins | rewrite | 1 | Loops reset the buffer in the body (control flow). | tests/surface/api/vocabulary/test_late_count.cpp; tests/surface/api/vocabulary/README.md
Medium_LateBoundVocabulary.MembersChainOnANamedDynamic | rewrite | 1 | Loops in the body. | tests/surface/api/vocabulary/test_late_count.cpp; tests/surface/api/vocabulary/README.md
```

### `tests/surface/api/vocabulary/test_placement.cpp`

```
Medium_Placement.AnIndexNamesItsDevice | delete | 1 | Trivial: stores an index and reads it back. | tests/surface/api/vocabulary/test_placement.cpp
Medium_Placement.ABufferNamesItsDevice | keep | 9 | Promise row: a buffer names its own device. The buffer allocates on a device, so the device is the boundary. | tests/surface/api/vocabulary/test_placement.cpp; tests/surface/api/vocabulary/README.md
```

### `tests/surface/context/test_context.cpp`

```
Small_Context.IsHeldByPointerOnly | rewrite | 1 | static_assert and EXPECT_EQ on literals (forbidden kinds). The compile-time check moves to file scope. | tests/surface/context/test_context.cpp; tests/surface/context/README.md
```

### `tests/surface/context/test_context_handle.cpp`

```
Small_ContextHandle.ADefaultHandleIsEmpty | delete | 1 | Trivial default; the share check uses a Detail:: symbol. Emptiness of a wrapped handle is in AHandleWrappedAroundNothingIsEmpty. | tests/surface/context/test_context_handle.cpp; tests/surface/context/README.md
Small_ContextHandle.AHandleWrappedAroundNothingIsEmpty | rewrite | 1 | Detail::ContextHandleAccess::wrap (Detail symbol) in a surface test. The copy-of-empty claim can use the public default constructor. | tests/surface/context/test_context_handle.cpp; tests/surface/context/README.md
```

### `tests/surface/context/test_context_handle_access.cpp`

```
Small_ContextHandleAccess.AnEmptyShareGivesAnEmptyHandle | delete | 1 | Detail:: symbol; same promise as AHandleWrappedAroundNothingIsEmpty in test_context_handle.cpp, which is the unit's own file. | tests/surface/context/test_context_handle_access.cpp; tests/surface/context/test_context_handle.cpp
Small_ContextHandleAccess.HandleOfReturnsTheOwnersHandle | rewrite | 1 | Detail::ContextHandleAccess::handle_of (Detail symbol). The promise lives only in the bridge; owner ruling needed. | tests/surface/context/test_context_handle_access.cpp; tests/surface/context/README.md
```

### `tests/surface/context_access/test_context_access.cpp`

```
Medium_ContextAccess.AHandleReturnsTheContextItWasBuiltFrom | rewrite | 1 | Internal::ContextAccess and the engine's runtime_assembly Test::make_context in a surface test (forbidden kind). Owner ruling needed. | tests/surface/context_access/test_context_access.cpp; tests/surface/context_access/README.md
Medium_ContextAccess.AnOwnerIsReadThroughItsHandle | rewrite | 1 | Detail::ContextHandleAccess friend and Internal symbols in a surface test. | tests/surface/context_access/test_context_access.cpp; tests/surface/context_access/README.md
Medium_ContextAccess.ANullContextMakesAnEmptyHandle | rewrite | 1 | Internal::ContextAccess in a surface test (forbidden kind). No device is made, so the Medium label is also wrong. | tests/surface/context_access/test_context_access.cpp; tests/surface/context_access/README.md
Medium_ContextAccess.EveryShareOfAHandleReachesOneCapabilityCache | rewrite | 1 | Internal symbols and make_context (device) in a surface test. | tests/surface/context_access/test_context_access.cpp; tests/surface/context_access/README.md
Medium_ContextAccess.EachHandleBuiltKeepsItsOwnCapabilityCache | rewrite | 1 | Internal symbols in a surface test. | tests/surface/context_access/test_context_access.cpp; tests/surface/context_access/README.md
```

### `tests/surface/engine_handles/test_late_controls_access.cpp`

```
Small_LateControlsAccess.BothSharesNameOneBinding | rewrite | 1 | Detail:: symbols and Internal::Graph::LateBindings (engine header late_bindings.hpp) in a surface test (forbidden kinds). Owner ruling needed. | tests/surface/engine_handles/test_late_controls_access.cpp
Small_LateControlsAccess.NullStaysNull | delete | 1 | Trivial null conversion, with Detail:: symbols. | tests/surface/engine_handles/test_late_controls_access.cpp
```

### `tests/surface/engine_handles/test_plan_access.cpp`

```
Small_PlanAccess.ARoundTripNamesTheSamePlan | rewrite | 1 | Detail::Plan and Internal::Graph::CompiledGraph (engine header) in a surface test (forbidden kinds). Owner ruling needed. | tests/surface/engine_handles/test_plan_access.cpp
Small_PlanAccess.NullStaysNull | delete | 1 | Trivial null conversion, with Detail:: symbols. | tests/surface/engine_handles/test_plan_access.cpp
```

### `tests/surface/interop/test_device_uuid.cpp`

```
Small_DeviceUUID.EqualOnlyWhenEveryByteIs | delete | 1 | Loop in the body (control flow). The same promise is Small_InteropVocabulary.UUIDsCompareByEveryByte in test_interop_capabilities.cpp, which has no loop and is kept. Q4 would keep this file's copy as the unit's own, but Q1 decides first; see the report. | tests/surface/interop/test_device_uuid.cpp; tests/surface/interop/test_interop_capabilities.cpp; tests/surface/interop/README.md
```

### `tests/surface/interop/test_external_handle_kind_set.cpp`

```
Small_ExternalHandleKindSet.ADefaultSetIsEmpty | delete | 1 | Loop over kinds in the body (control flow). The default empty set is checked by Small_InteropVocabulary.DefaultCapabilitiesOfferNothing (importable_kinds.is_empty()). | tests/surface/interop/test_external_handle_kind_set.cpp; tests/surface/interop/test_interop_capabilities.cpp
Small_ExternalHandleKindSet.AKindSetHoldsExactlyTheKindsInserted | split | 3 | Two promises: membership holds only the inserted kind; empty again after erase. | tests/surface/interop/test_external_handle_kind_set.cpp; tests/surface/interop/README.md
Small_ExternalHandleKindSet.InsertAndEraseAreIdempotent | rewrite | 7 | Several actions (insert, copy, insert, erase) with checks between. The promise is one sentence. | tests/surface/interop/test_external_handle_kind_set.cpp; tests/surface/interop/README.md
Small_ExternalHandleKindSet.EraseRemovesOnlyItsKind | rewrite | 1 | A loop inserts every kind in the body (control flow). | tests/surface/interop/test_external_handle_kind_set.cpp; tests/surface/interop/README.md
Small_ExternalHandleKindSet.SetsCompareByMembers | keep | 9 | Promise row: two sets compare equal exactly when they hold the same kinds. | tests/surface/interop/test_external_handle_kind_set.cpp; tests/surface/interop/README.md
Small_ExternalHandleKindSet.WorksAtCompileTime | rewrite | 1 | static_assert run as a test in the body (forbidden kind). The compile-time insert check moves to file scope. | tests/surface/interop/test_external_handle_kind_set.cpp; tests/surface/interop/README.md
```

### `tests/surface/interop/test_external_memory.cpp`

```
Small_ExternalMemory.DefaultsNameNoHandle | delete | 1 | Trivial default values and a static_assert. Same promise as ADefaultDescriptorStatesNoHandleKind in test_interop_capabilities.cpp. | tests/surface/interop/test_external_memory.cpp; tests/surface/interop/test_interop_capabilities.cpp
```

### `tests/surface/interop/test_interop_capabilities.cpp`

```
Small_InteropVocabulary.UUIDsCompareByEveryByte | keep | 9 | Promise row: UUIDs compare by every byte (byte 15 alone differs). Its partner, device_uuid's test, is deleted at Q1. | tests/surface/interop/test_interop_capabilities.cpp; tests/surface/interop/test_device_uuid.cpp
Small_InteropVocabulary.DefaultCapabilitiesOfferNothing | delete | 1 | Trivial default values (forbidden kind). Default-deny is not tested after review; restate it as a behaviour test if the owner wants it. | tests/surface/interop/test_interop_capabilities.cpp; tests/surface/interop/README.md
Small_InteropVocabulary.ADefaultDescriptorStatesNoHandleKind | delete | 1 | Trivial default values (forbidden kind). | tests/surface/interop/test_interop_capabilities.cpp; tests/surface/interop/README.md
```

### `tests/surface/memory_exchange/test_capability_cache.cpp`

```
Medium_CapabilityCache.TheSecondQueryHitsTheCache | delete | 1 | stub of the repository's own CapabilityCache::Probe, a call counter in a lambda; the cache-hit promise is also checked by Medium_CapabilityCache.EachDeviceIsProbedOnce in the same file, so the replacement must cover one device without a skip | tests/surface/memory_exchange/test_capability_cache.cpp; tests/surface/memory_exchange/README.md; modules/surface/memory_exchange/include/SushiRuntime/memory_exchange/capability_cache.hpp; modules/surface/memory_exchange/README.md
Medium_CapabilityCache.EachDeviceIsProbedOnce | rewrite | 1 | stub recorder of CapabilityCache::Probe, and a device list read from sycl::device::get_devices with a skip on one device; the per-device promise has no other test. The rewrite needs the in-memory adapter of CapabilityCache::Probe in modules/surface/memory_exchange, and a device key the cache takes without hardware (see capability_cache.hpp, the key is sycl::device) | tests/surface/memory_exchange/test_capability_cache.cpp; tests/surface/memory_exchange/README.md; modules/surface/memory_exchange/include/SushiRuntime/memory_exchange/capability_cache.hpp; modules/surface/memory_exchange/README.md
Medium_CapabilityCache.AThrowingProbeKeepsNothing | rewrite | 1 | stub that throws on its first call and counts calls, a control branch inside the stand-in; the promise has no other test. The rewrite needs the in-memory Probe adapter with a switch that throws once | tests/surface/memory_exchange/test_capability_cache.cpp; tests/surface/memory_exchange/README.md; modules/surface/memory_exchange/include/SushiRuntime/memory_exchange/capability_cache.hpp; modules/surface/memory_exchange/README.md
```

### `tests/surface/memory_exchange/test_memory_exchange.cpp`

```
Medium_MemoryExchange.CapabilitiesOfDeviceZero | rewrite | 8 | memory_exchange: query_capabilities returns capabilities of a device that answers. Size medium is right, a device answers, but the test builds a runtime in its body with Test::make_context, which medium forbids; the runtime comes from the test program's shared fixture | tests/surface/memory_exchange/test_memory_exchange.cpp; modules/surface/memory_exchange/include/SushiRuntime/memory_exchange/memory_exchange.hpp; modules/surface/memory_exchange/source/memory_exchange.cpp; modules/engine/runtime_assembly/include/SushiRuntime/runtime_assembly/context_factory.hpp; modules/engine/runtime_assembly/README.md
Medium_MemoryExchange.AHostExportGivesTheSizeAsked | rewrite | 8 | memory_exchange: export_host_memory allocates the bytes asked. Size medium is right, a device allocation, but the runtime is built in the body. Requesting 4096 at alignment 4096 leaves rounding unseen; rounding is checked by Medium_InteropHostExport.AlignsAndRoundsTheRegion in another file, a different promise | tests/surface/memory_exchange/test_memory_exchange.cpp; modules/surface/memory_exchange/include/SushiRuntime/memory_exchange/memory_exchange.hpp; modules/surface/memory_exchange/source/memory_exchange.cpp; modules/engine/runtime_assembly/include/SushiRuntime/runtime_assembly/context_factory.hpp; tests/surface/api/interop/test_host_export.cpp
Medium_MemoryExchange.AnImportWithNoKindIsRefused | resize | 8 | memory_exchange: an import of an Unspecified kind is refused with InvalidAccess naming it. The check reads only the descriptor and the capabilities (descriptor_refusal in memory_exchange.cpp), so a small test reaches it once that function moves to an internal header; the device check before it keeps one medium test. Q4 copy: Medium_InteropImport.ADefaultDescriptorIsRefusedForItsUnstatedKind in tests/surface/api/interop/test_interop.cpp, kept here by the own-module ruling | tests/surface/memory_exchange/test_memory_exchange.cpp; modules/surface/memory_exchange/source/memory_exchange.cpp; modules/surface/memory_exchange/include/SushiRuntime/memory_exchange/memory_exchange.hpp; tests/surface/api/interop/test_interop.cpp; modules/surface/memory_exchange/README.md
Medium_MemoryExchangeImport.AKindTheDeviceDoesNotImportIsRefusedByName | rewrite | 6 | memory_exchange: an import of a kind the device does not import is refused, naming the kind. The expected code and the kind come from query_capabilities, which is the code under test, so the oracle is not named. The rewrite takes the in-memory Probe adapter with a fixed importable set and writes the expected InvalidAccess in the test. Q4 copy: Medium_InteropImport.AHandleKindOfAnotherPlatformIsRefusedByName in tests/surface/api/interop/test_interop.cpp, kept here by the own-module ruling | tests/surface/memory_exchange/test_memory_exchange.cpp; modules/surface/memory_exchange/source/memory_exchange.cpp; modules/surface/memory_exchange/include/SushiRuntime/memory_exchange/memory_exchange.hpp; tests/surface/api/interop/test_interop.cpp; modules/surface/memory_exchange/README.md
```

### `tests/surface/memory_exchange/test_memory_exchange_log_category.cpp`

```
Small_MemoryExchangeLogCategory.TheCategoryIsNamedAfterTheModule | delete | 1 | forbidden kinds: static_assert run as a test, SUCCEED() with no assertion, and trivial code (a log category constant); it states no real promise | tests/surface/memory_exchange/test_memory_exchange_log_category.cpp; modules/surface/memory_exchange/include/SushiRuntime/memory_exchange/memory_exchange_log_category.hpp
```

### `tests/surface/profiling/benchmark_run_overhead.cpp`

```
Medium_RunOverheadBenchmark.PrintsTheCostOfOneRunInEachState | delete | 1 | Forbidden kind: printing as the body (a table to stdout); its only checks are median greater than zero. It states no promise and is a measurement, which the testing skill moves to a Benchmark_ scenario with no size. CMake already builds it as a benchmark source, not as a test | tests/surface/profiling/benchmark_run_overhead.cpp; tests/surface/profiling/CMakeLists.txt; tests/surface/profiling/test_register_profiler_parts/session_pipeline.hpp
```

### `tests/surface/profiling/test_clock_conversion.cpp`

```
Small_ClockConversion.ThePairedInstantMapsToItsSteadyReading | merge | 4 | clock_conversion: linear mapping of event time by the pairing distance; the same promise as ALaterEventTime... and AnEarlierEventTime..., merge into one row table in this file | tests/surface/profiling/test_clock_conversion.cpp; modules/surface/profiling/include/SushiRuntime/profiling/clock_conversion.hpp
Small_ClockConversion.ALaterEventTimeLandsTheSameDistanceLater | merge | 4 | same promise as ThePairedInstantMapsToItsSteadyReading and AnEarlierEventTimeLandsTheSameDistanceEarlier; one row table | tests/surface/profiling/test_clock_conversion.cpp; modules/surface/profiling/include/SushiRuntime/profiling/clock_conversion.hpp
Small_ClockConversion.AnEarlierEventTimeLandsTheSameDistanceEarlier | merge | 4 | same promise as ThePairedInstantMapsToItsSteadyReading and ALaterEventTimeLandsTheSameDistanceLater; one row table | tests/surface/profiling/test_clock_conversion.cpp; modules/surface/profiling/include/SushiRuntime/profiling/clock_conversion.hpp
Small_ClockConversion.ATimeBeforeTheOriginReadsZero | keep | 9 | clock_conversion: a time before the origin reads 0 and does not wrap; the origin neighbour (3001 reads 1) is unchecked, see the add | tests/surface/profiling/test_clock_conversion.cpp; modules/surface/profiling/include/SushiRuntime/profiling/clock_conversion.hpp
Small_ClockConversion.AnEventClockFarAheadOfTheSteadyClockConverts | keep | 9 | clock_conversion: distance preserved on a 64-bit event clock far ahead of the steady one | tests/surface/profiling/test_clock_conversion.cpp; modules/surface/profiling/include/SushiRuntime/profiling/clock_conversion.hpp
Small_ClockConversion.SaturatesTheUncertaintyAt32Bits | keep | 9 | clock_conversion: saturate_uncertainty keeps 32 bits and saturates; boundary values in one promise | tests/surface/profiling/test_clock_conversion.cpp; modules/surface/profiling/include/SushiRuntime/profiling/clock_conversion.hpp
```

### `tests/surface/profiling/test_device_source.cpp`

```
Small_DeviceSource.ConvertsEventTimesThroughThePairing | rewrite | 1 | device_source: a device span is converted through its device pairing onto the record clock (sole test of it). Forbidden kind: the fixture Small_DeviceSource writes FakeAttachment, a stand-in for FeedAttachment, an interface this module owns (not the port's in-memory adapter). Rewrite on an in-memory FeedAttachment adapter in the profiling module | tests/surface/profiling/test_device_source.cpp; modules/surface/profiling/include/SushiRuntime/profiling/device_source.hpp; modules/surface/profiling/source/device_source.cpp
Small_DeviceSource.SetsNodeInvocationOperationAndLayerAttributes | rewrite | 1 | device_source: node, invocation, operation and the layer name are written. Spy as above (FakeAttachment in the fixture). The name joins two promises (integer attributes; layer name); the rewrite splits them | tests/surface/profiling/test_device_source.cpp; modules/surface/profiling/source/device_source.cpp; modules/surface/profiling/include/SushiRuntime/profiling/device_source.hpp
Small_DeviceSource.ANameAttributeNamesTheChainLayer | delete | 1 | Q1 spy as above; the layer-name promise is checked in SetsNodeInvocationOperationAndLayerAttributes (scheduler value). The chain value is added as a row to that test in the rewrite | tests/surface/profiling/test_device_source.cpp; modules/surface/profiling/source/device_source.cpp
Small_DeviceSource.OmitsAWidthThatIsUnknown | rewrite | 1 | device_source: an unknown launch or live width is left out and an operation of 0 is left out. Spy as above. The name joins two promises; the rewrite splits them | tests/surface/profiling/test_device_source.cpp; modules/surface/profiling/source/device_source.cpp; modules/surface/profiling/include/SushiRuntime/profiling/device_source.hpp
Small_DeviceSource.WritesTheCostOfANodeThatStatesOne | rewrite | 1 | device_source: a stated cost writes flops, bytes and dtype (sole test of non-zero cost). Spy as above; the zero and absent cases of the next test become rows here | tests/surface/profiling/test_device_source.cpp; modules/surface/profiling/source/device_source.cpp
Small_DeviceSource.WritesAStatedZeroCostAndOmitsAnAbsentOne | rewrite | 1 | device_source: a stated zero cost writes zeros and an absent cost writes nothing. Spy as above; merged into the rows of WritesTheCostOfANodeThatStatesOne in the rewrite | tests/surface/profiling/test_device_source.cpp; modules/surface/profiling/source/device_source.cpp
Small_DeviceSource.PutsAHostNodeOnTheHostTrack | rewrite | 1 | device_source: a host node (host device index) goes on the host track with steady times and zero uncertainty, with no pairing. Spy as above (sole host test) | tests/surface/profiling/test_device_source.cpp; modules/surface/profiling/source/device_source.cpp; modules/surface/profiling/include/SushiRuntime/profiling/device_source.hpp
Small_DeviceSource.ASpanOfALaterPeriodWaitsForIt | rewrite | 1 | device_source: a span whose start lies in a later period waits and is taken by that period, not counted late. Spy as above (sole test of waiting) | tests/surface/profiling/test_device_source.cpp; modules/surface/profiling/source/device_source.cpp
Small_DeviceSource.ASpanOfAClosedPeriodIsDroppedLate | rewrite | 1 | device_source: a span whose start lies before the period is dropped late and not added. Spy as above; the duplicate ASpanPastTheDelayIsStillCountedLate is deleted. collect reaches the process-wide logger through SR_LOG_WARN when a loss is reported (question 8 note for the rewrite) | tests/surface/profiling/test_device_source.cpp; modules/surface/profiling/source/device_source.cpp
Small_DeviceSource.ForwardsTheDropsOfARun | rewrite | 1 | device_source: a run drop counts reach the sink as late spans. Spy as above; the zero case of ARunWithNoDropsForwardsNone becomes a row here | tests/surface/profiling/test_device_source.cpp; modules/surface/profiling/source/device_source.cpp; modules/surface/profiling/include/SushiRuntime/profiling/run_queue.hpp
Small_DeviceSource.ARunWithNoDropsForwardsNone | delete | 1 | Q1 spy as above; the promise (drops forwarded as late) is checked in ForwardsTheDropsOfARun, which takes the zero case as a row | tests/surface/profiling/test_device_source.cpp; modules/surface/profiling/source/device_source.cpp
Small_DeviceSource.AMissingPairingDropsTheSpan | rewrite | 1 | device_source: a device span with no pairing is dropped and counted late. Spy as above (sole test). collect reaches the process-wide logger through SR_LOG_WARN (question 8 note) | tests/surface/profiling/test_device_source.cpp; modules/surface/profiling/source/device_source.cpp
Small_DeviceSource.TheQueuesOverflowIsCounted | rewrite | 1 | device_source: records a full queue dropped are counted late. The test builds its own source with no attachment, but the fixture SetUp starts the shared spy, so the fixture rule applies; the rewrite drops the spy from the fixture. collect reaches the process-wide logger (question 8 note) | tests/surface/profiling/test_device_source.cpp; modules/surface/profiling/source/device_source.cpp; modules/surface/profiling/source/run_queue.cpp
Small_DeviceSource.ARecordWithNoRowIsNamedByItsIndex | rewrite | 1 | device_source: a record with no table row is named "node <index>". Spy as above (sole test) | tests/surface/profiling/test_device_source.cpp; modules/surface/profiling/source/device_source.cpp
Small_DeviceSource.AnUnknownRuntimeIsSkipped | rewrite | 1 | device_source: an observation of a runtime the source was not given is skipped. Spy as above (sole test) | tests/surface/profiling/test_device_source.cpp; modules/surface/profiling/source/device_source.cpp
Small_DeviceSource.WritesTheGraphLabelWhenOneIsSet | delete | 1 | Q1 spy as above; the graph attribute promise is checked in TwoGraphsWithEqualNodeIndicesDifferByGraph, which carries the one-table case as a row | tests/surface/profiling/test_device_source.cpp; modules/surface/profiling/source/device_source.cpp
Small_DeviceSource.TwoGraphsWithEqualNodeIndicesDifferByGraph | rewrite | 1 | device_source: two tables with equal node indices are told apart by their graph label. Spy as above; takes the one-table label case from WritesTheGraphLabelWhenOneIsSet as a row | tests/surface/profiling/test_device_source.cpp; modules/surface/profiling/source/device_source.cpp
Small_DeviceSource.WritesTheOriginOfANodeThatStatesOne | rewrite | 1 | device_source: a node that states an origin has it written; a node that states none has none. Spy as above (sole origin test) | tests/surface/profiling/test_device_source.cpp; modules/surface/profiling/source/device_source.cpp
Small_DeviceSource.OmitsTheOriginAndTheLabelWhenEmpty | rewrite | 1 | device_source: an empty origin and an empty label write no attribute. Spy as above. The name joins origin and label; the rewrite may split them | tests/surface/profiling/test_device_source.cpp; modules/surface/profiling/source/device_source.cpp
Small_DeviceSource.TheLabelOfATableIsInternedOnceForAllItsRuns | rewrite | 1 | device_source: the label of one table is interned once across its runs (names_.size unchanged). Spy as above (sole test of interning) | tests/surface/profiling/test_device_source.cpp; modules/surface/profiling/source/device_source.cpp
Small_DeviceSourceRuntimes.TwoRuntimesGetTracksNamedByTheirNodes | keep | 9 | device_source: tracks are named by each runtime node and device, one per runtime; no fixture, no spy, no device | tests/surface/profiling/test_device_source.cpp; modules/surface/profiling/source/device_source.cpp; modules/surface/profiling/include/SushiRuntime/profiling/device_source.hpp
Small_DeviceSource.EndStopsTheDeliveryAndDropsWhatWasNeverCollected | rewrite | 1 | device_source: end() detaches the delivery and discards what was never collected. Spy as above (the detach count is read from it). Two effects that can break apart; the rewrite splits them | tests/surface/profiling/test_device_source.cpp; modules/surface/profiling/source/device_source.cpp; modules/surface/profiling/source/run_queue.cpp
Small_DeviceSourceStart.BeginFailsWhenTheAttachmentRefuses | rewrite | 1 | device_source: begin() returns the refusal of the attachment and does not begin. The test writes its own FakeAttachment stand-in for FeedAttachment (forbidden kind). Rewrite on the in-memory FeedAttachment adapter that refuses; the does-not-begin half is unasserted and the rewrite checks it through collect | tests/surface/profiling/test_device_source.cpp; modules/surface/profiling/source/device_source.cpp; modules/surface/profiling/include/SushiRuntime/profiling/device_source.hpp
Small_DeviceSourceStart.TheSourceIsNamedSushiruntimeDevice | delete | 1 | test of trivial code, a constant compared with its literal (forbidden kind, "a constant"); the spelling is checked by test_registration.cpp::Medium_RegisterProfilerParts.TheRegistryMakesTheDeviceSource | tests/surface/profiling/test_device_source.cpp; tests/surface/profiling/test_register_profiler_parts/test_registration.cpp; modules/surface/profiling/source/device_source.cpp
Small_DeviceSource.ReportsItsMaxDelayBelowTheProfilersLimit | rewrite | 1 | device_source: the source reports the max delay it was given, and the default is within the profiler limit. Spy as above (the fixture source). The first assertion compares the constant with itself and the 60 s limit is a bare literal; the rewrite splits the default-within-limit contract from the reported value and takes the limit from the profiler | tests/surface/profiling/test_device_source.cpp; modules/surface/profiling/include/SushiRuntime/profiling/device_source.hpp; modules/surface/profiling/source/device_source.cpp
Small_DeviceSource.ASpanPastTheDelayIsStillCountedLate | delete | 1 | Q1 spy as above; the name promises a delay, the body checks a span that starts before its period (the promise of ASpanOfAClosedPeriodIsDroppedLate). The max-delay promise is covered by the rewrite of ReportsItsMaxDelayBelowTheProfilersLimit | tests/surface/profiling/test_device_source.cpp; modules/surface/profiling/source/device_source.cpp
```

### `tests/surface/profiling/test_device_track_name.cpp`

```
Small_DeviceTrackName.NamesTheNodeTheIndexAndTheDevice | keep | 9 | device_track_name: node/index/device format | tests/surface/profiling/test_device_track_name.cpp; modules/surface/profiling/include/SushiRuntime/profiling/device_track_name.hpp; modules/surface/profiling/source/device_track_name.cpp
Small_DeviceTrackName.TwoDevicesOfOneNameGetDifferentTracks | keep | 9 | device_track_name: distinct devices get distinct tracks | tests/surface/profiling/test_device_track_name.cpp; modules/surface/profiling/source/device_track_name.cpp
Small_DeviceTrackName.TwoNodesWithTheSameDeviceGetDifferentTracks | keep | 9 | device_track_name: distinct nodes get distinct tracks | tests/surface/profiling/test_device_track_name.cpp; modules/surface/profiling/source/device_track_name.cpp
Small_DeviceTrackName.TheHostTrackNamesTheNode | split | 3 | two promises: host_track_name is node/host, and the host track differs from a device track; one test each | tests/surface/profiling/test_device_track_name.cpp; modules/surface/profiling/source/device_track_name.cpp
Medium_DeviceTrackName.TheRuntimeOverloadAgreesWithThePartsOverload | rewrite | 1 | Forbidden kind: control flow in the body (a for loop over devices with EXPECT inside). Sole test of the agreement. Rewrite as one check per device through an expect_ helper, keeping Runtime::create only for the wiring. Question 8 also applies: the name choice is decided on values and belongs behind the seam named for the next test | tests/surface/profiling/test_device_track_name.cpp; modules/surface/profiling/source/device_track_name.cpp; modules/surface/profiling/include/SushiRuntime/profiling/device_track_name.hpp
Medium_DeviceTrackName.AnIndexTheRuntimeLacksNamesAGenericDevice | resize | 8 | device_track_name: the "device" fallback is a decision on values, reached only through Runtime::create (a device probe). Seam: a lookup over a device-name list, tested small; the medium wiring test keeps the runtime path | tests/surface/profiling/test_device_track_name.cpp; modules/surface/profiling/source/device_track_name.cpp; modules/surface/profiling/include/SushiRuntime/profiling/device_track_name.hpp
```

### `tests/surface/profiling/test_profiling_log_category.cpp`

```
Small_ProfilingLogCategory.TheCategoryIsNamedAfterTheModule | rewrite | 1 | Forbidden kinds: a static_assert run as a test, followed by SUCCEED(); a test of a constant. It is the only test of the category promise, which is real: device_source logs warnings and debug lines through K_PROFILING_LOG_CATEGORY. Rewrite: an in-memory log sink reached through a seam, asserting the category of one emitted line (the global logger is a process-wide instance, question 8) | tests/surface/profiling/test_profiling_log_category.cpp; modules/surface/profiling/include/SushiRuntime/profiling/profiling_log_category.hpp; modules/surface/profiling/source/device_source.cpp
```

### `tests/surface/profiling/test_register_profiler_parts/test_collect_profiling.cpp`

```
Medium_CollectProfiling.TheTableAndTheRecordsJoinByNodeIndex | rewrite | 1 | Forbidden kind: a for loop in the body with ASSERT and EXPECT. Also joins the cost facts of the table (those go to a small test over NodeTable, question 8). Rewrite as a join helper, and move the cost facts out | tests/surface/profiling/test_register_profiler_parts/test_collect_profiling.cpp; tests/surface/profiling/test_register_profiler_parts/observed_pipeline.hpp; modules/foundation/report/include/SushiRuntime/report/node_table.hpp
Medium_CollectProfiling.EveryDeviceRecordHasItsDevicesPairing | delete | 1 | Forbidden kind: a for loop in the body. The promise (each device record has its device's pairing) is checked by test_collect_profiling.cpp::TaskBodiesOnTheWorkerPoolClaimTheirDevicesPairing through expect_every_device_record_paired, which the rewrite of this file reuses on the pipeline graph | tests/surface/profiling/test_register_profiler_parts/test_collect_profiling.cpp; modules/surface/profiling/include/SushiRuntime/profiling/device_source.hpp
Medium_CollectProfiling.APlanRunOnTheChainFillsTheSameTable | rewrite | 1 | Forbidden kinds: a for loop with asserts in the body, and a conditional skip. Sole test of the chain-equals-pool table promise. Rewrite with a row helper and no skip | tests/surface/profiling/test_register_profiler_parts/test_collect_profiling.cpp; tests/surface/profiling/test_register_profiler_parts/observed_pipeline.hpp
Medium_CollectProfiling.CarriesNoDropWhenNoneWasLost | split | 3 | two promises: a run that loses nothing carries zero drops (medium, the run is the boundary); it logs nothing (a process-wide log capture, which belongs behind a seam, question 8). The name joins them | tests/surface/profiling/test_register_profiler_parts/test_collect_profiling.cpp; tests/surface/profiling/test_register_profiler_parts/observed_pipeline.hpp
Medium_CollectProfiling.CarriesTheDropCountsOfTheRun | split | 3 | two promises: the drop counts plus the records equal the dispatches (medium); one WARN line per run (process-wide log capture, seam needed, question 8). The name joins them | tests/surface/profiling/test_register_profiler_parts/test_collect_profiling.cpp; tests/surface/profiling/test_register_profiler_parts/observed_pipeline.hpp
Medium_CollectProfiling.TheRunAfterAnOverflowLosesNothing | keep | 9 | collect_profiling: the run after an overflow sizes its ledger for the demand and loses nothing; a real ledger in the boundary | tests/surface/profiling/test_register_profiler_parts/test_collect_profiling.cpp; tests/surface/profiling/test_register_profiler_parts/observed_pipeline.hpp
Medium_CollectProfiling.TaskBodiesOnTheWorkerPoolClaimTheirDevicesPairing | keep | 9 | collect_profiling: a plan of task bodies on the worker pool pairs the device of each record; the helper expect_every_device_record_paired is the one check of this promise | tests/surface/profiling/test_register_profiler_parts/test_collect_profiling.cpp; modules/surface/profiling/include/SushiRuntime/profiling/device_source.hpp
Medium_CollectProfiling.TaskBodiesOnTheChainClaimTheirDevicesPairing | rewrite | 1 | Forbidden kind: a conditional skip in the body (if the plan is not a chain). The chain pairing promise is the same as the worker-pool test with a different config; the rewrite takes the two configs as rows of one test | tests/surface/profiling/test_register_profiler_parts/test_collect_profiling.cpp
```

### `tests/surface/profiling/test_register_profiler_parts/test_device_timing_window.cpp`

```
Medium_DeviceTimingWindow.SpansExistOnlyForTheRecordedPeriods | rewrite | 1 | Forbidden kinds: for loops and an if inside the period callback, and a for loop over the periods with a count. Sole test of the window's span counts. Rewrite with a helper and no control flow in the body | tests/surface/profiling/test_register_profiler_parts/test_device_timing_window.cpp; tests/surface/profiling/test_register_profiler_parts/session_pipeline.hpp
Medium_DeviceTimingWindow.CostsNoRebuild | rewrite | 1 | Forbidden kinds: for loops in the body over the counts and the periods. Its promise (no plan rebuild in a session) is also in test_run_observer.cpp::AddingAnObserverCostsNoRebuild; the span count per period in this test is the same as SpansExistOnlyForTheRecordedPeriods. Rewrite to keep only the no-rebuild check and its helper | tests/surface/profiling/test_register_profiler_parts/test_device_timing_window.cpp; tests/surface/profiling/test_register_profiler_parts/test_run_observer.cpp; tests/surface/profiling/test_register_profiler_parts/session_pipeline.hpp
Medium_DeviceTimingWindow.ARecordMadeWithoutTheSourceHoldsNoDeviceSpans | rewrite | 1 | Forbidden kind: a for loop over the periods in the body. Sole test of the no-source record. Rewrite with a helper that counts spans | tests/surface/profiling/test_register_profiler_parts/test_device_timing_window.cpp; tests/surface/profiling/test_register_profiler_parts/session_pipeline.hpp
Medium_DeviceTimingWindow.APausedSessionCostsNoDeviceTiming | rewrite | 1 | Forbidden kind: a for loop in the body over observations. The name joins pause, ungated observer and resume; the rewrite splits them, one test each | tests/surface/profiling/test_register_profiler_parts/test_device_timing_window.cpp; tests/surface/profiling/test_register_profiler_parts/session_pipeline.hpp
```

### `tests/surface/profiling/test_register_profiler_parts/test_graph_identity.cpp`

```
Medium_GraphLabel.TheTableCarriesTheLabelOfItsGraph | merge | 4 | same promise as AGraphWithNoLabelHasAnEmptyOne and ADynamicGraphCarriesItsLabel (the label reaches the table); one row table in this file | tests/surface/profiling/test_register_profiler_parts/test_graph_identity.cpp; tests/surface/profiling/test_register_profiler_parts/observed_pipeline.hpp; modules/foundation/report/include/SushiRuntime/report/node_table.hpp
Medium_GraphLabel.AGraphWithNoLabelHasAnEmptyOne | merge | 4 | same promise as TheTableCarriesTheLabelOfItsGraph; one row table | tests/surface/profiling/test_register_profiler_parts/test_graph_identity.cpp; tests/surface/profiling/test_register_profiler_parts/observed_pipeline.hpp
Medium_GraphLabel.ALabelChangeAppliesAtTheNextBuildAndForcesNone | resize | 8 | graph_label: the label is a host fact of the plan, and the rebuild counts are planner values. The device is not needed for either; seam: plan label and build counts over a host-only plan, tested small. The medium observer run stays as the boundary check | tests/surface/profiling/test_register_profiler_parts/test_graph_identity.cpp; tests/surface/profiling/test_register_profiler_parts/observed_pipeline.hpp; modules/foundation/report/include/SushiRuntime/report/node_table.hpp
Medium_GraphLabel.ADynamicGraphCarriesItsLabel | merge | 4 | same promise as TheTableCarriesTheLabelOfItsGraph; one row table | tests/surface/profiling/test_register_profiler_parts/test_graph_identity.cpp; modules/foundation/report/include/SushiRuntime/report/node_table.hpp
Medium_NodeOptions.OriginIsCopiedAtRecordTime | resize | 8 | node_options: the origin copy happens when the node is recorded, a host-side act with no device need. Only door: runtime.graph() over a real runtime. Seam: a host-only graph recorder, tested small over the copy | tests/surface/profiling/test_register_profiler_parts/test_graph_identity.cpp; modules/foundation/report/include/SushiRuntime/report/node_table.hpp
Medium_NodeOptions.ANodeThatStatesNoOriginHasNone | rewrite | 1 | Forbidden kind: a for loop over rows with EXPECT in the body. Sole test of the empty origin. Rewrite as one row check through a helper | tests/surface/profiling/test_register_profiler_parts/test_graph_identity.cpp; tests/surface/profiling/test_register_profiler_parts/observed_pipeline.hpp
Medium_GraphId.TwoGraphsOnOneRuntimeObserveTheirOwnIds | merge | 4 | same promise as ADynamicGraphRunCarriesItsOwnId (an observed run carries its graph's id); one row table over the graph kinds | tests/surface/profiling/test_register_profiler_parts/test_graph_identity.cpp; modules/foundation/report/include/SushiRuntime/report/run_observer.hpp
Medium_GraphId.ADynamicGraphRunCarriesItsOwnId | merge | 4 | same promise as TwoGraphsOnOneRuntimeObserveTheirOwnIds; one row table | tests/surface/profiling/test_register_profiler_parts/test_graph_identity.cpp; modules/foundation/report/include/SushiRuntime/report/run_observer.hpp
Medium_GraphId.AnAsyncStepCarriesTheIdOfItsGraph | keep | 9 | graph_id: an async step is observed under its graph's id; the completion arrives on the thread that learns the work is done, so the medium boundary applies | tests/surface/profiling/test_register_profiler_parts/test_graph_identity.cpp; tests/surface/profiling/test_register_profiler_parts/observed_pipeline.hpp
Medium_GraphId.AMovedFromGraphReportsZero | resize | 8 | graph_id: id bookkeeping across a move is host-side; the only door is runtime.graph() over a real runtime (question 8). Seam: a host-only graph id holder, tested small | tests/surface/profiling/test_register_profiler_parts/test_graph_identity.cpp; modules/foundation/report/include/SushiRuntime/report/node_table.hpp
Medium_GraphId.NoGraphHasIdZero | rewrite | 1 | Forbidden kind: a for loop in the body that builds the input. Its promise (distinct, non-zero ids) is partly covered by the NE check in TwoGraphsOnOneRuntime... and the zero check in ADynamicGraphRunCarriesItsOwnId. Rewrite with a builder of several graphs and one expect_ helper | tests/surface/profiling/test_register_profiler_parts/test_graph_identity.cpp
```

### `tests/surface/profiling/test_register_profiler_parts/test_lock_wait.cpp`

```
Medium_PendingMutexLockWait.AnUncontendedLockWritesNone | resize | 8 | lock_wait: whether a zone is written depends on contention, a decision on values. No thread is needed; the front takes its sink through a seam, and the collector is process-wide (question 8). Seam: the front over a zone sink, tested small | tests/surface/profiling/test_register_profiler_parts/test_lock_wait.cpp; modules/foundation/profile/include/SushiRuntime/profile/profile.hpp
Medium_PendingMutexLockWait.AContendedLockWritesAZone | rewrite | 1 | Forbidden kind: control flow in the test body (while loops that spin on the holder thread and on the start flag). The contended write is a real thread boundary and stays medium; rewrite with the waits in a helper | tests/surface/profiling/test_register_profiler_parts/test_lock_wait.cpp; modules/foundation/profile/include/SushiRuntime/profile/profile.hpp
Medium_PendingMutexLockWait.TheGuardReleasesTheMutexAtTheEndOfItsScope | resize | 8 | lock_wait: the release is a host-side fact of the guard, with no thread. It uses the fixture, which opens the process-wide collector, so the test reaches a process-wide instance it does not need (question 8). Seam: the front over a plain mutex, tested small | tests/surface/profiling/test_register_profiler_parts/test_lock_wait.cpp; modules/foundation/profile/include/SushiRuntime/profile/profile.hpp
```

### `tests/surface/profiling/test_register_profiler_parts/test_plan_zones.cpp`

```
Medium_PlanZones.ABuildWritesPlanBuildAroundPlanCompile | rewrite | 1 | Forbidden kinds: a for loop, an if, a continue and an assertion inside the loop in the body. Sole test of the plan zone nesting. Rewrite with a helper that finds the zone parents | tests/surface/profiling/test_register_profiler_parts/test_plan_zones.cpp; tests/surface/profiling/test_register_profiler_parts/session_pipeline.hpp; modules/surface/profiling/source/register_profiler_parts.cpp
Medium_PlanZones.ABuildWritesTheNodeAndRootCountsAsValues | keep | 9 | plan_zones: the graph.nodes and graph.roots values are written by a real build; the profiler write is the boundary | tests/surface/profiling/test_register_profiler_parts/test_plan_zones.cpp; tests/surface/profiling/test_register_profiler_parts/session_pipeline.hpp
Medium_PlanZones.NoZoneIsWrittenWhileTheGateIsClosed | rewrite | 1 | Forbidden kind: for loops in the body over the drained events, with EXPECTs. Sole test of the closed-gate promise. Rewrite with a helper that reads the drained names | tests/surface/profiling/test_register_profiler_parts/test_plan_zones.cpp; tests/surface/profiling/test_register_profiler_parts/session_pipeline.hpp
Medium_PlanZones.ADynamicGraphWritesRegionCommit | keep | 9 | plan_zones: a dynamic graph writes region.commit when it composes its plan; profiler write boundary | tests/surface/profiling/test_register_profiler_parts/test_plan_zones.cpp
```

### `tests/surface/profiling/test_register_profiler_parts/test_registration.cpp`

```
Medium_RegisterProfilerParts.RefusesASecondRegistration | resize | 8 | registration: the refusal is a host decision on the registry's answer. The only door is register_profiler_parts over a concrete Runtime, which probes devices. Seam: register over a context port, tested small; the contributor count of the deleted duplicate moves here | tests/surface/profiling/test_register_profiler_parts/test_registration.cpp; tests/surface/profiling/test_register_profiler_parts/session_pipeline.hpp; modules/surface/profiling/source/register_profiler_parts.cpp; modules/surface/profiling/include/SushiRuntime/profiling/register_profiler_parts.hpp
Medium_RegisterProfilerParts.TheRegistryMakesTheDeviceSource | resize | 8 | registration: the source name is a host fact from the registry; same seam as RefusesASecondRegistration. It stays the one check of the spelling through the registry | tests/surface/profiling/test_register_profiler_parts/test_registration.cpp; tests/surface/profiling/test_register_profiler_parts/session_pipeline.hpp; modules/surface/profiling/source/register_profiler_parts.cpp
Medium_RegisterProfilerParts.ASessionAddsAndRemovesTheObserver | split | 3 | two promises: the observer is added when the source begins, and removed when it ends; the name joins them. The run and timing checks are the boundary and stay medium | tests/surface/profiling/test_register_profiler_parts/test_registration.cpp; tests/surface/profiling/test_register_profiler_parts/session_pipeline.hpp; modules/surface/profiling/source/register_profiler_parts.cpp
Medium_RegisterProfilerParts.TheDestructorRemovesItsOwnObserverOnly | keep | 9 | registration: the destructor removes only the observer it added; the other observer keeps timing | tests/surface/profiling/test_register_profiler_parts/test_registration.cpp; modules/surface/profiling/source/register_profiler_parts.cpp
Medium_RegisterProfilerParts.AFurtherRuntimeAttachesBeforeASessionAndNotDuringOne | split | 3 | two promises: attach succeeds before a session and refuses during one; the name joins them | tests/surface/profiling/test_register_profiler_parts/test_registration.cpp; modules/surface/profiling/source/register_profiler_parts.cpp
Medium_RegisterProfilerParts.RefusedPartsAttachNothing | keep | 9 | registration: refused parts return false from attach; the check of "adds nothing" is not asserted, noted for the next revision | tests/surface/profiling/test_register_profiler_parts/test_registration.cpp; modules/surface/profiling/source/register_profiler_parts.cpp
```

### `tests/surface/profiling/test_register_profiler_parts/test_run_counters.cpp`

```
Medium_RunCounters.TransferBytesMatchTheExplicitCopies | rewrite | 1 | Forbidden kind: control flow in a test body (an if inside the second host node). Sole test of the transfer counters. Rewrite the read into an expect_ helper | tests/surface/profiling/test_register_profiler_parts/test_run_counters.cpp; tests/surface/profiling/test_register_profiler_parts/session_pipeline.hpp; modules/surface/profiling/source/register_profiler_parts.cpp
Medium_RunCounters.ARunThatCopiesNothingWritesZeroBytes | keep | 9 | run_counters: a run that copies nothing writes a zero counter, one per run; profiler write boundary | tests/surface/profiling/test_register_profiler_parts/test_run_counters.cpp; tests/surface/profiling/test_register_profiler_parts/session_pipeline.hpp
Medium_RunCounters.TheTaskCountIsWrittenPerRun | keep | 9 | run_counters: the task count of each run is written, three nodes in three runs | tests/surface/profiling/test_register_profiler_parts/test_run_counters.cpp; tests/surface/profiling/test_register_profiler_parts/session_pipeline.hpp
Medium_RunCounters.WorkerBucketSumsAreWrittenOnEveryRun | rewrite | 1 | Forbidden kind: a for loop over the four bucket names in the body, with EXPECTs. Sole test of the bucket promise. Rewrite as four rows of one expect_ helper | tests/surface/profiling/test_register_profiler_parts/test_run_counters.cpp; tests/surface/profiling/test_register_profiler_parts/session_pipeline.hpp
Medium_RunCounters.AnUnobservedRunReportsNoStaleWorkerTime | rewrite | 1 | Forbidden kinds: a for loop and an if in the body, plus a handle reset inside the run callback. Sole test of the stale-time promise. Rewrite with a helper that reads the value of one period | tests/surface/profiling/test_register_profiler_parts/test_run_counters.cpp; tests/surface/profiling/test_register_profiler_parts/session_pipeline.hpp
```

### `tests/surface/profiling/test_register_profiler_parts/test_run_observer.cpp`

```
Medium_RunObserver.SeesEachBlockingRunOnce | rewrite | 1 | Forbidden kind: for loops in the body (three runs, then records per run, with EXPECTs). Sole test of the record content per run. Rewrite as one expect_ helper per observation | tests/surface/profiling/test_register_profiler_parts/test_run_observer.cpp; tests/surface/profiling/test_register_profiler_parts/observed_pipeline.hpp; modules/foundation/report/include/SushiRuntime/report/run_observer.hpp
Medium_RunObserver.TwoObserversBothReceiveEveryRun | keep | 9 | run_observer: two observers both receive every run | tests/surface/profiling/test_register_profiler_parts/test_run_observer.cpp; tests/surface/profiling/test_register_profiler_parts/observed_pipeline.hpp
Medium_RunObserver.RemovingOneLeavesTheOther | keep | 9 | run_observer: removing one observer leaves the other receiving runs; the removed one hears no later run | tests/surface/profiling/test_register_profiler_parts/test_run_observer.cpp; tests/surface/profiling/test_register_profiler_parts/observed_pipeline.hpp
Medium_RunObserver.IsNotCalledAfterItsHandleIsReset | merge | 4 | same promise as RemovingOneLeavesTheOther (a removed observer hears no later run); merged into it as a second row | tests/surface/profiling/test_register_profiler_parts/test_run_observer.cpp; tests/surface/profiling/test_register_profiler_parts/observed_pipeline.hpp
Medium_RunObserver.ADestroyedHandleRemovesItsObserver | keep | 9 | run_observer: a handle that goes out of scope removes its observer; the destructor path, not reset | tests/surface/profiling/test_register_profiler_parts/test_run_observer.cpp; tests/surface/profiling/test_register_profiler_parts/observed_pipeline.hpp
Medium_RunObserver.AHandleThatOutlivesItsRuntimeRemovesNothing | keep | 9 | run_observer: a handle outliving its runtime does not crash; the oracle is the absence of a fault, under the sanitizers | tests/surface/profiling/test_register_profiler_parts/test_run_observer.cpp; tests/surface/profiling/test_register_profiler_parts/observed_pipeline.hpp
Medium_RunObserver.AnObserverHearsOnlyItsOwnRuntime | keep | 9 | run_observer: an observer is not called by another runtime's runs | tests/surface/profiling/test_register_profiler_parts/test_run_observer.cpp; tests/surface/profiling/test_register_profiler_parts/observed_pipeline.hpp
Medium_RunObserver.SeesAnAsyncStepWhenItsWorkFinishesNotWhenItIsWaitedFor | keep | 9 | run_observer: the observation of an async step arrives from the thread that learns its work is done; the thread hand-over is the medium boundary | tests/surface/profiling/test_register_profiler_parts/test_run_observer.cpp; tests/surface/profiling/test_register_profiler_parts/observed_pipeline.hpp
Medium_RunObserver.AnAsyncStepWithNoObserverStillCompletes | keep | 9 | run_observer: an async step with no observer completes with the expected result (21 at index 10) | tests/surface/profiling/test_register_profiler_parts/test_run_observer.cpp; tests/surface/profiling/test_register_profiler_parts/observed_pipeline.hpp
Medium_RunObserver.TheFirstRunAfterAnAddOfAPlanBuiltBeforeCarriesRecords | keep | 9 | run_observer: a run after an add carries full records though its plan was built earlier; the timing promise of the window test is the same and this one is kept | tests/surface/profiling/test_register_profiler_parts/test_run_observer.cpp; tests/surface/profiling/test_register_profiler_parts/observed_pipeline.hpp; tests/surface/profiling/test_register_profiler_parts/test_device_timing_window.cpp
Medium_RunObserver.AddingAnObserverCostsNoRebuild | keep | 9 | run_observer: adding an observer, the first or a later one, costs no plan rebuild | tests/surface/profiling/test_register_profiler_parts/test_run_observer.cpp; tests/surface/profiling/test_register_profiler_parts/observed_pipeline.hpp
Medium_RunObserver.RemovingTheLastObserverCostsNoRebuild | split | 3 | two promises: removing observers costs no rebuild; the runs after the last removal reach no observer and are untimed; the name joins them | tests/surface/profiling/test_register_profiler_parts/test_run_observer.cpp; tests/surface/profiling/test_register_profiler_parts/observed_pipeline.hpp
Medium_RunObserver.AnObserverAddedMidRunTimesTheNextRunNotTheCurrentOne | rewrite | 1 | Forbidden kind: control flow in the test body (a spin-wait gate with a while and a deadline inside the host node). It also joins three promises: no records for the run in flight, a table still handed over, the next run timed. Rewrite with the gate in a helper and one test per promise | tests/surface/profiling/test_register_profiler_parts/test_run_observer.cpp; tests/surface/profiling/test_register_profiler_parts/observed_pipeline.hpp
Medium_RunObserver.ARunWithNoObserverMakesNoObservation | rewrite | 2 | with no observer attached the test cannot see an observation, so "makes no observation" would not fail if broken (question 2). The promise has no other test. Rewrite: add a counting observer, remove it, then check that the run after the removal is not counted; keep the result check (index 3 holds 7) | tests/surface/profiling/test_register_profiler_parts/test_run_observer.cpp; tests/surface/profiling/test_register_profiler_parts/observed_pipeline.hpp
Medium_RunObserver.ARunOnTheChainPathCarriesTheChainLayer | rewrite | 1 | Forbidden kind: control flow in the body (an if that skips on a non-chain plan). Sole test of the chain layer on an observation. Rewrite so the chain check is a helper over the observation, without the skip | tests/surface/profiling/test_register_profiler_parts/test_run_observer.cpp; tests/surface/profiling/test_register_profiler_parts/observed_pipeline.hpp
Medium_RunObserver.AMultiStepRunIsOneObservationWithEveryStep | rewrite | 1 | Forbidden kind: a for loop in the body that takes the highest invocation. Sole test of the multi-step promise. Rewrite with a helper that returns the highest invocation | tests/surface/profiling/test_register_profiler_parts/test_run_observer.cpp; tests/surface/profiling/test_register_profiler_parts/observed_pipeline.hpp
```

### `tests/surface/profiling/test_register_profiler_parts/test_run_zones.cpp`

```
Medium_RunZones.TheSchedulerPathWritesSubmitAndWaitUnderTheRun | rewrite | 1 | Forbidden kinds: for loops and an if in the body, with ASSERT and EXPECT inside. Sole test of run.submit and run.wait under run.steps. Rewrite with the zone walk in an expect_ helper | tests/surface/profiling/test_register_profiler_parts/test_run_zones.cpp; tests/surface/profiling/test_register_profiler_parts/session_pipeline.hpp; modules/surface/profiling/source/register_profiler_parts.cpp
Medium_RunZones.DispatchZonesAreWrittenAtLevelTwo | rewrite | 1 | Forbidden kind: an if in the body that skips below level 2. Sole test of dispatch zones. Rewrite with the level read as a helper and the skip reported by the runner | tests/surface/profiling/test_register_profiler_parts/test_run_zones.cpp; tests/surface/profiling/test_register_profiler_parts/session_pipeline.hpp
Medium_RunZones.TheStealZoneIsAbsentBelowLevelThree | rewrite | 1 | Forbidden kinds: an if that skips at level 3 and a for loop over the periods. Sole test of the steal-zone absence. Rewrite with a helper | tests/surface/profiling/test_register_profiler_parts/test_run_zones.cpp; tests/surface/profiling/test_register_profiler_parts/session_pipeline.hpp
Medium_RunZones.TheChainPathWritesItsOwnZones | rewrite | 1 | Forbidden kind: an if in the body that skips on a non-chain plan. Sole test of the chain zones. Rewrite without the skip | tests/surface/profiling/test_register_profiler_parts/test_run_zones.cpp; tests/surface/profiling/test_register_profiler_parts/session_pipeline.hpp
Medium_RunZones.WorkerThreadsCarryTheirNames | rewrite | 1 | Forbidden kinds: a for loop and an if in the body. Sole test of the worker names. Rewrite with a helper over the thread list | tests/surface/profiling/test_register_profiler_parts/test_run_zones.cpp; tests/surface/profiling/test_register_profiler_parts/session_pipeline.hpp
Medium_RunZones.TheRunZonesAreOnTheCallersThread | rewrite | 1 | Forbidden kinds: a for loop and an if in the body. Sole test of the caller-thread placement. Rewrite with a helper | tests/surface/profiling/test_register_profiler_parts/test_run_zones.cpp; tests/surface/profiling/test_register_profiler_parts/session_pipeline.hpp
Medium_RunZones.RuntimeWaitWritesWaitRuntimeAroundWaitQueues | rewrite | 1 | Forbidden kinds: a for loop with a continue in the body. Sole test of wait.runtime around wait.queues. Rewrite with a helper | tests/surface/profiling/test_register_profiler_parts/test_run_zones.cpp; tests/surface/profiling/test_register_profiler_parts/session_pipeline.hpp
```

### `tests/surface/profiling/test_register_profiler_parts/test_runtime_session.cpp`

```
Medium_RuntimeSession.RecordsPlanRunAndDeviceSpansOfAGraph | rewrite | 1 | Forbidden kind: for loops in the body with EXPECTs. Sole end-to-end test of the period content. Rewrite with a helper per period | tests/surface/profiling/test_register_profiler_parts/test_runtime_session.cpp; tests/surface/profiling/test_register_profiler_parts/session_pipeline.hpp
Medium_RuntimeSession.JoinsEverySpanToANodeName | rewrite | 1 | Forbidden kinds: for loops, ASSERT and EXPECT in them, and an if in the body. Sole end-to-end test of the span join by name and operation (the table join is in test_collect_profiling.cpp). Rewrite with a helper per span | tests/surface/profiling/test_register_profiler_parts/test_runtime_session.cpp; tests/surface/profiling/test_register_profiler_parts/test_collect_profiling.cpp; tests/surface/profiling/test_register_profiler_parts/session_pipeline.hpp
Medium_RuntimeSession.PutsEachSpanInThePeriodHoldingItsStart | rewrite | 1 | Forbidden kind: for loops in the body. Sole end-to-end test of placement by start time. Rewrite with a helper per period | tests/surface/profiling/test_register_profiler_parts/test_runtime_session.cpp; tests/surface/profiling/test_register_profiler_parts/session_pipeline.hpp
Medium_RuntimeSession.ResolvesEverySubmitThreadToAnEnclosingZone | rewrite | 1 | Forbidden kinds: for loops, a continue and an if in the body. Sole end-to-end test of the submit-thread enclosure. Rewrite with a helper | tests/surface/profiling/test_register_profiler_parts/test_runtime_session.cpp; tests/surface/profiling/test_register_profiler_parts/session_pipeline.hpp
Medium_RuntimeSession.WritesTheCostAttributesWhereACostWasStated | delete | 1 | Forbidden kinds: for loops and if chains in the body. The promise (a stated cost is written as attributes, a stated zero is written, an absent cost writes none) is checked at a smaller size in test_device_source.cpp::Small_DeviceSource.WritesTheCostOfANodeThatStatesOne and WritesAStatedZeroCostAndOmitsAnAbsentOne | tests/surface/profiling/test_register_profiler_parts/test_runtime_session.cpp; tests/surface/profiling/test_device_source.cpp
Medium_RuntimeSession.EverySpanNamesItsLayerAndLiesOnADeviceTrack | rewrite | 1 | Forbidden kind: for loops in the body. The span-on-device-track check against the real runtime is the only integration test of it. Rewrite with a helper | tests/surface/profiling/test_register_profiler_parts/test_runtime_session.cpp; tests/surface/profiling/test_register_profiler_parts/session_pipeline.hpp; modules/surface/profiling/include/SushiRuntime/profiling/device_track_name.hpp
Medium_RuntimeSession.ARunOnTheChainPathNamesTheChainLayer | delete | 1 | Forbidden kinds: for loop and a conditional skip in the body. The chain-layer attribute is checked at a smaller size in test_device_source.cpp::Small_DeviceSource.ANameAttributeNamesTheChainLayer, and the observation's layer is checked in test_run_observer.cpp | tests/surface/profiling/test_register_profiler_parts/test_runtime_session.cpp; tests/surface/profiling/test_device_source.cpp; tests/surface/profiling/test_register_profiler_parts/test_run_observer.cpp
Medium_RuntimeSession.TheRecordStatesTheDevicesAndTheBuild | delete | 1 | Forbidden kind: for loop in the body. The facts are checked by test_runtime_environment.cpp: StatesEveryDeviceOfTheRuntime (rewritten) and StatesTheBuildTypeAsABuildFact (resized) | tests/surface/profiling/test_register_profiler_parts/test_runtime_session.cpp; tests/surface/profiling/test_runtime_environment.cpp
Medium_RuntimeSession.ARunAsyncStepWaitedForLateStillLandsInItsPeriod | rewrite | 1 | Forbidden kind: control flow in the test body (an if with a return inside the period callback, and a spin-wait while). Sole test of the late wait. Rewrite with the spin-wait in a helper | tests/surface/profiling/test_register_profiler_parts/test_runtime_session.cpp; tests/surface/profiling/test_register_profiler_parts/session_pipeline.hpp
Medium_RuntimeSession.ASpanThatFinishesWithinTheMaxDelayLandsInItsPeriodAndIsNotLate | rewrite | 1 | Forbidden kind: control flow in the body (an if and spin-wait loops inside the callback). Sole integration test of the in-delay placement. Rewrite with the gate in a helper | tests/surface/profiling/test_register_profiler_parts/test_runtime_session.cpp; tests/surface/profiling/test_register_profiler_parts/session_pipeline.hpp; modules/surface/profiling/include/SushiRuntime/profiling/device_source.hpp
Medium_RuntimeSession.AStepWhoseWorkFinishedAfterTheLingerIsCountedInTheDrops | rewrite | 1 | Forbidden kinds: control flow in the body, and a wall-clock wait of 2.5 s to hold a period open (the sleep kind). Sole integration test of late drops. Rewrite with a gate on the period, not the clock | tests/surface/profiling/test_register_profiler_parts/test_runtime_session.cpp; tests/surface/profiling/test_register_profiler_parts/session_pipeline.hpp; modules/surface/profiling/include/SushiRuntime/profiling/device_source.hpp
Medium_RuntimeSession.AnInferenceShapedGraphOnTheCpuDeviceRecordsEveryNode | rewrite | 1 | Forbidden kind: for loops in the body. Sole end-to-end test of the inference graph's node records. Rewrite with a helper per period | tests/surface/profiling/test_register_profiler_parts/test_runtime_session.cpp; tests/surface/profiling/test_register_profiler_parts/session_pipeline.hpp
```

### `tests/surface/profiling/test_register_profiler_parts/test_runtime_session_devices.cpp`

```
Large_RuntimeSession.SpansOnTheGpuHaveAPairingUncertaintyBelowTheirDuration | rewrite | 1 | Forbidden kinds: for loops and an if in the body, and a conditional skip without a GPU. The assertion compares a kernel's duration with a pairing uncertainty, a timing relation that can flip on a loaded machine (determinism rule 4). Rewrite with a helper and a stated bound | tests/surface/profiling/test_register_profiler_parts/test_runtime_session_devices.cpp; tests/surface/profiling/test_register_profiler_parts/session_pipeline.hpp
Large_RuntimeSession.TwoDevicesGetOneTrackAndOnePairingEach | rewrite | 1 | Forbidden kinds: for loops in the body and a conditional skip without a second device. Sole test of the two-device tracks. Rewrite with a helper per period | tests/surface/profiling/test_register_profiler_parts/test_runtime_session_devices.cpp; tests/surface/profiling/test_register_profiler_parts/session_pipeline.hpp; modules/surface/profiling/include/SushiRuntime/profiling/device_track_name.hpp
```

### `tests/surface/profiling/test_register_profiler_parts/test_runtime_session_stress.cpp`

```
Medium_RuntimeSessionStress.TwoRuntimesFeedOneSourceFromSeveralThreadsWhileTheConsumerCollects | rewrite | 1 | Forbidden kinds: control flow in the test body (for loops that build graphs, start threads and check spans). The docstring promises that no observation is lost within capacity, but the body checks only failures and well-formed spans, so the loss promise has no check. Rewrite with the thread setup in a helper, medium size kept for the real threads | tests/surface/profiling/test_register_profiler_parts/test_runtime_session_stress.cpp; tests/surface/profiling/test_register_profiler_parts/session_pipeline.hpp; modules/surface/profiling/include/SushiRuntime/profiling/register_profiler_parts.hpp
```

### `tests/surface/profiling/test_register_profiler_parts/test_usm_allocator_zones.cpp`

```
Medium_UsmAllocatorZones.AnAllocationWritesAZoneAndItsSize | split | 3 | two promises: an allocation writes a memory.allocate zone and a size value; the release zone check is a second promise (see the next record) | tests/surface/profiling/test_register_profiler_parts/test_usm_allocator_zones.cpp; tests/surface/profiling/test_register_profiler_parts/session_pipeline.hpp
Medium_UsmAllocatorZones.EachAllocationWritesOneValueOfItsOwnSize | rewrite | 1 | Forbidden kinds: a for loop with an if in the body, counting values. Sole test of per-allocation values. Rewrite with a helper that counts the values of a size | tests/surface/profiling/test_register_profiler_parts/test_usm_allocator_zones.cpp; tests/surface/profiling/test_register_profiler_parts/session_pipeline.hpp
```

### `tests/surface/profiling/test_run_feed.cpp`

```
Small_RunFeed.QueuesEachObservationTaggedWithItsRuntime | merge | 4 | run_feed: a feed queues each observation tagged with its runtime; same promise as TwoFeedsOfOneQueueKeepTheirRuntimesApart; one row table | tests/surface/profiling/test_run_feed.cpp; modules/surface/profiling/include/SushiRuntime/profiling/run_feed.hpp; modules/surface/profiling/include/SushiRuntime/profiling/run_queue.hpp
Small_RunFeed.TwoFeedsOfOneQueueKeepTheirRuntimesApart | merge | 4 | same promise as QueuesEachObservationTaggedWithItsRuntime; one row table | tests/surface/profiling/test_run_feed.cpp; modules/surface/profiling/include/SushiRuntime/profiling/run_feed.hpp
Small_RunFeed.AFeedKeepsItsQueueAlive | rewrite | 1 | Forbidden kind: EXPECT_NO_THROW around on_run, which is noexcept and catches every throw. It is the only test of the lifetime promise. The feed has no accessor, so the promise is seen only as no use-after-free: rewrite as an AddressSanitizer-checked case that drops the owner's handle and calls on_run | tests/surface/profiling/test_run_feed.cpp; modules/surface/profiling/include/SushiRuntime/profiling/run_feed.hpp
```

### `tests/surface/profiling/test_run_queue.cpp`

```
Small_RunQueue.TakesEveryObservationOldestFirst | keep | 9 | run_queue: take() returns every observation oldest first and empties the queue; the second take checks the emptiness the name's "every" implies | tests/surface/profiling/test_run_queue.cpp; modules/surface/profiling/include/SushiRuntime/profiling/run_queue.hpp; modules/surface/profiling/source/run_queue.cpp
Small_RunQueue.DropsTheOldestWhenFullAndCountsIt | rewrite | 1 | Forbidden kind: a for loop in the body that builds input (for runtime < 5 push). Sole test of the drop-oldest promise. Rewrite with a builder for the observations; the rewrite splits the drop promise from the overflow count | tests/surface/profiling/test_run_queue.cpp; modules/surface/profiling/source/run_queue.cpp; modules/surface/profiling/include/SushiRuntime/profiling/run_queue.hpp
Small_RunQueue.ACapacityOfZeroHoldsOne | keep | 9 | run_queue: capacity 0 holds one observation, the newest | tests/surface/profiling/test_run_queue.cpp; modules/surface/profiling/source/run_queue.cpp
Small_RunQueue.EndDropsWhatWasNeverCollected | split | 3 | two promises: discard() returns and drops the untaken count; a discard is not an overflow; one test each | tests/surface/profiling/test_run_queue.cpp; modules/surface/profiling/source/run_queue.cpp
Small_RunQueue.SeveralThreadsMayPushWhileOneTakes | rewrite | 1 | Forbidden kinds: control flow in the body (loops that start producer threads and join them) and a thread in a Small_ suite. The hand-over between threads is a medium boundary (testing, sizes table). Rewrite as Medium_ with the loops moved into a helper, and the no-loss count kept | tests/surface/profiling/test_run_queue.cpp; modules/surface/profiling/include/SushiRuntime/profiling/run_queue.hpp; modules/surface/profiling/source/run_queue.cpp
```

### `tests/surface/profiling/test_runtime_environment.cpp`

```
Medium_RuntimeEnvironment.StatesEveryDeviceOfTheRuntime | rewrite | 1 | Forbidden kind: a for loop over devices with EXPECTs in the body. Sole test of the device facts. Runtime::create probes real devices, so the fact formatting belongs behind a seam (question 8): contribute over a device list, tested small with a stand-in device; the driver "unknown" branch is device-specific | tests/surface/profiling/test_runtime_environment.cpp; modules/surface/profiling/source/runtime_environment.cpp; modules/surface/profiling/include/SushiRuntime/profiling/runtime_environment.hpp
Medium_RuntimeEnvironment.StatesTheVersionAndTheWorkers | resize | 8 | runtime_environment: version, worker count and rebalancer state are host facts formatted from values. Runtime::create is the only door (question 8). Seam: the same contribute-over-values seam as the device facts | tests/surface/profiling/test_runtime_environment.cpp; modules/surface/profiling/source/runtime_environment.cpp; modules/surface/profiling/include/SushiRuntime/profiling/runtime_environment.hpp
Medium_RuntimeEnvironment.StatesTheBuildTypeAsABuildFact | resize | 8 | runtime_environment: build facts are compile-time and need no device, yet the test reaches them through Runtime::create and register_profiler_parts. Seam: a build-facts function that takes the RecordFacts alone; small checks that each build key is set and none is in the environment set | tests/surface/profiling/test_runtime_environment.cpp; modules/surface/profiling/source/runtime_environment.cpp
Medium_RuntimeEnvironment.ARefusedRegistrationStatesNothingTwice | delete | 4 | same promise as tests/surface/profiling/test_register_profiler_parts/test_registration.cpp::Medium_RegisterProfilerParts.RefusesASecondRegistration, the test file of register_profiler_parts, which stays. Its one extra check (registry.contributors().size() == 1) moves into the kept test | tests/surface/profiling/test_runtime_environment.cpp; tests/surface/profiling/test_register_profiler_parts/test_registration.cpp; modules/surface/profiling/source/register_profiler_parts.cpp
```

### `tests/surface/profiling/test_source_names.cpp`

```
Small_SourceNames.TheDeviceSourceIsNamedSushiruntimeDevice | delete | 1 | Forbidden kind: a test of a constant, compared with its literal. The spelling is checked by test_registration.cpp::Medium_RegisterProfilerParts.TheRegistryMakesTheDeviceSource, which compares the registered name with the literal | tests/surface/profiling/test_source_names.cpp; tests/surface/profiling/test_register_profiler_parts/test_registration.cpp; modules/surface/profiling/include/SushiRuntime/profiling/source_names.hpp
Medium_SourceNames.TheSourceTheRegistryMakesCarriesThatName | delete | 4 | same promise as test_registration.cpp::Medium_RegisterProfilerParts.TheRegistryMakesTheDeviceSource (same registry, same name); it adds a GTEST_SKIP when no SYCL device is present, and a skip is not a pass | tests/surface/profiling/test_source_names.cpp; tests/surface/profiling/test_register_profiler_parts/test_registration.cpp
```

### `tests/surface/profiling/test_span_loss_tally.cpp`

```
Small_SpanLossTally.NamesEachCauseWithItsCountInOneLine | keep | 9 | span_loss_tally: each lost cause is named with its count, in cause order, zero counts omitted | tests/surface/profiling/test_span_loss_tally.cpp; modules/surface/profiling/include/SushiRuntime/profiling/span_loss_tally.hpp; modules/surface/profiling/source/span_loss_tally.cpp
Small_SpanLossTally.SaysNothingWhenNoSpanWasLost | keep | 9 | span_loss_tally: no report when no span was lost | tests/surface/profiling/test_span_loss_tally.cpp; modules/surface/profiling/source/span_loss_tally.cpp
Small_SpanLossTally.ReportsAtMostOncePerIntervalAndStartsAgainAfterEach | split | 3 | two promises: at most one report per 5 s steady interval, and a report clears the counts it named; adds between checks make them two tests | tests/surface/profiling/test_span_loss_tally.cpp; modules/surface/profiling/include/SushiRuntime/profiling/span_loss_tally.hpp; modules/surface/profiling/source/span_loss_tally.cpp
```

### `tools/tests/test_check_device_sources.py`

```
PassingTreeTest.test_reports_nothing_when_the_lists_match | rewrite | 1 | control flow: a for loop over two rules in the body; rows of rule replace it; the only test of the matching-lists promise | tools/tests/test_check_device_sources.py; tools/layering/check_device_sources.py; tools/tests/conftest.py
RuleDeviceSourceMissingTest.test_reports_a_unit_that_records_a_kernel_and_is_not_listed | merge | 4 | merge with tools/tests/test_check_device_sources.py::RuleDeviceSourceMissingTest.test_reports_a_unit_whose_quoted_header_launches_a_kernel: same rule, another way a unit instantiates a kernel | tools/tests/test_check_device_sources.py; tools/layering/check_device_sources.py; tools/tests/conftest.py
RuleDeviceSourceMissingTest.test_reports_a_unit_whose_quoted_header_launches_a_kernel | merge | 4 | merge with tools/tests/test_check_device_sources.py::RuleDeviceSourceMissingTest.test_reports_a_unit_that_records_a_kernel_and_is_not_listed: same rule, the kernel reached through a quoted header | tools/tests/test_check_device_sources.py; tools/layering/check_device_sources.py; tools/tests/conftest.py
RuleDeviceSourceMissingTest.test_reads_no_recording_call_in_a_library_source | resize | 8 | a library source is not read for recording calls because the api flag is off outside tests and applications; decided on lines in UnitTree._walk; core is _walk over lines with the flag as a value, seam to build; no size mark | tools/tests/test_check_device_sources.py; tools/layering/check_device_sources.py; tools/tests/conftest.py
RuleDeviceSourceUnneededTest.test_reports_a_listed_unit_with_a_container_add_only | merge | 4 | merge with tools/tests/test_check_device_sources.py::RuleDeviceSourceUnneededTest.test_reports_a_listed_unit_that_reaches_no_api: same rule, another listed unit that instantiates no kernel | tools/tests/test_check_device_sources.py; tools/layering/check_device_sources.py; tools/tests/conftest.py
RuleDeviceSourceUnneededTest.test_reports_a_listed_unit_that_reaches_no_api | merge | 4 | merge with tools/tests/test_check_device_sources.py::RuleDeviceSourceUnneededTest.test_reports_a_listed_unit_with_a_container_add_only: same rule, a plain listed unit | tools/tests/test_check_device_sources.py; tools/layering/check_device_sources.py; tools/tests/conftest.py
```

### `tools/tests/test_check_naming.py`

```
NamespaceDetailTest.test_namespace_detail_reports_lowercase_name | merge | 4 | merge with tools/tests/test_check_naming.py::NamespaceDetailTest.test_namespace_detail_accepts_capitalised_name: same rule, positive and negative inputs | tools/tests/test_check_naming.py; tools/naming/check_naming.py; tools/tests/conftest.py
NamespaceDetailTest.test_namespace_detail_accepts_capitalised_name | merge | 4 | merge with tools/tests/test_check_naming.py::NamespaceDetailTest.test_namespace_detail_reports_lowercase_name: same rule, the capitalised form | tools/tests/test_check_naming.py; tools/naming/check_naming.py; tools/tests/conftest.py
NamespaceDetailTest.test_namespace_detail_ignores_using_directive | merge | 4 | merge with tools/tests/test_check_naming.py::NamespaceDetailTest.test_namespace_detail_reports_lowercase_name: same rule, a using-directive as another negative input | tools/tests/test_check_naming.py; tools/naming/check_naming.py; tools/tests/conftest.py
InterfacePrefixTest.test_interface_prefix_reports_i_prefixed_class | merge | 4 | merge with tools/tests/test_check_naming.py::InterfacePrefixTest.test_interface_prefix_accepts_acronym_names: same rule, positive and negative inputs | tools/tests/test_check_naming.py; tools/naming/check_naming.py; tools/tests/conftest.py
InterfacePrefixTest.test_interface_prefix_accepts_acronym_names | merge | 4 | merge with tools/tests/test_check_naming.py::InterfacePrefixTest.test_interface_prefix_reports_i_prefixed_class: same rule | tools/tests/test_check_naming.py; tools/naming/check_naming.py; tools/tests/conftest.py
GetPrefixTest.test_get_prefix_reports_declared_getter | merge | 4 | merge with tools/tests/test_check_naming.py::GetPrefixTest.test_get_prefix_accepts_call_of_third_party_function: same rule, declaration and call | tools/tests/test_check_naming.py; tools/naming/check_naming.py; tools/tests/conftest.py
GetPrefixTest.test_get_prefix_accepts_call_of_third_party_function | merge | 4 | merge with tools/tests/test_check_naming.py::GetPrefixTest.test_get_prefix_reports_declared_getter: same rule | tools/tests/test_check_naming.py; tools/naming/check_naming.py; tools/tests/conftest.py
ConstantPrefixTest.test_constant_prefix_reports_namespace_constant | merge | 4 | merge with tools/tests/test_check_naming.py::ConstantPrefixTest.test_constant_prefix_accepts_prefixed_constant_and_macro: same rule, positive and negative inputs | tools/tests/test_check_naming.py; tools/naming/check_naming.py; tools/tests/conftest.py
ConstantPrefixTest.test_constant_prefix_accepts_prefixed_constant_and_macro | merge | 4 | merge with tools/tests/test_check_naming.py::ConstantPrefixTest.test_constant_prefix_reports_namespace_constant: same rule | tools/tests/test_check_naming.py; tools/naming/check_naming.py; tools/tests/conftest.py
ConstantPrefixTest.test_constant_prefix_ignores_function_local_static | merge | 4 | merge with tools/tests/test_check_naming.py::ConstantPrefixTest.test_constant_prefix_reports_namespace_constant: same rule, a function-local static as another negative input | tools/tests/test_check_naming.py; tools/naming/check_naming.py; tools/tests/conftest.py
EnumeratorCaseTest.test_enumerator_case_reports_snake_case_enumerator | merge | 4 | merge with tools/tests/test_check_naming.py::EnumeratorCaseTest.test_enumerator_case_accepts_pascal_case_and_acronyms: same rule | tools/tests/test_check_naming.py; tools/naming/check_naming.py; tools/tests/conftest.py
EnumeratorCaseTest.test_enumerator_case_accepts_pascal_case_and_acronyms | merge | 4 | merge with tools/tests/test_check_naming.py::EnumeratorCaseTest.test_enumerator_case_reports_snake_case_enumerator: same rule | tools/tests/test_check_naming.py; tools/naming/check_naming.py; tools/tests/conftest.py
ShortNameTest.test_short_name_reports_short_identifier_in_include_folder | merge | 4 | merge with tools/tests/test_check_naming.py::ShortNameTest.test_short_name_accepts_full_name_and_files_outside_include: same flag rule, positive and negative inputs | tools/tests/test_check_naming.py; tools/naming/check_naming.py; tools/tests/conftest.py
ShortNameTest.test_short_name_accepts_full_name_and_files_outside_include | merge | 4 | merge with tools/tests/test_check_naming.py::ShortNameTest.test_short_name_reports_short_identifier_in_include_folder: same flag rule | tools/tests/test_check_naming.py; tools/naming/check_naming.py; tools/tests/conftest.py
MaskingTest.test_comment_and_string_text_is_not_reported | resize | 8 | the masking is text logic in _blank_comments_and_literals; core is a SourceFile built from text by a text-taking reader, seam to build (_read_source reads the path); no size mark; small | tools/tests/test_check_naming.py; tools/naming/check_naming.py; tools/tests/conftest.py
MaskingTest.test_digit_separator_does_not_open_a_literal | resize | 8 | the same text-taking reader feeds rule_constant_prefix; small once the seam exists; no size mark | tools/tests/test_check_naming.py; tools/naming/check_naming.py; tools/tests/conftest.py
RegistrationTest.test_every_default_rule_is_registered | resize | 8 | the rule table is a set of names and identities; values only; small; no size mark | tools/tests/test_check_naming.py; tools/naming/check_naming.py; tools/tests/conftest.py
RegistrationTest.test_short_name_is_registered_only_behind_the_flag | resize | 8 | the flag table holds short-name and the default table does not; values only; small; no size mark | tools/tests/test_check_naming.py; tools/naming/check_naming.py; tools/tests/conftest.py
```

### `tools/tests/test_check_test_additions.py`

```
CheckTestAdditionsTest.test_a_commit_at_its_limit_passes_and_one_over_it_is_named | delete | 3 | tools/testing/check_test_additions.py was removed from the shared tools upstream: D:/Projects/sushiskills/tools/testing/check_test_additions.py does not exist; this copy and its test are a leftover | tools/tests/test_check_test_additions.py; tools/tests/repo_builder.py; D:/Projects/sushiskills/tools/testing/check_test_additions.py (not found)
CheckTestAdditionsTest.test_a_fix_has_the_smaller_limit | delete | 3 | the removal of tools/testing/check_test_additions.py from the shared tools, as above | tools/tests/test_check_test_additions.py; tools/tests/repo_builder.py; D:/Projects/sushiskills/tools/testing/check_test_additions.py (not found)
CheckTestAdditionsTest.test_a_second_program_needs_the_owners_line | delete | 3 | the removal of tools/testing/check_test_additions.py from the shared tools, as above | tools/tests/test_check_test_additions.py; tools/tests/repo_builder.py; D:/Projects/sushiskills/tools/testing/check_test_additions.py (not found)
CheckTestAdditionsTest.test_the_owners_line_approves_a_second_program | delete | 3 | the removal of tools/testing/check_test_additions.py from the shared tools, as above | tools/tests/test_check_test_additions.py; tools/tests/repo_builder.py; D:/Projects/sushiskills/tools/testing/check_test_additions.py (not found)
CheckTestAdditionsTest.test_a_repository_that_sets_no_base_commit_is_not_checked | delete | 3 | the removal of tools/testing/check_test_additions.py from the shared tools, as above | tools/tests/test_check_test_additions.py; tools/tests/repo_builder.py; D:/Projects/sushiskills/tools/testing/check_test_additions.py (not found)
```

### `tools/tests/test_test_budget_settings.py`

```
TestBudgetSettingsTest.test_the_skill_table_matches_the_settings | rewrite | 1 | control flow: an if guard with skipTest in the body; the skill file is absent in this repository (D:/Projects/sushiruntime/skills/testing/SKILL.md not found), so the test skips on every run here; the only test of the table-to-settings promise | tools/tests/test_test_budget_settings.py; tools/settings.py; skills/testing/SKILL.md (not found)
TestBudgetSettingsTest.test_reads_a_number_with_a_thousands_space_and_a_trailing_note | split | 3 | two behaviours: a number with a thousands space is read and its trailing note dropped (a); rows naming no key or lying outside the Budget table are skipped (b) | tools/tests/test_test_budget_settings.py
TestBudgetSettingsTest.test_names_each_key_that_differs | resize | 8 | differences() decides on two dicts; pure; small suffices; no size mark | tools/tests/test_test_budget_settings.py; tools/settings.py; tools/tests/conftest.py
TestBudgetSettingsTest.test_every_row_of_the_table_has_a_constant | resize | 8 | set equality of two key sets; pure values; small; no size mark | tools/tests/test_test_budget_settings.py; tools/settings.py; tools/tests/conftest.py
```

### `tools/tests/test_tier_table.py`

```
TierTableTest.test_is_registered | resize | 8 | three rule identities in K_CHECKER.rules; values only; small; no size mark | tools/tests/test_tier_table.py; tools/layering/check_layering.py; tools/tests/conftest.py
TierTableTest.test_tier_order_matches_the_cmake_table | resize | 8 | reads cmake/SushiRuntimeTiers.cmake, a file, so medium; no size mark; the comparison of two lists is a value decision, seam: a text-taking parser of SUSHIRUNTIME_TIER_ORDER | tools/tests/test_tier_table.py; tools/settings.py; cmake/SushiRuntimeTiers.cmake; tools/tests/conftest.py
TierTableTest.test_module_folders_match_the_cmake_table | resize | 8 | reads the cmake file and the modules tree, so medium; no size mark; the set comparison is a value decision, seam: a text-taking parser of SUSHIRUNTIME_MODULE_TIERS | tools/tests/test_tier_table.py; cmake/SushiRuntimeTiers.cmake; tools/tests/conftest.py
```

## Adds

Promises a header or a port states that no test checks: `unit: promise | oracle | size`.

```
aliases: a removed spelling is refused at dispatch after its removed_after date (aliases[6]) | exit code 2 with the date injected | medium
diffusion_backend: a non-uniform field moves toward its neighbours (no test of change) | host reference | small
ChainPlan (execution)/classify_chain: a plan whose nodes use memory on two devices is not chainable | classify result on a two-device plan | medium
ChainPlan/classify_chain: a plan that stays off the chain states the reason in not_chainable, which no test reads | string names the reason | medium
RuntimeContext observers: a removed observer hears no later run | notify after remove_run_observer counts zero for that observer | medium
RuntimeContext NUMA: a valid NUMA id gets a queue of a device on that node, not the fallback | device of the returned queue equals the node's device | medium
task_scheduler: a worker count of 0 asks for one worker per logical core | the platform's hardware_concurrency count | medium
thermal_proxy_strategy: a migration moves at least 10 tasks | specified: queues of 25 and 20 move 10 | small
thermal_proxy_strategy: the completion rate is the change since the previous evaluate | metamorphic: two evaluations | small
event_polling: a reaped task releases its successors and retires one unit from the latch | contract: successor count and latch count | medium
event_polling: an entry whose status query throws is reaped as not executed, and its error is returned | contract | medium
Deque: pop drains an overflowing deque to each pushed task, once, on one thread | id set of the pushed tags | small
Cluster: attach refuses a chunk_bytes outside K_MINIMUM_CHUNK_BYTES to K_MAXIMUM_CHUNK_BYTES, naming the value and both limits, and starts no transport (cluster.cpp lines 129 to 134 check it; no test of the four files asserts it) | the error text holds the value and both limits; the transport's start count is 0 | small, once the validation is a value function
Cluster: sharing an address again keeps one allocation for the workers (cluster.hpp, share(void*, bytes), says so; no test of the four files shares one address twice) | one handle with one id after two share calls | small, once the handle table is a value
Cluster: buffer_handle returns nothing once the shared memory is released (cluster.hpp, buffer_handle, says so; ShareRegistersAHostAccessibleHandle covers only addresses never shared) | the optional is empty after release | small, once the handle table is a value
InProcessFabric: set_partitioned drops frames to and from a cut worker while the connection stays open, and a restored link delivers again (in_process_transport.hpp says so; the Nemesis tests use it only under random faults and assert no frame count) | frames delivered before the cut, none during it, and again after the restore | medium
MessageRouter: set_handler() replaces the handler set before for the same type | contract | small
TransportResult: is_ok() is false for every status other than Ok | contract | small
read_environment: returns an empty string for a variable set to nothing | contract | small
StopSignal: destruction restores the previous handler of each signal | contract | small
resubmit_tasks: hands each reaped task to the scheduler once and returns the count handled | contract | small
version: version_matches() is false when the major or the minor differs from the headers | two given versions, no loaded library | small
device_probe: an exception from SYCL device discovery reaches the caller unchanged | a discovery that throws, through a seam over discovery (none exists yet) | small behind the seam; medium without it
error_translation: the message is empty when the text cannot be allocated | a copy that throws std::bad_alloc, through a seam (none exists yet) | small behind the seam
measurement: format_measurement returns one SR_MEASUREMENT line with no trailing newline | the returned string holds no newline | small
measurement: print_measurement writes the format_measurement line and a newline to stdout | captured stdout equals the format_measurement text plus a newline | medium
profile: current_thread_index reads the collector index of a thread that recorded while a session was open | the index the drained batch gives that thread, and not 0 | medium
region: a region open to the end contains a region open to the end that starts later | regions built by hand | small
usm_shared: destruction frees the allocation once | sanitizer (implicit; no in-test oracle without a free seam) | medium
device_queue_set: wait_all() returns KernelFailed with the first asynchronous error, after waiting for every queue | asynchronous error handler on a real queue, checked by the returned error | medium
device_queue_set: raise_asynchronous_errors() reports the first asynchronous error without waiting | asynchronous error handler on a real queue, checked by the returned error | medium
sycl_compat: has_host_task_v is true for a SYCL handler that has host_task | compile-time trait on sycl::handler | small
usm_allocator: owning_device prefers a shared match over a device match, and answers nothing when neither matches | SYCL pointer classification through a port | small
usm_allocator: shares_context reports whether two devices' queues share one SYCL context | SYCL context equality, one shared and one separate | medium
usm_allocator: a release of an unrecorded address on an allocator with no device logs one WARN and frees nothing | captured WARN line | small
usm_allocator: a sycl::free that throws logs one ERROR naming the address and leaves the record in place | captured ERROR line through a queue port | small
epoch_reclamation: a retired object is destroyed exactly once | destroyed counter of one object | small
epoch_reclamation: retire of a null pointer is ignored | nothing destroyed and no slot used | small
numa_lookup: a processing unit of a machine with no NUMA node object yields no node | empty optional; drop the row if hwloc always reports a node | small
observability: read_dispatch_timestamps folds every handed-over event into one span and reads the returned event alone when none is handed over | profiled events read back and folded | medium
topology: pin_current_thread_to_core logs an out-of-range core and changes nothing | captured log line through the log port | medium
hw_discovery: at most one CPU is kept per NUMA node, and a device with no PCI address is always kept | node list through a device-list port | small
hwloc_topology_provider: HwlocDistanceModel returns the SLIT entry of two nodes when the table exists | synthetic hwloc topology with a distance table | medium
hwloc_topology_provider: HwlocDistanceModel answers 10 for local and 21 for remote when no SLIT table exists | synthetic hwloc topology without a distance table | medium
matrix_distance_model: distance of an out-of-range id is 10 when both ids are equal and 21 otherwise | ACPI convention | small
numa_mapper: distance between two different nodes reads the SLIT row after map_distances, and is 21 when the row is absent | synthetic hwloc distance table | medium
numa_mapper: map_distances places a device with a PCI address on the node local to its bus, and a device without one on node 0 | synthetic hwloc bus layout | medium
numa_mapper: map_distances places a CPU device on the node that covers its binding | synthetic hwloc topology | medium
remapping_topology_provider: enumerate gives device i the synthetic node i modulo the node count | oracle is the index | small
remapping_topology_provider: cpu_core_to_numa gives core i the synthetic node i modulo the node count | oracle is the index | small
remapping_topology_provider: a fake node count below one is taken as one | every device on node 0 | small
Launch reach: a reach that ends one below SIZE_MAX is the exact sum, not saturated | contract | small
LateWindow: a window ending exactly at the capacity resolves, one element more is a fault | contract | small
LateCountResolution: a host count equal to the capacity resolves to itself, one above it is a fault | contract | small
Buffer byte count: the largest element count whose byte size fits returns its exact byte size | contract | small
Extent: a SYCL range with an origin stores that origin | contract | small
NdRange: a launch built without an origin has origin zero | contract | small
NodeSpec: a new spec's rule is K_NO_RULE until a recorder sets it | contract | small
NodeStamp: copies the parameter values of the options | contract | small
NodeStamp: copies the cost, the origin and the name the recorder owns | contract | small
parameters_refusal: an InvalidGraph error naming the rule and the counts past the limit (the message content; the OneParameterPastTheLimitIsRefused test checks only the code) | contract | small
Graph::add: a refused add of too many parameters records no node (the graph-level check of the deleted MoreParametersThanANodeCarriesAreRefused, to be kept in a rewritten test) | contract | medium
StepMachinery: before_step runs the live-field updaters, then the binding updaters, then the aggregated ones | contract | small
StepMachinery: after_step advances each written State once per step, once per view | contract | small
StepMachinery: take_fault also reports the faults of aggregated machineries | contract | small
StepMachinery: reset_updaters clears the aggregation and leaves the machinery's own hooks | contract | small
InteropImport: a successful import_device returns a buffer of count elements on an importing device (needs a real external handle, so it cannot run in a placeholder test) | contract | large
PlacementScope::buffer: a Fail refusal allocates nothing | live_allocation_count unchanged across the refused call | medium
capability_cache: two first queries from two threads probe a device once | probe call count after two threads query one device, against an in-memory probe | medium
capability_cache: probe_device_capabilities sets importable_kinds to the kinds SYCL accepts, and device_import only with a UUID and at least one kind | supports_importing_handle_type per kind, and the device aspects | medium
memory_exchange: query_capabilities returns DeviceLost when a sycl exception leaves the probe | the exception table in modules/surface/memory_exchange/README.md | medium
memory_exchange: query_capabilities clears device_import and importable_kinds for a device the context does not count as a GPU | context.devices().is_gpu of the same index | medium
memory_exchange: export_host_memory raises a requested alignment below alignof(max_align_t) to that alignment | address of the allocation modulo alignof(max_align_t), for alignment 1 | medium
memory_exchange: a sycl exception in export_host_memory or import_device_memory leaves as AllocationFailed or InvalidAccess, and none leaves | the exception table in modules/surface/memory_exchange/README.md | medium
memory_exchange: a release step that throws is logged once at ERROR and the remaining release steps still run | records of an in-memory log sink, and the effect of the later steps | medium
run_feed: an observation whose push fails is dropped and on_run does not throw | a push that throws, through a seam | small
clock_conversion: an event time one nanosecond after the steady origin reads 1, not 0 | exact value on the boundary | small
runtime_environment: a device whose driver does not answer reads "unknown" | fact table with a stand-in device, through the seam | small
runtime_environment: each device states its NUMA node | fact table through the seam | small
span_loss_tally: each DropCounts cause maps to its own SpanLoss, including UnreadableEvent and NoEvent, which no test names | exact counts | small
device_source: a span on a device index the runtime lacks is named with the device name "device" | exact track | small
check_device_sources: rule_device_source_missing also checks the sources of BENCHMARK_SOURCES | contract | small
check_device_sources: collect returns a unit tree only for a repository that has a modules folder | contract | small
check_naming: collect skips files under third_party, build, build-asan, build-tsan, venv, node_modules and __pycache__ folders | contract | medium
check_naming: main checks modules when no path is named | contract | medium
```
