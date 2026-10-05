# Sanitizers.cmake
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026 Sushi Systems
# Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.
# Commercial use requires a licence from Sushi Systems.

# Sanitizers.cmake — the project's sanitizer policy as a target.
#
# Single responsibility: define an INTERFACE target `sushiruntime_sanitizers`. On the
# clang/gcc (non-MSVC) toolchains a Debug or RelWithDebInfo build automatically
# gets ThreadSanitizer, matching the previous global behavior. AddressSanitizer is
# NOT wired here: it is injected explicitly by the CLI (`sr build --type asan`) via
# CMAKE_CXX_FLAGS into an isolated build tree, and TSan and ASan cannot coexist.
#
# The target always exists (possibly empty) so the root can link it
# unconditionally; when no sanitizer applies it contributes nothing.

add_library(sushiruntime_sanitizers INTERFACE)

# CMake reports the intel/llvm SYCL clang++ as CMAKE_CXX_COMPILER_ID "Clang"
# (not "IntelLLVM"), so the Windows exclusion below must match on "Clang"
# generally, not a toolchain-specific id.
if(NOT (MSVC OR (WIN32 AND CMAKE_CXX_COMPILER_ID MATCHES "Clang")))
    if(CMAKE_BUILD_TYPE MATCHES "Debug|RelWithDebInfo")
        target_compile_options(sushiruntime_sanitizers INTERFACE -fsanitize=thread)
        target_link_options(sushiruntime_sanitizers INTERFACE -fsanitize=thread)
    endif()
endif()
