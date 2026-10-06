# Sycl.cmake
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026-present Mustafa Garip & Sushi Systems
# Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.
# Commercial use requires a licence from Sushi Systems.

# Sycl.cmake — SYCL toolchain shaping.
#
# Single responsibility: pick and provision the SYCL toolchain. SushiRuntime is
# compiler-agnostic SYCL 2020; three toolchains are supported:
#   intel-llvm   intel/llvm nightly: clang++ -fsycl        (primary, vendor-neutral)
#   adaptivecpp  acpp driver + find_package(AdaptiveCpp)   (secondary, vendor-neutral)
#   oneapi       Intel oneAPI commercial icpx / icx-cl     (supported, profiling)
#
# This file has two phases because project() sits between them:
#   * on include (BEFORE project): toolchain selection, compiler inference, and
#     the oneAPI Windows linker fixups the compiler probe needs.
#   * sushiruntime_sycl_configure() (AFTER project): find_package(SYCL) dispatch, the
#     add_sycl_to_target() shim, and the clang --gcc-toolchain pin.
# Keeping GPU offload target selection OUT of here is deliberate: that is the GPU
# backend's job (see cmake/gpu/), so a new vendor never touches this file.

# --- Phase 1 (pre-project): toolchain selection --------------------------------

# Did the caller state a toolchain? Asked before the default below plants one,
# because afterwards the cache entry exists either way and the question can no
# longer be answered. On a reconfigure the entry survives from the previous run,
# which is the same answer for the same reason: the toolchain was settled then.
if(DEFINED CACHE{SR_SYCL_TOOLCHAIN})
    set(_sr_toolchain_stated TRUE)
else()
    set(_sr_toolchain_stated FALSE)
endif()

# One docstring, named once. The inference below used to FORCE the entry with ""
# and wipe the help text off it on the first configure.
set(_sr_toolchain_doc "SYCL toolchain: intel-llvm (primary), adaptivecpp, or oneapi")

set(SR_SYCL_TOOLCHAIN "intel-llvm" CACHE STRING "${_sr_toolchain_doc}")
set_property(CACHE SR_SYCL_TOOLCHAIN PROPERTY STRINGS intel-llvm adaptivecpp oneapi)

# When the caller left the default and pinned a compiler, infer the toolchain from
# the compiler executable name so existing oneAPI/acpp invocations keep working.
#
# "Left the default" is now actually tested. It never was: the guard asked only
# whether a compiler had been pinned, so -DSR_SYCL_TOOLCHAIN=adaptivecpp next to a
# pinned icpx was overwritten with "oneapi" without a word, and every STREQUAL
# dispatch downstream -- sushiruntime_sycl_configure, sushiruntime_gpu_finalize,
# and each vendor file -- then took the branch for a toolchain nobody asked for.
if(NOT _sr_toolchain_stated AND CMAKE_CXX_COMPILER)
    if(CMAKE_CXX_COMPILER MATCHES "acpp|syclcc")
        set(SR_SYCL_TOOLCHAIN "adaptivecpp" CACHE STRING "${_sr_toolchain_doc}" FORCE)
    elseif(CMAKE_CXX_COMPILER MATCHES "icpx|icx-cl|icx")
        set(SR_SYCL_TOOLCHAIN "oneapi" CACHE STRING "${_sr_toolchain_doc}" FORCE)
    elseif(CMAKE_CXX_COMPILER MATCHES "clang\\+\\+")
        set(SR_SYCL_TOOLCHAIN "intel-llvm" CACHE STRING "${_sr_toolchain_doc}" FORCE)
    endif()
endif()

# A stated toolchain is obeyed, so a compiler that belongs to a different one is
# now a mismatch rather than something silently corrected. Say so here: obeying it
# fails later, inside find_package or a device pass, with a message that describes
# the symptom and names neither of the two values that disagreed.
if(_sr_toolchain_stated AND CMAKE_CXX_COMPILER)
    set(_sr_compiler_toolchain "")
    if(CMAKE_CXX_COMPILER MATCHES "acpp|syclcc")
        set(_sr_compiler_toolchain "adaptivecpp")
    elseif(CMAKE_CXX_COMPILER MATCHES "icpx|icx-cl|icx")
        set(_sr_compiler_toolchain "oneapi")
    elseif(CMAKE_CXX_COMPILER MATCHES "clang\\+\\+")
        set(_sr_compiler_toolchain "intel-llvm")
    endif()
    if(_sr_compiler_toolchain AND NOT _sr_compiler_toolchain STREQUAL "${SR_SYCL_TOOLCHAIN}")
        message(WARNING
            "SR_SYCL_TOOLCHAIN=${SR_SYCL_TOOLCHAIN} was requested, but "
            "CMAKE_CXX_COMPILER is '${CMAKE_CXX_COMPILER}', which is the compiler "
            "for ${_sr_compiler_toolchain}. The request is being honoured -- pass a "
            "matching compiler, or drop SR_SYCL_TOOLCHAIN to have it inferred.")
    endif()
    unset(_sr_compiler_toolchain)
endif()

