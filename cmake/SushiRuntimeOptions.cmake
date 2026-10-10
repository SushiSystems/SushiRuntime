# SushiRuntimeOptions.cmake
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026-present Mustafa Garip & Sushi Systems
# Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.
# Commercial use requires a licence from Sushi Systems.

# Declares every option a person or the CLI sets. The logic that reads each one lives in
# the file named beside it. Included before project().
#
# Declared elsewhere: SR_SYCL_TOOLCHAIN in Sycl.cmake, which must ask whether the caller
# stated one before a default exists, and the GPU options in cmake/gpu/, which rewrite
# their own values after probing the machine.

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(SR_IS_TOP_LEVEL ON)
else()
    set(SR_IS_TOP_LEVEL OFF)
endif()

# What is built.
option(SR_ENABLE_DISTRIBUTED "Build the distributed module" OFF)
option(SR_BUILD_TESTS "Build the test programs" OFF)
option(SR_BUILD_BENCHMARKS
    "Build the stress and performance programs; read only with SR_BUILD_TESTS" ON)
option(SR_BUILD_EXAMPLES "Build applications/examples" ${SR_IS_TOP_LEVEL})

# How it is compiled (Determinism.cmake, Optimization.cmake).
option(SR_DETERMINISTIC_FP
    "Compile under the reproducible floating-point contract: no fast-math, no FMA contraction"
    ON)
option(SR_NATIVE_ARCH
    "Target the build machine's ISA; never for a binary that leaves that machine" OFF)

# Install (SushiRuntimeInstall.cmake, SushiRuntimePackaging.cmake). ON also for a parent
# that adds this tree: a parent that exports a target linking the runtime needs the
# runtime in an export set.
option(SR_INSTALL "Add the install rules and the package files" ON)
