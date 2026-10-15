# tools

The checkers every Sushi Systems repository carries, copied whole from `tools/` in SushiSkills.
A rule is fixed there first and copied here with `checking/copy_shared_tools.py`; `settings.py`
holds what differs. `naming/`, `packaging/`, `tests/test_check_naming.py` and
`tests/test_tier_table.py` are this repository's own.

| Path | Checks | Takes |
| --- | --- | --- |
| `check_all.py` | Nothing itself; runs the six checkers and the tests, prints only what failed | Repository root |
| `settings.py` | Nothing; holds the settings that differ between repositories | |
| `checking/copy_shared_tools.py` | Nothing; copies the shared tools from SushiSkills, keeping `settings.py`, this README and this repository's own tools, or lists the files that differ with `--report` | Repository root, `--from` |
| `checking/checker.py` | Nothing itself; runs a checker's rule table and prints findings | |
| `checking/ignored_paths.py` | Nothing itself; names the paths git ignores, so a checker reads only what the repository owns | |
| `testing/check_test_mirror.py` | That `tests/` mirrors `modules/`: a test file per public header, no test file that mirrors nothing, no folder without a counterpart | Repository root |
| `documentation/check_source_comments.py` | C++, GLSL, TypeScript and Python: license block, file header, block ceiling, `//` and `#` runs, separators, history words, `@date` | Files or folders |
| `documentation/check_docs_layout.py` | `docs/` entries, required documents, document names, work folders, status lines, design ceiling, links under `docs/`, reachability from `docs/README.md`, READMEs under `modules/<tier>/`, archive candidates | Repository root |
| `documentation/check_changelog.py` | Headings and entry shape, releases kept live, length, one sentence, nesting, cited places | Repository root |
| `layering/check_layering.py` | Declared tiers, upward includes, reaches into another module's `source/`, same-tier includes outside `K_SAME_TIER_EDGES`, includes of a module the `CMakeLists.txt` does not name in `DEPENDS` or `PRIVATE_DEPENDS`, modules it names there and does not include | Repository root |
| `naming/check_naming.py` | C++: `namespace detail`, the I prefix on classes, `get_` declarations, `K_` constants, enumerator case; `--short-names` adds short identifiers in `include/` | Files or folders, default `modules` |
| `licensing/write_license_block.py` | Nothing; writes the license block of `source-comments` into every tracked source file, or lists the files that lack it with `--report` | Repository root, `--project` |
| `packaging/verify_installed_package.py` | That an installed SushiRuntime builds and runs a consumer with only the install prefix on the loader path; `sr test --suite package` runs it | Install prefix |
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
