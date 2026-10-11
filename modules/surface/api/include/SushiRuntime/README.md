# SushiRuntime public headers

This folder holds the umbrella header and the `api/` folder.

| Entry | What it holds |
| --- | --- |
| `SushiRuntime.h` | The umbrella. It includes `core/version.hpp` and `api/fluent.hpp`, and nothing else. |
| `api/` | The fluent v1 API, one folder per concern. |

The sub-folders of `api/`, each with its own page:

| Folder | Page |
| --- | --- |
| `data/` | [data/README.md](api/data/README.md) |
| `detail/` | [detail/README.md](api/detail/README.md) |
| `graph/` | [graph/README.md](api/graph/README.md) |
| `interop/` | [interop/README.md](api/interop/README.md) |
| `launch/` | [launch/README.md](api/launch/README.md) |
| `runtime/` | [runtime/README.md](api/runtime/README.md) |
| `vocabulary/` | [vocabulary/README.md](api/vocabulary/README.md) |

The umbrella includes `core/version.hpp`, which declares the version macros, `runtime_version()` and
the version check. Its documentation is in
[modules/foundation/core/README.md](../../../../foundation/core/README.md), under `version.hpp`.
