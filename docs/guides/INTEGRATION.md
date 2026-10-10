# Integrating SushiRuntime

How to consume SushiRuntime from another project, and the one fact that decides
how your build has to be shaped.

---

## 1. The fact that shapes everything

**The runtime's kernels are header templates. They are instantiated and compiled
in *your* translation units, not in the library.**

When you write:

```cpp
graph.add(Reads(src), Writes(dst), N, [ps, pd](std::size_t i) { pd[i] = ps[i] * 2.0f; });
```

that lambda is a SYCL kernel. It is compiled by *your* compiler, into *your*
object file, and offloaded to the device from *your* binary. The library never
saw it. This is what makes single-source kernels work — you write ordinary C++ in
your own code and it runs on a GPU — and it has three consequences you cannot
design around:

1. **Every translation unit of yours that touches the SushiRuntime graph API must
   be compiled by a SYCL compiler**, with SYCL enabled. A conventional C++
   compiler will not produce device code for your lambda, and the failure is a
   link error or a runtime "kernel not found", not a clear diagnostic.
2. **Your floating-point flags are part of the determinism guarantee.** The
   library cannot promise byte-equal replay for arithmetic it did not compile.
   This is why the exported target carries the FP contract as a *usage
   requirement* — see §4.
3. **Header and binary must come from the same release.** Layout and inline code
   travel with the headers; the rest is in the library. See §5.

This is not a defect to be fixed. It is the cost of single-source, and it is the
single most important thing an integrator needs to know.

---

## 2. The two-lane pattern

You do not have to compile your whole project with a SYCL compiler, and you
should not want to: SYCL compilers are slower, and most of a codebase never
touches a device.

Split your build into two lanes:

- **The SYCL lane** — the subsystems that record graphs and write kernels. These
  link `SushiRuntime::SushiRuntime` and compile under the SYCL toolchain.
- **The ordinary lane** — everything else. Rendering, audio, asset loading, tools.
  These link nothing from SushiRuntime, include none of its headers, and compile
  under your normal compiler.

The boundary between them is an ordinary C++ interface of your own: the SYCL lane
exposes plain functions and POD types, and the ordinary lane calls them. Nothing
of SushiRuntime crosses it.

Keep the SYCL lane as small as it can be. It is where your build time goes.

---

## 3. Consuming the package

### From an installed prefix

```bash
# In the runtime's tree
sr build
cmake --install build --prefix /opt/sushiruntime

# In your project
cmake -S . -B build -DCMAKE_PREFIX_PATH=/opt/sushiruntime
```

```cmake
find_package(SushiRuntime REQUIRED)

add_library(my_simulation STATIC simulation.cpp)     # a SYCL-lane target
target_link_libraries(my_simulation PRIVATE SushiRuntime::SushiRuntime)
```

`tests/package/` in this repository is a complete, working example of exactly
this — two files, no access to the runtime's source tree. It is also where the
flag-hygiene assertion of §4 lives.

### From a sibling checkout

`add_subdirectory(path/to/sushiruntime)` also works and gives you the same
`SushiRuntime::SushiRuntime` target, so switching between the two later costs no
edits to your link lines. Prefer the package for anything you intend to pin: a
checkout is a convention, an installed versioned artifact is not.

### Version compatibility

The package is written with `SameMinorVersion` compatibility while the project is
pre-1.0: against the current 0.3.0 release,
`find_package(SushiRuntime 0.3 REQUIRED)` accepts 0.3.x and rejects 0.4.0,
because the API is not frozen and a minor bump may break you. At the
v1.0.0 freeze this becomes `SameMajorVersion`.

---

## 4. What you inherit, and what you must supply

Linking `SushiRuntime::SushiRuntime` gives you:

| Inherited | Why |
|---|---|
| Include directories | Obviously. |
| The library itself | Obviously. |
| The floating-point contract (`SR_DETERMINISTIC_FP`, `/fp:precise` or `-ffp-contract=off -fno-fast-math`) | Your translation units compile the runtime's kernel templates; byte-equal replay is only true if they compile under the same FP semantics. |

You do **not** inherit the runtime's warnings, `-Werror`, sanitizer selection, or
`-march`. Those are that project's development choices, and a dependency
silently imposing them on you is a build failure whose cause is invisible from
your own source tree. `tests/package/CMakeLists.txt` asserts this from the
consumer side, which is the only side it is observable from.

