# Sanitizers.cmake
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026-present Mustafa Garip & Sushi Systems
# Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.
# Commercial use requires a licence from Sushi Systems.

# Sanitizers.cmake — the project's sanitizer policy as a target.
#
# Single responsibility: define an INTERFACE target `sushiruntime_sanitizers`. On the
# clang/gcc (non-MSVC) toolchains SR_THREAD_SANITIZER adds ThreadSanitizer; no build type
# implies it, and the CLI sets it for its isolated tree only (`sr build --type tsan`).
# AddressSanitizer is NOT wired here: it is injected explicitly by the CLI
# (`sr build --type asan`) via CMAKE_CXX_FLAGS into an isolated build tree, and TSan and
# ASan cannot coexist.
#
# The target always exists (possibly empty) so the root can link it
# unconditionally; when no sanitizer applies it contributes nothing.

add_library(sushiruntime_sanitizers INTERFACE)

# CMake reports the intel/llvm SYCL clang++ as CMAKE_CXX_COMPILER_ID "Clang"
# (not "IntelLLVM"), so the Windows exclusion below must match on "Clang"
# generally, not a toolchain-specific id.
if(NOT (MSVC OR (WIN32 AND CMAKE_CXX_COMPILER_ID MATCHES "Clang")))
    if(SR_THREAD_SANITIZER)
        # The SYCL device pass is left uninstrumented: the device ThreadSanitizer of the SYCL
        # runtime cannot reserve its shadow memory beside the host one and aborts. The opt-out is
        # spelled -Xsycl-target-frontend because ccache drops -Xarch_device and its argument.
        # A unit compiled without -fsycl has no device pass, so the opt-out is unused there and
        # -Werror would turn that into an error; -Wno-unused-command-line-argument keeps it quiet.
        set(device_off
            "$<$<COMPILE_LANG_AND_ID:CXX,Clang,IntelLLVM>:SHELL:-Xsycl-target-frontend -fno-sanitize=thread>"
            "$<$<COMPILE_LANG_AND_ID:CXX,Clang,IntelLLVM>:-Wno-unused-command-line-argument>")
        target_compile_options(sushiruntime_sanitizers INTERFACE -fsanitize=thread ${device_off})
        target_link_options(sushiruntime_sanitizers INTERFACE -fsanitize=thread
            "$<$<LINK_LANG_AND_ID:CXX,Clang,IntelLLVM>:SHELL:-Xarch_device -fno-sanitize=thread>")
    endif()
endif()
