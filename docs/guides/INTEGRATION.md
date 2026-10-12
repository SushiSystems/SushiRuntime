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

1. Every translation unit that records a graph is compiled by a SYCL compiler with SYCL
   enabled. A conventional compiler produces no device code for the lambda.
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
with no access to this source tree, and `sr test --suite package` installs the library and
builds and runs it.

`add_subdirectory(path/to/sushiruntime)` defines the same `SushiRuntime::SushiRuntime` target.
Under a parent project the examples are not built (`SR_BUILD_EXAMPLES` is off there).

### Version compatibility

The package file declares `SameMinorVersion` compatibility: `find_package(SushiRuntime 0.4)`
accepts 0.4.x and rejects 0.5.0. A MINOR release may break the interface while the version is
below 1.0.0, so pin the exact tag you build against, such as `v0.4.0`. The root
[README](../../README.md#stability) states the rule.

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
`SushiRuntime_DISTRIBUTED`.

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

**What it includes.** `<SushiRuntime/sushiruntime.hpp>`, and
`<SushiRuntime/distributed/distributed.hpp>` when it attaches a cluster. No other header of the
library is needed, and no header of the engine is installed.

**What it exposes.** Functions and types of your project: a function that creates the runtime, a
function that runs a step, your own error type. A header of the adapter that the ordinary lane
includes names no type of `SushiRuntime`.

**How it turns a `Result` into your error type.** One function maps every `ErrorCode` to a code
of yours. With no `default` in the `switch`, a compiler that warns on an unhandled enumerator (`-Wswitch`)
reports a code a new release added.
SushiAI does this in one file:

```cpp
        Status status_of_error(const SushiRuntime::Error& error, SeamPhase phase) noexcept
        {
            using SushiRuntime::ErrorCode;

            const char* text = error.message.c_str();
            switch (error.code)
            {
                case ErrorCode::AllocationFailed:
                    return failure_of(StatusCode::OutOfMemory, text);

                case ErrorCode::KernelFailed:
                case ErrorCode::DeviceLost:
                    return failure_of(StatusCode::DeviceFailed, text);

                case ErrorCode::Unsupported:
                case ErrorCode::InvalidAccess:
                case ErrorCode::InvalidState:
                    return failure_of(StatusCode::Unsupported, text);

                case ErrorCode::InvalidGraph:
                    return failure_of(StatusCode::InvalidDescription, text);

                case ErrorCode::ProtocolMismatch:
                    return failure_of(StatusCode::Incompatible, text);

                case ErrorCode::CompileFailed:
                    return failure_of(untyped_code(phase), text);

                case ErrorCode::DanglingResource:
                    SA_FATAL("SushiRuntime reported {}: {}",
                             SushiRuntime::to_string(error.code), text);
            }

            return failure_of(untyped_code(phase), text);
        }
```

Source: not compiled

Every call into the library then goes through a function that tests the `Result` and returns
the mapped error:

```cpp
        Status run_step(SushiRuntime::Graph& graph, SushiRuntime::RunReport& report) noexcept
        {
            return guard(SeamPhase::Execute, [&]()
            {
                const SushiRuntime::Result<SushiRuntime::RunReport*> ran = graph.run(report);
                if (!ran)
                    return status_of_error(ran.error(), SeamPhase::Execute);

                return Status();
            });
        }
```

Source: not compiled

Both blocks are from `sushiai/modules/core/core/source/seam.cpp`, in the SushiAI repository, which
this repository does not build.
`Status`, `StatusCode`, `SeamPhase`, `failure_of`, `untyped_code`, `guard` and `SA_FATAL` are
SushiAI's own names. `guard` catches what a callee outside SushiRuntime throws; the library
itself throws nothing.

SushiAI's seam maps errors and still passes `SushiRuntime::Graph` in its signatures, because
the code that calls it is the SYCL lane. SushiBLAS shows the other half: every operation is
recorded through one class, `TaskRecorder`, and one class, `RecordedGraph`, owns the graph and
runs it (`sushiblas/modules/engine/engine`). A change to a recording verb or to a run verb is
then one edit in each.

A broken rule stops the process and returns nothing to map. An adapter inside a host that must
not stop installs its own handler with `set_assertion_handler()` at startup.

## 7. Troubleshooting

**Link errors on kernel symbols, or "kernel not found" at run time.** A translation unit that
records a graph was compiled without SYCL. See §1.

**A replay is not byte-equal between two builds.** Check that `SushiRuntime_DETERMINISTIC_FP`
is `ON` in the package you consumed and that your build adds no fast-math flag. The tests named
`Integration_Determinism` in `tests/integration/test_determinism.cpp` make the same check
in this repository.

**`find_package` does not find `IntelSYCL`.** Configure your SYCL toolchain before
`find_package(SushiRuntime)`, or add your oneAPI installation to `CMAKE_PREFIX_PATH`.

**The process stops in `Runtime::create()` with a message naming two versions.** The loader
found a shared library of another release. See §5.

**A build failure that names `-Werror` or `-march`.** The library's private flags reached the
exported target, which is a defect in this repository. Report it.
