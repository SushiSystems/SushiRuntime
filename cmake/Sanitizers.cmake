# Sanitizers.cmake — the project's sanitizer policy as a target.
#
# Single responsibility: define an INTERFACE target `sushi_sanitizers`. On the
# clang/gcc (non-MSVC) toolchains a Debug or RelWithDebInfo build automatically
# gets ThreadSanitizer, matching the previous global behavior. AddressSanitizer is
# NOT wired here: it is injected explicitly by the CLI (`sr build --type asan`) via
# CMAKE_CXX_FLAGS into an isolated build tree, and TSan and ASan cannot coexist.
#
# The target always exists (possibly empty) so the root can link it
# unconditionally; when no sanitizer applies it contributes nothing.

add_library(sushi_sanitizers INTERFACE)

if(NOT (MSVC OR (WIN32 AND CMAKE_CXX_COMPILER_ID STREQUAL "IntelLLVM")))
    if(CMAKE_BUILD_TYPE MATCHES "Debug|RelWithDebInfo")
        target_compile_options(sushi_sanitizers INTERFACE -fsanitize=thread)
        target_link_options(sushi_sanitizers INTERFACE -fsanitize=thread)
    endif()
endif()
