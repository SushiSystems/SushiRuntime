# SushiRuntimeProjectOptions.cmake
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026 Sushi Systems
# Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.
# Commercial use requires a licence from Sushi Systems.

# ProjectOptions.cmake — cross-cutting project knobs and C++ language settings.
#
# Single responsibility: the one place to see every build option and the language
# baseline. Include AFTER project(). GPU/test-internal options live with their own
# modules (cmake/gpu/*, tests/CMakeLists.txt); this file holds only the settings
# that are genuinely global to the project.

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

# --- Cross-cutting options ---

# Distributed master-worker layer. OFF by default: when off, none of the
# distributed sources, headers, or third-party transport deps are compiled, so the
# standalone single-machine build is completely unaffected (zero overhead, zero
# extra dependencies).
option(SR_ENABLE_DISTRIBUTED "Build the master-worker distributed execution layer" OFF)
if(SR_ENABLE_DISTRIBUTED)
    message(STATUS "Distributed layer enabled (master-worker offload).")
endif()

# Test suite. OFF by default so a bare library build never needs GTest.
option(SR_BUILD_TESTS "Build the test suite (functional + benchmark)" OFF)
