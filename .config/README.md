# .config

Configuration a flag can point a tool at. A file its tool finds only in the repository root
stays there.

| File | Tool | Call site that passes the path |
| --- | --- | --- |
| `doxygen/Doxyfile` | Doxygen, run from the repository root | `sr docs` (`cli/sushiruntime/services/project.py`), the documentation job of `.github/workflows/ci.yml` |
| `docker/Dockerfile` | `docker build -f`, with the repository root as the build context | `sr container build` (`cli/sushiruntime/services/docker.py`) |

`.dockerignore` stays in the root: Docker reads it from the build context.
