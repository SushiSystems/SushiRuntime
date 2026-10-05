# CompilerWarnings.cmake
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026 Sushi Systems
# Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.
# Commercial use requires a licence from Sushi Systems.

# CompilerWarnings.cmake — the project's warning policy as a target.
#
# Single responsibility: define an INTERFACE target `sushiruntime_warnings`
# carrying the compiler warning level and the preprocessor definitions a
# warning-clean build needs. Nothing here shapes code generation — optimization and architecture
# targeting live in Optimization.cmake, the floating-point contract in
# Determinism.cmake — because those three answer to different owners: warnings
# are a review standard, architecture is a deployment choice, and FP is part of
# what the library promises its callers.
#
# BuildPolicy.cmake links this PRIVATE, so `-Werror` is a rule for this tree and
# never something an installed consumer inherits.

add_library(sushiruntime_warnings INTERFACE)

if(MSVC OR (WIN32 AND CMAKE_CXX_COMPILER_ID STREQUAL "IntelLLVM"))
    # Windows / MSVC / Intel icx-cl
    target_compile_options(sushiruntime_warnings INTERFACE
        /W4 /permissive- /EHsc
    )
    target_compile_definitions(sushiruntime_warnings INTERFACE _CRT_SECURE_NO_WARNINGS)
else()
    # Linux / GCC / Clang / Intel oneAPI
    target_compile_options(sushiruntime_warnings INTERFACE
        -Wall
        -Wextra
        -Werror
        -pedantic
    )
endif()
