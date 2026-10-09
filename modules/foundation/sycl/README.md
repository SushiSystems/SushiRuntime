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
