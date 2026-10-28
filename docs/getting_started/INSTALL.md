# Install SushiRuntime

SushiRuntime installs in two ways. Both give the same checkout and the same `sr` command, and
neither needs the other: SushiHub is not required to build SushiRuntime, and a checkout made
without it joins a SushiHub workspace later without any change.

## Standalone

You need:

- Git, Python 3.11 or later and CMake 3.20 or later.
- A SYCL 2020 compiler. `sr setup` installs the default one, the intel/llvm bundle, and
  [`CLI_GUIDE.md`](../guides/CLI_GUIDE.md), "Requirements", lists the other two.
- Network access, for the toolchain, hwloc, googletest and the two sibling libraries below.

SushiRuntime builds against SushiLog and SushiProfiler. You do not clone them: `sr setup`
builds their packages at the tags `cli/sushistack.deps.toml` pins, and `sr build` hands each
package to CMake.

```bash
git clone https://github.com/SushiSystems/SushiRuntime.git
cd SushiRuntime
pip install -e ./cli
sr setup
sr build
sr test
```

`pip install` also installs `sushicore`, the command-line layer `sr` shares with the other Sushi
modules, from PyPI at the version `cli/pyproject.toml` pins. `sr setup` writes the paths of this
machine to `cli/config.local.toml`, which git ignores.

## With SushiHub

```bash
hub add sushiruntime            # clone it into the workspace
hub install-cli sushiruntime    # put sr on the path
hub link sushiruntime <path>    # adopt a checkout you already have, instead of hub add
```

`hub link` records where the checkout is and changes nothing in it. `hub install` provisions the
toolchain and the libraries of every module in the workspace once, and `sr` reads the result.
After either route, `sr build` and `sr test` are the commands above.
