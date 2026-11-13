# Changelog

## Unreleased

## v0.5.2 — 2026-11-13

- 2026-11-13 — deps: Moved the SushiLog pin to v0.3.1, the version SushiBLAS v0.1.1 and SushiAI pin, so `setup` of a consumer of both no longer refuses two SushiLog pins (`cli/sushistack.deps.toml`).
- 2026-11-13 — cli: Held Typer below 0.27 and required sushicore 0.10.16, whose `describe` reads a Typer parameter's Click type that 0.27 no longer gives (`cli/pyproject.toml`).
- 2026-11-13 — ci: Ran the AdaptiveCpp job on Ubuntu 24.04, since apt.llvm.org has no LLVM 17 for the release `ubuntu-latest` moved to (`ci.yml`).
