# Optimization.cmake
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026-present Mustafa Garip & Sushi Systems
# Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.
# Commercial use requires a licence from Sushi Systems.

# Optimization.cmake — architecture targeting and TLS model as a target.
#
# Single responsibility: define an INTERFACE target `sushiruntime_perf` carrying
# the code-generation flags that are a property of the machine a binary is built
# *for*. The optimization level itself is not here: CMake's own
# CMAKE_CXX_FLAGS_<CONFIG> already supplies -O3 / /O2 in Release, and repeating
# it made Debug builds compile at -O3 as well.
#
# Architecture targeting is opt-in. `-march=native` bakes the build machine's
# exact ISA into the binary, which is right for a developer benchmarking on the
# box in front of them and wrong for anything shipped: the resulting binary
# faults with an illegal instruction on any older CPU, and the instruction
# selection it licenses is one of the ways two builds of the same source stop
# agreeing bit-for-bit. BuildPolicy.cmake links this PRIVATE, so it applies to
# this tree only and never reaches an installed consumer.

add_library(sushiruntime_perf INTERFACE)

if(MSVC OR (WIN32 AND CMAKE_CXX_COMPILER_ID STREQUAL "IntelLLVM"))
    if(SR_NATIVE_ARCH)
        target_compile_options(sushiruntime_perf INTERFACE /arch:AVX2)
    endif()
else()
    # initial-exec resolves a thread_local through a fixed offset instead of a
    # call to __tls_get_addr, which the allocator's per-thread magazines hit on
    # every fast-path allocation. It constrains the library to being linked at
    # load time rather than dlopen'd, which is why it stays in-tree policy.
    target_compile_options(sushiruntime_perf INTERFACE -ftls-model=initial-exec)

    if(SR_NATIVE_ARCH)
        target_compile_options(sushiruntime_perf INTERFACE -march=native)
    endif()
endif()

# Debug host code carries line tables only: stack traces keep their files and lines at a fraction of
# full debug information's compile memory, object size and link time. The later flag overrides -g.
# The intel/llvm driver also runs a spir64 device pass, which rejects the flag, so it goes to the
# host pass alone.
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang" AND NOT CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
    if(SR_SYCL_TOOLCHAIN STREQUAL "intel-llvm")
        set(sr_line_tables "SHELL:-Xarch_host -gline-tables-only")
    else()
        set(sr_line_tables "-gline-tables-only")
    endif()
    target_compile_options(sushiruntime_perf INTERFACE
        "$<$<AND:$<CONFIG:Debug>,$<COMPILE_LANGUAGE:CXX>>:${sr_line_tables}>")
endif()

if(SR_NATIVE_ARCH)
    message(STATUS "SR_NATIVE_ARCH=ON — binaries target this machine's ISA and are not portable.")
endif()
