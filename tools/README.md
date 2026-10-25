# tools

The checkers every Sushi Systems repository carries, copied whole from `tools/` in SushiSkills.
A rule is fixed there first and copied here with `checking/copy_shared_tools.py`; `settings.py`
holds what differs. `naming/`, `packaging/`, `layering/check_device_sources.py`,
`tests/test_check_naming.py`, `tests/test_check_device_sources.py` and `tests/test_tier_table.py`
are this repository's own.

| Path | Checks | Takes |
| --- | --- | --- |
| `check_all.py` | Nothing itself; runs the eleven checkers, then the repository's own `<area>/check_<what>.py` files and the tests, and prints only what failed | Repository root |
| `settings.py` | Nothing; holds the settings that differ between repositories | |
| `checking/copy_shared_tools.py` | Nothing; copies these files, `.github/CODE_OF_CONDUCT.md` and `.github/SECURITY.md` into a repository, keeping its license blocks, `settings.py`, this README and its own tools, or lists the files that differ with `--report` | Repository root, `--from` |
| `checking/checker.py` | Nothing itself; runs a checker's rule table and prints findings | |
| `checking/ignored_paths.py` | Nothing itself; names the files and folders git ignores, which no checker reads | |
| `testing/check_test_mirror.py` | `tests/` mirrors the source: one `test_<name>` file per header or source file (`<name>.test.ts` in TypeScript), no test without a unit, no folder without a counterpart, no test that includes a header under `source/`; in a Unity repository, `Assets/<Product>/Tests/` with `<Name>Tests.cs` | Repository root |
| `testing/check_test_sizes.py` | Every test names its size in its source: a `Small_`, `Medium_` or `Large_` C++ suite, a size mark on a pytest file, class or function, a `small:` TypeScript `describe` | Repository root |
| `testing/check_probes.py` | Folders named as probes or benchmarks outside `applications/probes/` and `applications/benchmarks/`, such a program using a test framework or added to the test runner, one named after another of its kind, one its README does not list | Repository root |
| `logs/check_logging.py` | Console writes outside the paths `settings.py` allows, empty `catch` and `except: pass`, C++ log calls without their module's category, a logging module without `<module>_log_category.hpp`, Python root-logger, named-logger and f-string records | Repository root |
| `documentation/check_source_comments.py` | C++, GLSL, TypeScript and Python: license block, file header, block ceiling, `//` and `#` runs, separators, history words, `@date` | Files or folders |
| `documentation/check_docs_layout.py` | `docs/` entries, required documents, document names, work folders, status lines in the four forms, design ceiling, links under `docs/`, reachability from `docs/README.md`, READMEs under `modules/<tier>/`, archive candidates by the date in the status line, roadmap, journal, handover, milestone, priority and to-do files | Repository root |
| `documentation/check_changelog.py` | Headings and entry shape, releases kept live, length, one sentence, nesting, cited places | Repository root |
| `documentation/check_backlog.py` | `REMAINING_WORK.md` and `KNOWN_ISSUES.md`: shape, IDs and the counter, paths, references here and in siblings checked out beside the repository, entry size, a backlog entry for every open design document; `--report` lists the siblings read and the entries naming one | Repository root |
| `documentation/design_status.py` | Nothing itself; finds the design documents and work folders and reads their status lines | |
| `documentation/check_design_format.py` | Design documents: the five sections in order, a status line of at most 160 characters, the phase table and its states, no commit hash, no line that dates built or merged work | Repository root |
| `documentation/check_docs_truth.py` | The manual against the source: named symbols, cited paths, example sources, history words, page length, duplicated passages, em dashes, the allow-list | Repository root, `--source` |
| `layering/check_layering.py` | Declared tiers and domains, upward includes from a module or its tests, cross-domain and unlisted same-tier includes, reaches into another module's `source/`, dependencies the build file lists, internal headers in installed ones | Repository root |
| `layering/check_device_sources.py` | The units a declaring call lists in `DEVICE_SOURCES` are exactly those that instantiate a kernel: a `parallel_for` or `single_task` in the unit or a quoted header it includes, or, in a test or an application that includes an API header, a call that records a kernel (`add`, `add_rule`, `kernel`, `resolve`, a `Detail` emitter). The library's sources are read for the first kind only; `api/detail/device_pass.hpp` stops the build of one that records a kernel. Lists built from CMake variables are not read | Repository root |
| `naming/check_naming.py` | C++: `namespace detail`, the I prefix on classes, `get_` declarations, `K_` constants, enumerator case; `--short-names` adds short identifiers in `include/` | Files or folders, default `modules` |
| `licensing/write_license_block.py` | Nothing; writes the license block of `source-comments` into every tracked source file, or lists the files that lack it with `--report` | Repository root, `--project` |
| `packaging/verify_installed_package.py` | That an installed SushiRuntime builds and runs a consumer with only the install prefix on the loader path; `sr test --label package` runs it | Install prefix |
| `routing/summarize_dispatches.py` | Nothing; sums the dispatch lines of every `REPORT.md` per model and effort, for `model-routing` | Repository root |
| `tests/` | Unit tests for the checker rules; run `python -m unittest discover -s tools/tests` | |

`check_docs_layout.py` reads inline Markdown links, with or without a title, wrapped over a
line or in angle brackets. It does not read reference-style links or a link around an image.

`write_license_block.py` is the one tool here that writes files. It takes its licence lines
from `check_source_comments.py`, covers `.in` templates and `.inc` fragments by the file they become, `--closed` for a closed repository, `--skip GLOB` for generated
files and `--upstream PATH=LINE;LINE` for a ported file; a later run without it keeps those lines. A `--report` run that prints nothing
proves a repository's headers are in place.

Every checker takes `--report` and `--rule NAME`, exits 1 on findings, 0 when clean or
reporting, 2 on a bad path. Python 3.11, standard library only.

## Per-repository settings

`settings.py` holds them. `K_TIER_ORDER` lists the tiers, lowest first; it copies
`cmake/SushiRuntimeTiers.cmake`, and `tests/test_tier_table.py` fails when they differ.
`K_MODULE_FUNCTION` names `sushiruntime_add_module`, whose `DEPENDS` and `PRIVATE_DEPENDS` lists
are checked against the includes and are the one list of same-tier edges, so
`K_SAME_TIER_EDGES` stays empty; a module marked `INTERNAL` may not be included by an installed header. `K_CLOSED`
makes `check_source_comments.py` expect the closed licence lines. `check_docs_truth.py` takes
the macro prefixes from the source unless `K_MACRO_PREFIXES` names them.

Every other `K_` constant sits at the top of its checker and is the same in every repository.
