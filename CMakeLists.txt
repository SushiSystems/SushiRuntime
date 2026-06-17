cmake_minimum_required(VERSION 3.20)

# --- SYCL toolchain selection -------------------------------------------------
# SushiRuntime is compiler-agnostic SYCL 2020. Three toolchains are supported,
# AdaptiveCpp being the primary/recommended one. The project deliberately leans
# away from vendor lock-in: the default and llvm paths are vendor-neutral, while
# oneAPI is kept only as a secondary path for Intel profiling tools.
#   adaptivecpp  acpp driver + find_package(AdaptiveCpp)   (primary, vendor-neutral)
#   llvm         Clang/LLVM SYCL: clang++ -fsycl           (secondary, vendor-neutral)
#   oneapi       Intel oneAPI commercial icpx / icx-cl     (secondary, profiling)
#
# Pick one explicitly with -DSR_SYCL_TOOLCHAIN=<name>. When left at the
# default and a compiler is provided, the toolchain is inferred from the
# compiler executable name so existing oneAPI/clang++ invocations keep working.
set(SR_SYCL_TOOLCHAIN "adaptivecpp" CACHE STRING
    "SYCL toolchain: adaptivecpp (primary), llvm, or oneapi")
set_property(CACHE SR_SYCL_TOOLCHAIN PROPERTY STRINGS adaptivecpp llvm oneapi)

if(DEFINED CMAKE_CXX_COMPILER AND NOT CMAKE_CXX_COMPILER STREQUAL "")
    if(CMAKE_CXX_COMPILER MATCHES "acpp|syclcc")
        set(SR_SYCL_TOOLCHAIN "adaptivecpp" CACHE STRING "" FORCE)
    elseif(CMAKE_CXX_COMPILER MATCHES "icpx|icx-cl|icx")
        set(SR_SYCL_TOOLCHAIN "oneapi" CACHE STRING "" FORCE)
    elseif(CMAKE_CXX_COMPILER MATCHES "clang\\+\\+")
        set(SR_SYCL_TOOLCHAIN "llvm" CACHE STRING "" FORCE)
    endif()
endif()

# --- vcpkg discovery (no hardcoded drive paths) -------------------------------
# Prefer the VCPKG_ROOT env var; fall back to C:/vcpkg only if it actually
# exists. Never overwrite a toolchain file the caller already supplied.
if(WIN32)
    if((NOT DEFINED VCPKG_ROOT OR VCPKG_ROOT STREQUAL "") AND DEFINED ENV{VCPKG_ROOT})
        set(VCPKG_ROOT "$ENV{VCPKG_ROOT}" CACHE PATH "Path to vcpkg root")
    elseif((NOT DEFINED VCPKG_ROOT OR VCPKG_ROOT STREQUAL "") AND EXISTS "C:/vcpkg")
        set(VCPKG_ROOT "C:/vcpkg" CACHE PATH "Path to vcpkg root (fallback)")
    endif()
    if(VCPKG_ROOT AND (NOT DEFINED CMAKE_TOOLCHAIN_FILE OR CMAKE_TOOLCHAIN_FILE STREQUAL ""))
        set(CMAKE_TOOLCHAIN_FILE "${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake"
            CACHE FILEPATH "vcpkg toolchain file")
    endif()
    if(VCPKG_ROOT)
        message(STATUS ">>> Using VCPKG_ROOT: ${VCPKG_ROOT}")
    endif()
endif()

# --- Default compiler per toolchain (only when the caller gave none) ----------
if(NOT DEFINED CMAKE_CXX_COMPILER OR CMAKE_CXX_COMPILER STREQUAL "")
    if(SR_SYCL_TOOLCHAIN STREQUAL "adaptivecpp")
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

project(SushiRuntime VERSION 0.1.0 LANGUAGES CXX C)

message(STATUS ">>> SYCL toolchain: ${SR_SYCL_TOOLCHAIN} (${CMAKE_CXX_COMPILER})")

# --- Policies ---
if(POLICY CMP0135)
  cmake_policy(SET CMP0135 NEW)
endif()

# --- Configuration ---
# C++23 is required for std::expected, std::source_location and std::println
# used by the v1 error-handling channel and fluent API.
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# --- Compiler Flags (Production Ready) ---
if(MSVC OR (WIN32 AND CMAKE_CXX_COMPILER_ID STREQUAL "IntelLLVM"))
    # Windows / MSVC / Intel icx-cl
    add_compile_options(/W4 /permissive- /EHsc)
    
    # Release / Maximum Performance Optimizations
    add_compile_options($<$<CONFIG:Release>:/O2>)         # Maximum speed optimization
    add_compile_options($<$<CONFIG:Release>:/fp:fast>)    # Fast floating-point math
    add_compile_options($<$<CONFIG:Release>:/arch:AVX2>)  # Enable AVX2 instructions (Works on AMD)
    
    add_definitions(-D_CRT_SECURE_NO_WARNINGS)
