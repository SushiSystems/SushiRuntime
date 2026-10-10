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

if(SR_NATIVE_ARCH)
    message(STATUS "SR_NATIVE_ARCH=ON — binaries target this machine's ISA and are not portable.")
endif()
