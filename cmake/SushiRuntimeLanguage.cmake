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

if(SR_ENABLE_DISTRIBUTED)
    message(STATUS "Distributed layer enabled (master-worker offload).")
endif()
