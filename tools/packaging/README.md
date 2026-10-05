# tools/packaging

This repository's own tool, beside the shared checkers that `tools/README.md` lists.

## verify_installed_package.py

The script is the packaging oracle: it proves that an installed SushiRuntime is usable from
outside its tree.

Two things can be wrong with a package, and only one of them shows up in an ordinary build.
When `find_package` resolves and a consumer compiles, the export set and the headers are
right. That says nothing about whether the installed tree carries the libraries the consumer
loads at run time, because the developer machine's `PATH` already holds the SYCL toolchain and
the vcpkg tree. A check that inherits that environment passes whether or not the packaging
works.

So the consumer, `tests/package/`, is built with the toolchain environment, which it needs,
and then run with a scrubbed one that sees only Windows and the install prefix. The run is the
part that fails when a redistributable is missing.

The script refuses a prefix that holds a `CMakeCache.txt`. A build tree is not an install
tree, and pointing the script at one would test nothing: the build tree's exported targets
reference the source, and the binaries sit next to every dependency that produced them.
