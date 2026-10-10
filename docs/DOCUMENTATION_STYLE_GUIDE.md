# Documentation style guide

Prose in this repository follows the `humanizer` skill of SushiSkills. Where a document goes
follows the `documentation` skill. Neither is restated here; this page holds what is particular
to SushiRuntime.

## Pages

| Page | Speaks to | Register |
| --- | --- | --- |
| `README.md` at the root | Someone deciding whether to use the runtime | What it is, requirements, the build line |
| `getting_started/INTRODUCTION.md` | A first-time user | Second person, complete programs |
| `guides/` | Someone doing one job: the CLI, integration, a cluster | Second person, commands in order |
| `architecture/ARCHITECTURE.md` | Someone changing the runtime | Present tense, the tree as it is |
| `design/` | Whoever continues the work | The decision, its evidence and its status |

`architecture/ARCHITECTURE.md` names only classes, constants and algorithms that exist in
a module's `include/` or `source/`. A name that leaves the code leaves the page in the same commit.

## Code in a page

- A C++ sample compiles against the current headers. A change that renames a symbol fixes
  every sample that uses it in the same commit.
- A command is written as the `sr` CLI spells it, with the flag names `sr --help` prints.
- A cited file is a path from the repository root in backticks, such as
  `modules/surface/api/include/SushiRuntime/api/graph/graph.hpp`. Section numbers of another page are cited as
  `§9.1` beside a link to that page.

## Doxygen input

`Doxyfile` at the repository root names `docs/reference/API_MAINPAGE.md` and three manual pages in its
`INPUT`. A page that moves or is renamed changes `INPUT` in the same commit. Doxygen writes its
site to `build/docs/api-site/html/`, inside the build tree.

## Words

English, British spelling in prose ("licence" the noun, "serialise"). Identifiers keep the
spelling the code uses. The terms these pages share are defined in
[`reference/GLOSSARY.md`](reference/GLOSSARY.md); a page uses a term the way the glossary
defines it or adds its own row.

## Checking

`python tools/documentation/check_docs_layout.py .` checks the tree, names, status lines, links
and reachability from [`README.md`](README.md). `python tools/documentation/check_changelog.py .`
checks the changelog's shape. Both print nothing when the tree is in shape.
