# tools

The checkers every Sushi Systems repository carries, copied whole from `tools/` in SushiSkills.
A rule is fixed there first and copied here; `K_` settings are the only local changes.
`packaging/` is this repository's own.

| Path | Checks | Takes |
| --- | --- | --- |
| `common/checker.py` | Nothing itself; runs a checker's rule table and prints findings | |
| `documentation/check_source_comments.py` | C++, GLSL, TypeScript and Python: license block, file header, block ceiling, `//` and `#` runs, separators, history words, `@date` | Files or folders |
| `documentation/check_docs_layout.py` | `docs/` entries, required documents, document names, work folders, status lines, design ceiling, links under `docs/`, reachability from `docs/README.md`, READMEs under `modules/<tier>/`, archive candidates | Repository root |
| `documentation/check_changelog.py` | Headings and entry shape, releases kept live, length, one sentence, nesting, cited places | Repository root |
| `layering/check_layering.py` | Declared tiers, upward includes, reaches into another module's `source/`, same-tier includes outside `K_SAME_TIER_EDGES`, includes of a module the `CMakeLists.txt` does not name in `DEPENDS` or `PRIVATE_DEPENDS` | Repository root |
| `naming/check_naming.py` | C++: `namespace detail`, the I prefix on classes, `get_` declarations, `K_` constants, enumerator case; `--short-names` adds short identifiers in `include/` | Files or folders, default `modules` |
| `licensing/write_license_block.py` | Nothing; writes the license block of `source-comments` into every tracked source file, or lists the files that lack it with `--report` | Repository root, `--project` |
| `packaging/verify_installed_package.py` | That an installed SushiRuntime builds and runs a consumer with only the install prefix on the loader path; `sr test --suite package` runs it | Install prefix |
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

`K_` constants at the top of each checker. `check_layering.py` does nothing until
`K_TIER_ORDER` lists the repository's tiers, lowest first. `K_TIER_ORDER` and
`K_SAME_TIER_EDGES` copy `cmake/SushiRuntimeTiers.cmake`, and `tests/test_tier_table.py` fails
when they differ. The two rules that read `K_SAME_TIER_EDGES` and the module `CMakeLists.txt`
files are this repository's own; they cover `include/` and `source/`, since a module's tests
link the whole library. `K_COPYRIGHT_LINE` and
`K_LICENSE_LINES` in `check_source_comments.py` hold the repository's licence lines; a closed
repository sets `K_LICENSE_LINES` to `("All rights reserved. No licence is granted.",)`.
