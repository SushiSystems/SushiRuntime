# SushiRuntimeLanguage.cmake
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026-present Mustafa Garip & Sushi Systems
# Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.
# Commercial use requires a licence from Sushi Systems.

# Sets the CMake policies and the C++ language baseline. Included after project().
# The options are in cmake/SushiRuntimeOptions.cmake.

# --- Policies ---
if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()

# --- Language baseline ---
# C++17. The v1 error-handling channel and fluent API are built on a hand-rolled
# Result type and SFINAE rather than std::expected/concepts/std::format, so the
# whole tree (including the SYCL CUDA/nvptx device pass) stays on a standard every
# supported toolchain compiles cleanly.
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# --- C runtime (MSVC ABI) ---
# Every build type takes the release C runtime, so a Debug tree needs no debug build of its dependencies.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")
# A Debug tree links the Release build of an imported package, which shares that runtime. None is
# the configuration of a distribution package, such as Ubuntu's libgtest-dev.
set(CMAKE_MAP_IMPORTED_CONFIG_DEBUG Release RelWithDebInfo MinSizeRel None "")
# The MSVC STL's container layout depends on this; libstdc++ and libc++ never read it.
if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC" OR CMAKE_CXX_SIMULATE_ID STREQUAL "MSVC")
    add_compile_definitions("$<$<COMPILE_LANGUAGE:CXX>:_ITERATOR_DEBUG_LEVEL=0>")
endif()

if(SR_ENABLE_DISTRIBUTED)
    message(STATUS "Distributed layer enabled (master-worker offload).")
endif()
