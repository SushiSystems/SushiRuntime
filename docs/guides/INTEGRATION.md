# Integrating SushiRuntime

How another project consumes SushiRuntime: what the build must look like, what the package
gives you, and how to keep the library behind one module of your own.

## 1. Kernels compile in your project

A kernel is a template in an installed header. Your compiler instantiates it, in your
translation unit, and your binary offloads it to the device.

```cpp
    graph.add_rule(heat, [](std::size_t i, const float* cur, float* next)
    {
        const float left  = (i == 0)     ? cur[i] : cur[i - 1];
        const float right = (i == N - 1) ? cur[i] : cur[i + 1];
        next[i] = cur[i] + 0.25f * (left - 2.0f * cur[i] + right);
    });
```

Source: `tests/package/main.cpp`

The library never sees that lambda. Three things follow.

1. Every translation unit that records a kernel is compiled by a SYCL compiler with SYCL
   enabled. A conventional compiler produces no device code for the lambda. Under DPC++, a unit
   compiled without `-fsycl` that records a kernel through the API fails to compile on a
   `static_assert` that names the flag.
2. Your floating-point flags decide whether a replay is byte-equal. The library cannot promise
   that for arithmetic it did not compile, so the exported target carries the flags; see §4.
3. The headers and the shared library come from one release. A `Detail` type that crosses into
   the library is read by its layout on both sides; see §5.

## 2. Two lanes

A SYCL compiler is slower than a conventional one, and most of a codebase never touches a
device. Split the build:

- **The SYCL lane** holds the code that records graphs and writes kernels. It links
  `SushiRuntime::SushiRuntime` and compiles under the SYCL toolchain.
- **The ordinary lane** holds everything else. It includes no header of SushiRuntime and
  compiles under your usual compiler.

The boundary between the lanes is an interface of your own, in your own types. §6 describes the
module that implements it.

## 3. Consuming the package

Install the built tree to a prefix from this repository. `sr package` installs to `build/dist`
unless `--prefix` names another directory:

```bash
sr build
sr package --no-archive
```

Then, in your project:

```cmake
find_package(SushiRuntime REQUIRED)

add_executable(sushiruntime_package_consumer main.cpp)

target_link_libraries(sushiruntime_package_consumer PRIVATE
    SushiRuntime::SushiRuntime
)

set_target_properties(sushiruntime_package_consumer PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
)
```

Source: `tests/package/CMakeLists.txt`

Configure with `CMAKE_PREFIX_PATH` set to the install prefix
and with the same SYCL compiler as `CMAKE_CXX_COMPILER`. `tests/package/` is a complete consumer
with no access to this source tree, and `sr test --label package` installs the library and
builds and runs it.

`add_subdirectory(path/to/sushiruntime)` defines the same `SushiRuntime::SushiRuntime` target.
Under a parent project the examples are not built (`SR_BUILD_EXAMPLES` is off there).

### One target per module

`SushiRuntime::SushiRuntime` links every public module. A consumer that uses only some of them
links those instead, and sees only their headers and the headers of the modules they depend on:

| Target | Module |
| --- | --- |
| `SushiRuntime::core` | Assertions, `Error`, `Result` and the shared value types |
| `SushiRuntime::log` | The front over SushiLog; links `SushiLog::SushiLog` |
| `SushiRuntime::machine`, `SushiRuntime::region`, `SushiRuntime::report` | The machine, region and report types |
| `SushiRuntime::interop`, `SushiRuntime::context` | The interop and context headers |
| `SushiRuntime::api` | The surface: the runtime, graphs and kernels |
| `SushiRuntime::profiling` | The runtime's SushiProfiler source and `register_profiler_parts()`; present when the library was built with `SR_ENABLE_PROFILER`; links `SushiProfiler::SushiProfiler` |
| `SushiRuntime::transport`, `SushiRuntime::cluster_load`, `SushiRuntime::distributed` | The distributed layer |

A module with compiled code links the shared library; a header-only one, such as `log`, does
not. The installed package and the build tree export the same targets.

### Version compatibility

The package file declares `SameMinorVersion` compatibility: `find_package(SushiRuntime 0.4)`
accepts 0.4.x and rejects 0.5.0. SushiRuntime is below 1.0.0, and its public interface is not
stable:

- A MINOR release (0.4, 0.5) may change or remove any public name, signature, header path or
  binary layout. A PATCH release does not.
