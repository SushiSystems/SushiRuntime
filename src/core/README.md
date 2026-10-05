# core

Sources of the `Core` layer that need a translation unit. The headers are in
`include/SushiRuntime/core/`.

## version.cpp

`runtime_version()` and `runtime_version_string()` read the same `SUSHIRUNTIME_VERSION_*`
macros a consumer sees in `version.hpp`, but the values are frozen when the library is
compiled. A consumer that compares its own macros with these two functions is comparing two
builds. They agree only when the binary it loaded is the one its headers describe.