# Default compiler per toolchain — only when the caller gave none.
if(NOT DEFINED CMAKE_CXX_COMPILER OR CMAKE_CXX_COMPILER STREQUAL "")
    if(SR_SYCL_TOOLCHAIN STREQUAL "intel-llvm")
        find_program(SR_CLANGXX NAMES clang++)
        if(SR_CLANGXX)
            set(CMAKE_CXX_COMPILER "${SR_CLANGXX}" CACHE FILEPATH "CXX compiler")
        endif()
    elseif(SR_SYCL_TOOLCHAIN STREQUAL "adaptivecpp")
        find_program(SR_ACPP_DRIVER NAMES acpp syclcc)
        if(SR_ACPP_DRIVER)
            set(CMAKE_CXX_COMPILER "${SR_ACPP_DRIVER}" CACHE FILEPATH "CXX compiler")
        endif()
    elseif(SR_SYCL_TOOLCHAIN STREQUAL "oneapi" AND WIN32 AND DEFINED ENV{CMPLR_ROOT})
        set(CMAKE_CXX_COMPILER "$ENV{CMPLR_ROOT}/bin/icx-cl.exe" CACHE FILEPATH "CXX compiler")
    endif()
endif()

# Intel oneAPI on Windows needs its runtime lib paths on the linker search line
# to survive CMake's compiler probe (LNK1104). Resolved from the compiler dir,
# not hardcoded.
if(WIN32 AND SR_SYCL_TOOLCHAIN STREQUAL "oneapi" AND CMAKE_CXX_COMPILER MATCHES "icx-cl")
    get_filename_component(COMPILER_DIR "${CMAKE_CXX_COMPILER}" DIRECTORY)
    get_filename_component(INTEL_COMPILER_DIR "${COMPILER_DIR}" DIRECTORY)
    foreach(LIB_PATH "${INTEL_COMPILER_DIR}/lib" "${INTEL_COMPILER_DIR}/lib/intel64")
        if(EXISTS "${LIB_PATH}")
            set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} /Qoption,link,/LIBPATH:\"${LIB_PATH}\"")
            set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} /Qoption,link,/LIBPATH:\"${LIB_PATH}\"")
            set(CMAKE_MODULE_LINKER_FLAGS "${CMAKE_MODULE_LINKER_FLAGS} /Qoption,link,/LIBPATH:\"${LIB_PATH}\"")
        endif()
    endforeach()
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsycl")
    list(APPEND CMAKE_PREFIX_PATH "${INTEL_COMPILER_DIR}/lib/cmake/IntelSYCL")
endif()

# --- Phase 2 (post-project): find_package(SYCL) + shims ------------------------
#
# A macro (not a function) so find_package's discovered commands/targets and the
# directory-scope compile/link options land in the caller's scope.
macro(sushiruntime_sycl_configure)
    # Both find_package(AdaptiveCpp) and find_package(IntelSYCL) provide the same
    # add_sycl_to_target() command, so src/CMakeLists.txt stays toolchain-neutral.
    # For the bare-flag intel-llvm path we stub that command and the link target.
    if(SR_SYCL_TOOLCHAIN STREQUAL "adaptivecpp")
        find_package(AdaptiveCpp REQUIRED)
        message(STATUS "SYCL toolchain: AdaptiveCpp (${CMAKE_CXX_COMPILER})")
        # Stub the IntelSYCL link target so src/CMakeLists.txt's link line stays
        # toolchain-neutral; AdaptiveCpp already provides add_sycl_to_target().
        if(NOT TARGET IntelSYCL::SYCL_CXX)
            add_library(IntelSYCL::SYCL_CXX INTERFACE IMPORTED)
        endif()

    elseif(SR_SYCL_TOOLCHAIN STREQUAL "oneapi")
        find_package(IntelSYCL REQUIRED)
        message(STATUS "SYCL toolchain: Intel oneAPI commercial (${CMAKE_CXX_COMPILER})")

    elseif(SR_SYCL_TOOLCHAIN STREQUAL "intel-llvm")
        # intel/llvm nightly path: clang++ -fsycl. The project's primary toolchain.
        add_compile_options(-fsycl)
        add_link_options(-fsycl)
        # Stub the IntelSYCL target/command so src/CMakeLists.txt links unchanged.
        if(NOT TARGET IntelSYCL::SYCL_CXX)
            add_library(IntelSYCL::SYCL_CXX INTERFACE IMPORTED)
        endif()
        if(NOT COMMAND add_sycl_to_target)
            function(add_sycl_to_target)
            endfunction()
        endif()
        message(STATUS "SYCL toolchain: intel/llvm clang++ -fsycl (${CMAKE_CXX_COMPILER})")

    else()
        message(FATAL_ERROR "Unknown SR_SYCL_TOOLCHAIN='${SR_SYCL_TOOLCHAIN}'. Use intel-llvm, adaptivecpp, or oneapi.")
    endif()

    # The clang-based toolchains on Linux (acpp, intel-llvm clang++, oneAPI icpx)
    # let the host C++ stdlib follow the active GCC install. With C++17 there is no
    # <expected>/feature-test gating to work around, so only the --gcc-toolchain
    # prefix is pinned (portable, no hardcoded version) to keep clang's -fsycl
    # header search on a single libstdc++. SR_GCC_TOOLCHAIN is the install *prefix*;
    # default /usr fits standard Linux distros. Set it empty to rely purely on
    # discovery. Windows oneAPI (icx-cl) is excluded by the NOT WIN32 guard.
    if(NOT WIN32 AND (SR_SYCL_TOOLCHAIN STREQUAL "adaptivecpp"
                      OR SR_SYCL_TOOLCHAIN STREQUAL "intel-llvm"
                      OR SR_SYCL_TOOLCHAIN STREQUAL "oneapi"))
        set(SR_GCC_TOOLCHAIN "/usr" CACHE STRING
            "GCC install prefix for clang --gcc-toolchain (empty to disable)")
        if(SR_GCC_TOOLCHAIN)
            add_compile_options(--gcc-toolchain=${SR_GCC_TOOLCHAIN})
            add_link_options(--gcc-toolchain=${SR_GCC_TOOLCHAIN})
        endif()
    endif()
endmacro()
