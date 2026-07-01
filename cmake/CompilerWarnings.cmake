# CompilerWarnings.cmake — the project's warning/optimization policy as a target.
#
# Single responsibility: define an INTERFACE target `sushi_warnings` carrying the
# compiler warning level, the Release optimization flags, and the required
# preprocessor definitions. Consumers depend on the policy target rather than on
# raw flags (Dependency Inversion); the root links it globally so every target —
# library, examples, tests — is compiled under the same policy, exactly as the
# previous global add_compile_options() did.

add_library(sushi_warnings INTERFACE)

if(MSVC OR (WIN32 AND CMAKE_CXX_COMPILER_ID STREQUAL "IntelLLVM"))
    # Windows / MSVC / Intel icx-cl
    target_compile_options(sushi_warnings INTERFACE
        /W4 /permissive- /EHsc
        $<$<CONFIG:Release>:/O2>         # Maximum speed optimization
        $<$<CONFIG:Release>:/fp:fast>    # Fast floating-point math
        $<$<CONFIG:Release>:/arch:AVX2>  # Enable AVX2 instructions (works on AMD)
    )
    target_compile_definitions(sushi_warnings INTERFACE _CRT_SECURE_NO_WARNINGS)
else()
    # Linux / GCC / Clang / Intel oneAPI
    target_compile_options(sushi_warnings INTERFACE
        -O3
        -march=native
        -Wall
        -Wextra
        -Werror
        -pedantic
        -ftls-model=initial-exec
    )
endif()
