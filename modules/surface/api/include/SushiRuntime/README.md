# SushiRuntime public headers

This folder holds the umbrella header and the `api/` folder.

| Entry | What it holds |
| --- | --- |
| `sushiruntime.hpp` | The umbrella. It includes `api/fluent.hpp` and eight headers of `core/`: assertions, errors, results, the operation id, reference counting, the intrusive pointer, spans and the version. |
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

The core headers are documented in
[modules/foundation/core/README.md](../../../../foundation/core/README.md), each under its own name;
`version.hpp` declares the version macros, `runtime_version()` and the version check.
