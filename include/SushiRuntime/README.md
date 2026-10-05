# SushiRuntime public headers

The public headers of SushiRuntime, one folder per layer. This page holds the reasoning behind the
two headers at this level; each folder below has its own page.

| Folder | Page |
| --- | --- |
| `api/` | [api/README.md](api/README.md) |
| `core/` | [core/README.md](core/README.md) |
| `distributed/` | [distributed/README.md](distributed/README.md) |
| `execution/` | [execution/README.md](execution/README.md) |
| `graph/` | [graph/README.md](graph/README.md) |
| `memory/` | [memory/README.md](memory/README.md) |
| `scheduler/` | [scheduler/README.md](scheduler/README.md) |
| `topology/` | [topology/README.md](topology/README.md) |

The sections below record why the code is the way it is. Each one is named after a header and the
symbol or passage it explains, and that header's file block cites this page.

## version.hpp

### Headers and the loaded library

version.hpp gives the library's version in both of the forms a consumer needs. The macros are the
version of the headers being compiled against; runtime_version() reports the version the loaded
library was built from. Those are two different questions, and a consumer that pins by tag needs
both to catch the case where they disagree. A stale shared library found first on the loader path
otherwise fails silently and arbitrarily late, when it could have failed a startup check.
version_matches() is that check.

The literals in version.hpp are checked against the project's CMake version at configure time, so
the header and the build cannot drift apart.

### runtime_version

runtime_version() is compiled into the library, not into the caller, so it answers "which binary am
I actually running against" and not "which headers did I compile against".

### version_matches

version_matches() is the check a consumer should make once at startup. It returns true when the
loaded library is compatible with these headers. A mismatch means the headers and the binary came
from different releases; the classic cause is an older shared library earlier on the loader path.
Without the check the failure is a crash or a wrong answer at an arbitrary later point, with nothing
pointing back at the real cause.

It compares major and minor only: a patch release is defined not to change layout or behaviour, so
refusing to run against one would reject a legitimate upgrade.