- Pin an exact tag, such as `v0.4.0`. Do not follow a branch.
- Reach SushiRuntime through one adapter of your own, as
  [section 6](#6-write-one-adapter) shows. When a release breaks the interface, that module is the
  only place you edit.

## 4. What you inherit and what you supply

Linking `SushiRuntime::SushiRuntime` gives a target:

| Inherited | Why |
| --- | --- |
| The include directories of the public modules | The headers a consumer includes |
| The shared library | The compiled part of the surface and the engine |
| The floating-point flags, when the library was built with `SR_DETERMINISTIC_FP` on | `/fp:precise` under an MSVC-style driver, `-ffp-contract=off -fno-fast-math` under a GNU-style one. Your translation units compile the kernel templates, and a replay is byte-equal only when they compile under the same floating-point rules |

A target does not inherit the library's warnings, `-Werror`, sanitizers or `-march`.
`tests/package/CMakeLists.txt` fails its configure step when one of them appears in the
exported interface.

The package sets two variables a consumer can read: `SushiRuntime_DETERMINISTIC_FP` and
`SushiRuntime_DISTRIBUTED`. A consumer that links `SushiRuntime::profiling` links the same
SushiProfiler library as the runtime, its shared library, so the process has one collector;
[the profiling guide](PROFILING_GUIDE.md) says why.

You supply:

- **A SYCL compiler.** The package uses the target `IntelSYCL::SYCL_CXX` when your project
  already defined it and otherwise calls `find_package(IntelSYCL QUIET)`. When neither gives
  the target, the package defines an empty one, so a project on another SYCL toolchain
  configures that toolchain itself.
- **C++17 or later.**
- **The shared library on the loader path at run time**, with what your SYCL implementation
  needs beside it.

## 5. Check the library you loaded

```cpp
    // Headers and library must come from one release; see tests/package/README.md.
    if (!SushiRuntime::version_matches())
    {
        std::printf("FAIL: headers are %s, loaded library is %s\n",
                    SUSHIRUNTIME_VERSION_STRING, SushiRuntime::runtime_version_string());
        return 1;
    }
```

Source: `tests/package/main.cpp`

`SUSHIRUNTIME_VERSION_STRING` is the version of the headers you compiled against.
`runtime_version_string()` is the version the loaded library was built from. They differ when an
older shared library is earlier on the loader path. `version_matches()` compares the major and
the minor number.

`Runtime::create()` makes the same comparison and stops the process on a mismatch, with a
message that names both versions. A program that wants to report the mismatch itself calls
`version_matches()` first, as above.

## 6. Write one adapter

The interface changes with every MINOR release below 1.0.0. Keep the cost of that in one place:
a single module of your project that includes `<SushiRuntime/sushiruntime.hpp>`, and that the
rest of your code calls through your own types.

The adapter is the SYCL lane of §2, or the part of it that names the library.
`applications/examples/adapter/` is a complete one, built as `sr_adapter_demo`: a host program
that needs a field which advances by a rule and can be read back. Every block of this section
is from its three files, and
[the examples README](../../applications/examples/README.md) describes the program.

**What it includes.** `<SushiRuntime/sushiruntime.hpp>`, and
`<SushiRuntime/distributed/distributed.hpp>` when it attaches a cluster. No other header of the
library is needed, and no header of the engine is installed. In the example one file of three
includes it.

**What it exposes.** Functions and types of your project: a function that creates the backend,
a function that runs steps, your own status type. The header the ordinary lane includes names
no type of `SushiRuntime`. It declares the backend and does not define it, so the host holds
the backend by a handle and cannot look into it:

```cpp
    /** @brief Stands for the backend; the adapter alone defines it. */
    struct DiffusionBackend;
    // ...
    /** @brief Owns a backend the host cannot look into. */
    using BackendHandle = std::unique_ptr<DiffusionBackend, BackendDeleter>;

    /** @brief Creates a backend on the devices of this machine. */
    [[nodiscard]] Status create_backend(BackendHandle& backend);
    // ...
    /** @brief Advances the field by a number of steps. */
    [[nodiscard]] Status advance(DiffusionBackend& backend, std::size_t steps);
```

Source: `applications/examples/adapter/diffusion_backend.hpp`

The adapter defines that type from the handles of the library:

```cpp
    struct DiffusionBackend final
    {
        // ...
        SushiRuntime::Runtime runtime;    /**< The runtime every handle below came from. */
        SushiRuntime::State<float> field; /**< The field; empty until allocate_field(). */
        SushiRuntime::Graph graph;        /**< Holds the one rule that steps the field. */
    };
```

Source: `applications/examples/adapter/sushiruntime_adapter.cpp`

**How it turns a `Result` into your status type.** One function maps every `ErrorCode` to a
code of yours. With no `default` in the `switch`, a compiler that warns on an unhandled
enumerator (`-Wswitch`) reports a code a new release added:

```cpp
        /** @brief Returns the host's status for an error of the library. */
        Status status_of(const SushiRuntime::Error& error)
        {
            using SushiRuntime::ErrorCode;
            switch (error.code)
            {
                case ErrorCode::AllocationFailed:
                    return Status{StatusCode::OutOfMemory, error.message};
                case ErrorCode::KernelFailed:
                case ErrorCode::DeviceLost:
                    return Status{StatusCode::DeviceFailed, error.message};
                case ErrorCode::InvalidGraph:
                case ErrorCode::InvalidAccess:
                case ErrorCode::InvalidState:
                case ErrorCode::InvalidArgument:
                    return Status{StatusCode::InvalidRequest, error.message};
                case ErrorCode::Unsupported:
                    return Status{StatusCode::Unsupported, error.message};
                case ErrorCode::CompileFailed:
                case ErrorCode::DanglingResource:
                case ErrorCode::ProtocolMismatch:
                    return Status{StatusCode::BackendFault, error.message};
            }
            return Status{StatusCode::BackendFault, error.message};
        }

        /** @brief Returns Ok for a result that holds a value, and the mapped error otherwise. */
        template <typename T>
        Status status_of(const SushiRuntime::Result<T>& result)
        {
            return result ? Status() : status_of(result.error());
        }
```

Source: `applications/examples/adapter/sushiruntime_adapter.cpp`

Every call into the library then ends in one of the two, so no `Result` leaves the file:

```cpp
    Status advance(DiffusionBackend& backend, std::size_t steps)
    {
        return status_of(backend.graph.run(steps));
    }
```

Source: `applications/examples/adapter/sushiruntime_adapter.cpp`

The library throws nothing, so the adapter needs no `try`. An adapter that also calls a
library which throws catches at the same boundary and returns a status of its own.

**What it checks itself.** A broken rule stops the process and returns nothing to map. The
adapter therefore tests what it can before the library sees it: the example asks
`version_matches()` before `Runtime::create()`, which stops on a library of another release,
and turns down a field of no cells with its own `InvalidRequest`. What the graph refuses and
keeps, it reads back with `refusal()` and maps like any other error. A host that must not
stop on what is left installs its own handler with `set_assertion_handler()` at startup, as
`applications/examples/host_hooks_demo.cpp` does.

**What the host sees.** Its own header and its own codes:

```cpp
        // With no field allocated the library refuses the run; the host reads its own code.
        const Status early = advance(*backend, 1);
        std::printf("adapter: advance before allocate_field -> %s: %s\n", to_string(early.code),
                    early.message.c_str());
        if (early.code != StatusCode::InvalidRequest)
            return 1;
```

Source: `applications/examples/adapter/adapter_demo.cpp`

When a release changes a name or a signature, `sushiruntime_adapter.cpp` is the file that
changes. The example passes no type of the library across the seam. A larger consumer may keep
a second seam inside its SYCL lane: SushiBLAS records every operation through one class,
`TaskRecorder`, and one class, `RecordedGraph`, owns the graph and runs it
(`sushiblas/modules/engine/engine`), so a change to a recording verb or to a run verb is one
edit in each.

## 7. Troubleshooting

**"this translation unit launches a kernel and is compiled without -fsycl".** The unit records
a kernel; compile it with `-fsycl`. See §1.

**Link errors on kernel symbols, or "kernel not found" at run time.** A translation unit that
records a kernel, or launches one on a queue itself, was compiled without SYCL. See §1.

**A replay is not byte-equal between two builds.** Check that `SushiRuntime_DETERMINISTIC_FP`
is `ON` in the package you consumed and that your build adds no fast-math flag. The tests named
`Medium_Determinism` in `tests/surface/api/graph/test_graph.cpp` make the same check
in this repository.

**`find_package` does not find `IntelSYCL`.** Configure your SYCL toolchain before
`find_package(SushiRuntime)`, or add your oneAPI installation to `CMAKE_PREFIX_PATH`.

**The process stops in `Runtime::create()` with a message naming two versions.** The loader
found a shared library of another release. See §5.

**A build failure that names `-Werror` or `-march`.** The library's private flags reached the
exported target, which is a defect in this repository. Report it.