You must supply:

- **A SYCL compiler**, configured. The package will use an existing
  `IntelSYCL::SYCL_CXX` target if your project already set one up, and otherwise
  tries `find_package(IntelSYCL)`. If you are on a different SYCL toolchain,
  configure it yourself before `find_package(SushiRuntime)`.
- **C++17 or later.**
- **The runtime's shared library on the loader path** at run time, plus whatever
  your SYCL implementation needs (adapter libraries, an OpenCL CPU runtime for a
  host lane). See the redistributable notes in `README.md`.

---

## 5. Checking the binary you actually loaded

```cpp
#include <SushiRuntime/version.hpp>

if (!SushiRuntime::version_matches())
{
    // Headers and library came from different releases.
    std::fprintf(stderr, "SushiRuntime %s expected, %s loaded\n",
                 SUSHIRUNTIME_VERSION_STRING,
                 SushiRuntime::runtime_version_string());
    return 1;
}
```

`SUSHIRUNTIME_VERSION_*` are the version of the **headers you compiled against**;
`runtime_version()` reports the version the **loaded library was built from**.
They are different questions, and the interesting case is when they disagree —
usually an older shared library found first on the loader path. Left unchecked,
that surfaces as a crash or a wrong answer at an arbitrary later point with
nothing pointing back at the cause; checked at startup, it is one line of output.

You can also branch at compile time:

```cpp
#if SUSHIRUNTIME_VERSION >= SUSHIRUNTIME_VERSION_ENCODE(0, 2, 0)
    // use something introduced in 0.2.0
#endif
```

---

## 6. Configuration a co-tenant should set

The defaults assume the runtime is the only thing on the machine. If your process
has its own threads that matter — a render thread, an audio callback, a job
system — tell the runtime so:

```cpp
API::RuntimeConfig config;
config.first_core   = 4;                 // cores 0-3 are yours
config.worker_count = 8;                 // take eight of what is left
config.pinning      = API::PinPolicy::Pin;
config.rebalancer   = false;             // the default; leave it off for a frame loop

auto runtime = API::Runtime::create({}, config);

// Verify what you actually got — a request that does not fit is clamped, not obeyed.
const auto pool = runtime.advanced().worker_pool();
```

And in the loop itself, keep one report and hand it back every tick so the tick
allocates nothing:

```cpp
Core::RunReport report;
while (running)
    simulation.run(report);
```

A live element count that changes every tick (a grid's per-cell occupancy, a contact
list) is a `LateCount`: planned once for a capacity and resolved per run, from the host or
from a slot a predecessor kernel wrote. A launch sized by it is recorded through the
graph's recorder:

```cpp
API::LateCount n;
n.capacity = MAX_CELLS;
n.host = [&] { return live_cell_count; };
// SushiBLAS's prefix scan and counting sort take a LateCount this way.
```

See [`ARCHITECTURE.md`](../architecture/ARCHITECTURE.md) §9.1 for what the pool guarantees and §3 for the run API.

Per-node device timing (`RunReport::node_events`) is opt-in per graph, not
runtime-wide: call `graph.set_profiling_requested(true)` (or its
`DynamicGraph` counterpart) only on the graph you actually want to inspect. A
graph that never asks for it never touches the profiling-enabled queue that
carries the cost — measured at about 24 microseconds more per submission than
the plain queue on the RTX 3080 Ti CUDA adapter tested — so leaving other graphs on the
same runtime unrequested keeps them at their normal dispatch cost.

---

## 7. Troubleshooting

**Link errors on kernel symbols, or "kernel not found" at run time.** A
translation unit that touches the graph API was compiled without SYCL. See §1.

**Replay is not byte-equal between two builds.** Check that
`SushiRuntime_DETERMINISTIC_FP` is `ON` in the package you consumed, and that
your own build does not add fast-math on top. The runtime's
`Integration_Determinism` suite is the in-tree version of this check.

**`find_package` cannot find `IntelSYCL`.** Configure your SYCL toolchain before
`find_package(SushiRuntime)`, or point `CMAKE_PREFIX_PATH` at your oneAPI
installation. The package deliberately does not guess at a toolchain for you.

**A build failure mentioning `-Werror` or `-march`.** That would be a leak of the
runtime's private flags into the export set — a defect on our side, not yours.
`tests/package/` asserts against it; please report it.
