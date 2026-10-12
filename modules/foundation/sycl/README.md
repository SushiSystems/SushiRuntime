# sycl

The module holds what every SYCL user of the tree shares. `sycl_compat.hpp` hides the
differences between SYCL toolchains, so the rest of the tree is written once for intel-llvm,
oneAPI and AdaptiveCpp. `error_translation.hpp` turns what SYCL code throws into an `Error`.
The module is internal: its headers are for the other modules of the library and are not installed.

It depends on the SYCL headers of the toolchain and on `core`, for `Error` and `Result`.

## Public entry points

`sycl_compat.hpp` declares the helpers that submit a host task the same way on every
toolchain. A module that needs a toolchain-specific spelling adds it here, not at its call
site.

`error_translation.hpp` defines `Internal::SYCL::translate_exceptions()`. Every catch ladder
around SYCL code in the engine and the surface sources is a call of it.

The module has no sources and no tests of its own; the execution and scheduler tests run the
helpers on the toolchain the build selected.

## sycl_compat.hpp

### has_host_task

has_host_task uses SFINAE to test at compile time whether a command group handler has the standard
host_task method.

### enqueue_host_task

enqueue_host_task() picks the host execution mechanism the toolchain has. A handler with host_task
uses it. AdaptiveCpp has none, so the callable goes through AdaptiveCpp_enqueue_custom_operation;
the custom operation runs on the host once its dependencies are satisfied. Its interop_handle
argument is unused, and the lambda accepts it generically so that its type need not be named. The
callable takes no arguments.

## error_translation.hpp

### error_code_of

error_code_of() returns the code of a `sycl::exception`. A failure whose `sycl::errc` is
`memory_allocation` is `AllocationFailed`, the code `std::bad_alloc` has, so refused memory
reads the same from the host and from a device. Any other failure takes the code the caller
passes, because the same `sycl::errc::runtime` means a lost device while a queue is built and a
failed kernel while a graph runs.

### translate_exceptions

translate_exceptions() calls a callable and returns what it throws as a `Result<void>`. It adds
the `sycl::exception` arm to `Detail::translate_exceptions()` of
`<SushiRuntime/core/error_translation.hpp>`, which holds the arms of the standard library and
does not include SYCL. The callable runs inside the function's own `try` and nothing is
rethrown: a rethrow from a helper crashes on the Windows intel-llvm toolchain.

| Call | `sycl::exception` | `std::bad_alloc` | other `std::exception`, other object |
| --- | --- | --- | --- |
| `translate_exceptions(function)` | `KernelFailed` | `AllocationFailed` | `KernelFailed` |
| `translate_exceptions(device_code, code, function)` | `device_code` | `AllocationFailed` | `code` |

In both rows a `sycl::exception` of `memory_allocation` is `AllocationFailed`.
