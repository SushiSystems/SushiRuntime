# tests/package

A consumer project that builds against an installed SushiRuntime with no access to the
runtime's source tree. `tools/packaging/verify_installed_package.py` configures, builds and
runs it; `sr test --label package` runs that script.

## main.cpp

The program is the smallest thing that proves the package works: one kernel, recorded and run
against the installed library. It is small on purpose. Anything more would test the runtime,
which the runtime's own suite does. What is under test here is the package: that the headers
are found, that the kernel template compiles in this project's translation unit under the
toolchain the consumer chose, that the library links, and that the version the headers
describe is the version that loaded.

`version_matches()` is the check every consumer should make once at startup. Headers and a
binary from different releases are the classic packaging failure, and without the check it
surfaces as a crash somewhere unrelated and much later.
