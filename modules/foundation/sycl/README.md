# sycl

The module holds the differences between SYCL toolchains behind one header,
`sycl_compat.hpp`, so the rest of the tree is written once for intel-llvm, oneAPI and
AdaptiveCpp.

It depends on the SYCL headers of the toolchain and on no other module of the repository.

## Public entry points

`sycl_compat.hpp` declares the helpers that submit a host task the same way on every
toolchain. A module that needs a toolchain-specific spelling adds it here, not at its call
site.

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
