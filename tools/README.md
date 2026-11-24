# tools

This repository's side of the shared checkers, which run from the sushicore package with
`python -m sushicore.tools check .`:

| Path | Holds |
| --- | --- |
| `settings.py` | The settings the checkers read for this repository |
| `known_findings.toml` | The findings the checkers accept for now, each with its backlog ID |
| `hooks/pre-commit`, `hooks/commit-msg` | The git hooks; `python -m sushicore.tools install-hooks .` points git at them |

The repository's own tools:

| Path | Does |
| --- | --- |
| `layering/check_device_sources.py` | The units a declaring call lists in `DEVICE_SOURCES` are exactly those that instantiate a kernel: a `parallel_for` or `single_task` in the unit or a quoted header it includes, or, in a test or an application that includes an API header, a call that records a kernel (`add`, `add_rule`, `kernel`, `resolve`, a `Detail` emitter). The library's sources are read for the first kind only; `api/detail/device_pass.hpp` stops the build of one that records a kernel. Lists built from CMake variables are not read | Repository root |
| `naming/check_naming.py` | C++: `namespace detail`, the I prefix on classes, `get_` declarations, `K_` constants, enumerator case; `--short-names` adds short identifiers in `include/` | Files or folders, default `modules` |
| `packaging/verify_installed_package.py` | That an installed SushiRuntime builds and runs a consumer with only the install prefix on the loader path; `sr test --label package` runs it | Install prefix |