else()
    # Linux / GCC / Clang / Intel oneAPI
    add_compile_options(
        -O3 
        -march=native 
        -Wall 
        -Wextra 
        -Werror 
        -pedantic
        -ftls-model=initial-exec
    )
    
    # Sanitizers (Optional - Enabled for Debug/RelWithDebInfo)
    if(CMAKE_BUILD_TYPE MATCHES "Debug|RelWithDebInfo")
        add_compile_options(-fsanitize=thread)
        add_link_options(-fsanitize=thread)
    endif()
endif()

# --- Dependencies ---

# On Windows the pkgconf shipped by vcpkg is the one that knows about the
# vcpkg-installed hwloc. Resolve it relative to VCPKG_ROOT instead of a fixed
# drive path, and only when present.
if(WIN32 AND VCPKG_ROOT)
    set(VCPKG_TOOL_PATH "${VCPKG_ROOT}/installed/x64-windows/tools/pkgconf/pkgconf.exe")
    if(EXISTS "${VCPKG_TOOL_PATH}")
        set(PKG_CONFIG_EXECUTABLE "${VCPKG_TOOL_PATH}" CACHE FILEPATH "" FORCE)
        message(STATUS "Using explicit PKG_CONFIG: ${PKG_CONFIG_EXECUTABLE}")
    endif()
endif()

# 1. HWLOC (Hardware Locality) - via vcpkg with pkg-config
find_package(PkgConfig REQUIRED)
pkg_check_modules(HWLOC REQUIRED IMPORTED_TARGET hwloc)
message(STATUS "Found hwloc: ${HWLOC_INCLUDEDIR}")

# 2. SYCL — one of three toolchains, selected by SR_SYCL_TOOLCHAIN.
#
# Both find_package(AdaptiveCpp) and find_package(IntelSYCL) provide the same
# add_sycl_to_target() command, so src/CMakeLists.txt stays toolchain-neutral.
# For the bare-flag llvm path we stub that command and the link target.

# SR_CUDA_ARCH replaces the old hardcoded sm_61. Accepts a bare arch number
# (e.g. 61 for Pascal/GTX 1060, 86 for Ampere) used to build the per-toolchain
# CUDA target string. The dev GPU is Pascal, hence the 61 default.
set(SR_CUDA_ARCH "61" CACHE STRING "CUDA compute capability for the SYCL nvptx64 backend (e.g. 61, 80, 86)")
option(SR_ENABLE_CUDA "Enable SYCL CUDA backend for NVIDIA GPUs" OFF)

if(SR_SYCL_TOOLCHAIN STREQUAL "adaptivecpp")
    find_package(AdaptiveCpp REQUIRED)
    # AdaptiveCpp selects backends through ACPP_TARGETS. "generic" is the
    # portable single-pass JIT target; add cuda:sm_<arch> when CUDA is requested.
    if(NOT DEFINED ACPP_TARGETS OR ACPP_TARGETS STREQUAL "")
        if(SR_ENABLE_CUDA)
            set(ACPP_TARGETS "generic;cuda:sm_${SR_CUDA_ARCH}" CACHE STRING "AdaptiveCpp targets" FORCE)
        else()
            set(ACPP_TARGETS "generic" CACHE STRING "AdaptiveCpp targets" FORCE)
        endif()
    endif()
    message(STATUS "SYCL toolchain: AdaptiveCpp (ACPP_TARGETS=${ACPP_TARGETS})")

elseif(SR_SYCL_TOOLCHAIN STREQUAL "oneapi")
    find_package(IntelSYCL REQUIRED)
    message(STATUS "SYCL toolchain: Intel oneAPI commercial (${CMAKE_CXX_COMPILER})")
    if(SR_ENABLE_CUDA)
        message(STATUS "CUDA backend enabled — nvptx64 sm_${SR_CUDA_ARCH}")
        add_compile_options(-fsycl-targets=spir64,nvptx64-nvidia-cuda
                            -Xsycl-target-backend=nvptx64-nvidia-cuda --cuda-gpu-arch=sm_${SR_CUDA_ARCH})
        add_link_options(-fsycl-targets=spir64,nvptx64-nvidia-cuda
                         -Xsycl-target-backend=nvptx64-nvidia-cuda --cuda-gpu-arch=sm_${SR_CUDA_ARCH})
    endif()

