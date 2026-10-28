# Repository hygiene

## Remaining work

| ID | Pri | State | Title | Done when | Blocked by | Source | Opened |
| --- | --- | --- | --- | --- | --- | --- | --- |
| RW-54 | P2 | open | Add `.clang-format` and `.editorconfig` | the root holds both files |  |  | 2026-10-19 |
| RW-55 | P2 | open | Bring the ignore rules up to date | `.gitignore` drops `dependencies/` and `docs/html` and names `.superpowers/`, `.pytest_cache/` and `.venv/` |  |  | 2026-10-19 |
| RW-56 | P2 | open | Configure from the build folder | `cli/sushiruntime/services/project.py` runs the configure and compile steps with the build folder as working directory, so no probe file lands in the root |  |  | 2026-10-19 |
| RW-63 | P2 | unmeasured | Run the GPU detection's acceptance on a CUDA machine | `sr build` reports `SR_GPU_BACKEND_RESOLVED=cuda` and the NVIDIA topology test runs and passes |  |  | 2026-10-19 |
| RW-64 | P2 | open | Take the build environment from sushicore | `cli/sushiruntime/` sets its build environment through `sushicore.build_env`, not a copy of it in `env.py` |  |  | 2026-10-19 |

## Known issues

| ID | Origin | Symptom | Workaround | Fix | Source | Opened |
| --- | --- | --- | --- | --- | --- | --- |