elseif(SR_SYCL_TOOLCHAIN STREQUAL "llvm")
    # Vendor-neutral Clang/LLVM SYCL path: clang++ -fsycl. Works with any
    # SYCL-enabled clang (e.g. an intel/llvm-derived build) without binding to
    # Intel tooling.
    add_compile_options(-fsycl)
    add_link_options(-fsycl)

    # Point clang at a GCC install prefix for the host C++ stdlib. This is needed
    # because -fsycl's modified header search can otherwise pick an older libstdc++
    # that lacks C++23 <expected> (e.g. GCC 11 alongside GCC 13 on Ubuntu 22.04).
    # SR_GCC_TOOLCHAIN is the install *prefix* (portable, not a version-pinned
    # include path); default /usr fits standard Linux distros. Set it empty to
    # rely purely on discovery.
    if(NOT WIN32)
        set(SR_GCC_TOOLCHAIN "/usr" CACHE STRING
            "GCC install prefix for clang --gcc-toolchain (empty to disable)")
        if(SR_GCC_TOOLCHAIN)
            add_compile_options(--gcc-toolchain=${SR_GCC_TOOLCHAIN})
            add_link_options(--gcc-toolchain=${SR_GCC_TOOLCHAIN})

            # -fsycl's device pass can miss the host libstdc++ headers that
            # --gcc-toolchain alone would supply, falling back to an older stdlib
            # without C++23 <expected>. Discover the newest libstdc++ version dir
            # under the prefix (no hardcoded version) and add it explicitly.
            file(GLOB _sr_libstdcxx_dirs "${SR_GCC_TOOLCHAIN}/include/c++/*")
            if(_sr_libstdcxx_dirs)
                list(SORT _sr_libstdcxx_dirs COMPARE NATURAL)
                list(GET _sr_libstdcxx_dirs -1 _sr_libstdcxx)
                get_filename_component(_sr_gcc_ver "${_sr_libstdcxx}" NAME)
                add_compile_options(-I${_sr_libstdcxx})
                file(GLOB _sr_libstdcxx_triple
                     "${SR_GCC_TOOLCHAIN}/include/*/c++/${_sr_gcc_ver}")
                foreach(_d IN LISTS _sr_libstdcxx_triple)
                    add_compile_options(-I${_d})
                endforeach()
                message(STATUS "llvm toolchain: using libstdc++ headers at ${_sr_libstdcxx}")
            endif()
        endif()
    endif()
    if(SR_ENABLE_CUDA)
        message(STATUS "CUDA backend enabled — nvptx64 sm_${SR_CUDA_ARCH}")
        add_compile_options(-fsycl-targets=spir64,nvptx64-nvidia-cuda
                            -Xsycl-target-backend=nvptx64-nvidia-cuda --cuda-gpu-arch=sm_${SR_CUDA_ARCH})
        add_link_options(-fsycl-targets=spir64,nvptx64-nvidia-cuda
                         -Xsycl-target-backend=nvptx64-nvidia-cuda --cuda-gpu-arch=sm_${SR_CUDA_ARCH})
    endif()
    # Stub the IntelSYCL target/command so src/CMakeLists.txt links unchanged.
    if(NOT TARGET IntelSYCL::SYCL_CXX)
        add_library(IntelSYCL::SYCL_CXX INTERFACE IMPORTED)
    endif()
    if(NOT COMMAND add_sycl_to_target)
        function(add_sycl_to_target)
        endfunction()
    endif()
    message(STATUS "SYCL toolchain: Clang/LLVM clang++ -fsycl (${CMAKE_CXX_COMPILER})")

else()
    message(FATAL_ERROR "Unknown SR_SYCL_TOOLCHAIN='${SR_SYCL_TOOLCHAIN}'. Use adaptivecpp, llvm, or oneapi.")
endif()

# For the AdaptiveCpp and oneapi paths, find_package already provides
# add_sycl_to_target(). Stub the IntelSYCL link target on the AdaptiveCpp path
# so src/CMakeLists.txt's link line stays toolchain-neutral.
if(SR_SYCL_TOOLCHAIN STREQUAL "adaptivecpp" AND NOT TARGET IntelSYCL::SYCL_CXX)
    add_library(IntelSYCL::SYCL_CXX INTERFACE IMPORTED)
endif()

# Distributed master-worker layer. OFF by default: when off, none of the
# distributed sources, headers, or third-party transport deps are compiled, so
# the standalone single-machine build is completely unaffected (zero overhead,
# zero extra dependencies). Mirrors the SR_ENABLE_CUDA pattern above.
option(SR_ENABLE_DISTRIBUTED "Build the master-worker distributed execution layer" OFF)
if(SR_ENABLE_DISTRIBUTED)
    message(STATUS "Distributed layer enabled (master-worker offload).")
endif()

# --- Subdirectories ---
add_subdirectory(src)

# Example apps (currently the distributed master/worker demo). The subdir only
# adds targets when SR_ENABLE_DISTRIBUTED is on, so standalone is unaffected.
add_subdirectory(examples)

# --- Testing ---
enable_testing()

# Find GTest
find_package(GTest CONFIG REQUIRED)

add_subdirectory(tests)