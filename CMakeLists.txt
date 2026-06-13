cmake_minimum_required(VERSION 3.20)

# --- Force Authoritative vcpkg Path (Fixes VS Hijacking) ---
if(WIN32)
    if(NOT DEFINED VCPKG_ROOT OR VCPKG_ROOT STREQUAL "")
        set(VCPKG_ROOT "C:/vcpkg" CACHE PATH "Path to vcpkg root")
    endif()
    set(CMAKE_TOOLCHAIN_FILE "${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake" CACHE FILEPATH "vcpkg toolchain file")
    message(STATUS ">>> Using VCPKG_ROOT: ${VCPKG_ROOT}")
endif()

if(WIN32 AND NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE "C:/vcpkg/scripts/buildsystems/vcpkg.cmake" CACHE STRING "vcpkg toolchain file")
endif()

# --- Compiler Selection & Setup ---
if(WIN32)
    if(NOT DEFINED CMAKE_CXX_COMPILER OR CMAKE_CXX_COMPILER STREQUAL "")
        set(INTEL_ONEAPI_DIR "C:/Program Files (x86)/Intel/oneAPI/compiler/latest")
        set(CMAKE_CXX_COMPILER "${INTEL_ONEAPI_DIR}/bin/icx-cl.exe" CACHE FILEPATH "CXX compiler")
    endif()

    # Automatically resolve Intel compiler lib paths if using icx-cl
    if(CMAKE_CXX_COMPILER MATCHES "icx-cl")
        get_filename_component(COMPILER_DIR "${CMAKE_CXX_COMPILER}" DIRECTORY)
        get_filename_component(INTEL_COMPILER_DIR "${COMPILER_DIR}" DIRECTORY)
        
        # Add Intel runtime lib paths to avoid LNK1104 during compiler checks
        set(INTEL_LIB_PATHS
            "${INTEL_COMPILER_DIR}/lib"
            "${INTEL_COMPILER_DIR}/lib/intel64"
        )
        
        foreach(LIB_PATH IN LISTS INTEL_LIB_PATHS)
            if(EXISTS "${LIB_PATH}")
                # Pass via linker flags using icx-cl compatible syntax
                set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} /Qoption,link,/LIBPATH:\"${LIB_PATH}\"")
                set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} /Qoption,link,/LIBPATH:\"${LIB_PATH}\"")
                set(CMAKE_MODULE_LINKER_FLAGS "${CMAKE_MODULE_LINKER_FLAGS} /Qoption,link,/LIBPATH:\"${LIB_PATH}\"")
            endif()
        endforeach()
        
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsycl")
        list(APPEND CMAKE_PREFIX_PATH "${INTEL_COMPILER_DIR}/lib/cmake/IntelSYCL")
    endif()
endif()

project(SushiRuntime VERSION 0.1.0 LANGUAGES CXX C)

# --- Policies ---
if(POLICY CMP0135)
  cmake_policy(SET CMP0135 NEW)
endif()

# --- Configuration ---
set(CMAKE_CXX_STANDARD 20)
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

# Force correct vcpkg tools path if on Windows
if(WIN32)
    set(VCPKG_TOOL_PATH "C:/vcpkg/installed/x64-windows/tools/pkgconf/pkgconf.exe")
    if(EXISTS "${VCPKG_TOOL_PATH}")
        set(PKG_CONFIG_EXECUTABLE "${VCPKG_TOOL_PATH}" CACHE FILEPATH "" FORCE)
        message(STATUS "Using explicit PKG_CONFIG: ${PKG_CONFIG_EXECUTABLE}")
    endif()
endif()

# 1. HWLOC (Hardware Locality) - via vcpkg with pkg-config
find_package(PkgConfig REQUIRED)
pkg_check_modules(HWLOC REQUIRED IMPORTED_TARGET hwloc)
message(STATUS "Found hwloc: ${HWLOC_INCLUDEDIR}")

# 2. SYCL — supports two toolchains:
#   a) Intel oneAPI commercial release  → icpx / icx-cl  → FindIntelSYCL
#   b) intel/llvm open-source build     → clang++ -fsycl → no FindIntelSYCL needed
#
# Detect which toolchain is active by checking the compiler executable name.
# intel/llvm's clang++ already has -fsycl baked in; we just need the flag.
if(CMAKE_CXX_COMPILER MATCHES "icpx|icx-cl|icx")
    find_package(IntelSYCL REQUIRED)
    message(STATUS "SYCL toolchain: Intel oneAPI commercial (${CMAKE_CXX_COMPILER})")
elseif(CMAKE_CXX_COMPILER MATCHES "clang\\+\\+")
    # intel/llvm open-source path: enable SYCL via compiler flag only.
    # Ubuntu 22.04 ships GCC 11 (no <format>); point clang++ at GCC 13 toolchain
    # which has full C++20 stdlib support.
    # --gcc-toolchain points to the GCC install prefix (where bin/lib/include live).
    # Explicit C++13 include dirs are added as fallback in case toolchain discovery
    # still misses them under -fsycl's modified search order.
    add_compile_options(-fsycl
        --gcc-toolchain=/usr
        -I/usr/include/c++/13
        -I/usr/include/x86_64-linux-gnu/c++/13)
    add_link_options(-fsycl --gcc-toolchain=/usr)
    # Stub out IntelSYCL::SYCL_CXX so src/CMakeLists.txt links without changes
    if(NOT TARGET IntelSYCL::SYCL_CXX)
        add_library(IntelSYCL::SYCL_CXX INTERFACE IMPORTED)
    endif()
    # Stub out add_sycl_to_target() — flags already applied globally above
    if(NOT COMMAND add_sycl_to_target)
        function(add_sycl_to_target)
        endfunction()
    endif()
    message(STATUS "SYCL toolchain: intel/llvm open-source clang++ (${CMAKE_CXX_COMPILER})")
else()
    message(FATAL_ERROR "No supported SYCL compiler found. Use icpx (oneAPI) or clang++ (intel/llvm).")
endif()

# NVIDIA CUDA backend for SYCL (requires Codeplay oneAPI for NVIDIA plugin)
# GTX 1060 is Pascal architecture = sm_61
# Enable with: cmake -DSUSHI_ENABLE_CUDA=ON
option(SUSHI_ENABLE_CUDA "Enable SYCL CUDA backend for NVIDIA GPUs" OFF)
if(SUSHI_ENABLE_CUDA)
    message(STATUS "CUDA backend enabled — targeting nvptx64 (sm_61 for GTX 1060)")
    # Append CUDA target to SYCL compilation; sm_61 = Pascal (GTX 1060/1070/1080)
    add_compile_options(-fsycl-targets=spir64,nvptx64-nvidia-cuda
                        -Xsycl-target-backend=nvptx64-nvidia-cuda --cuda-gpu-arch=sm_61)
    add_link_options(-fsycl-targets=spir64,nvptx64-nvidia-cuda
                     -Xsycl-target-backend=nvptx64-nvidia-cuda --cuda-gpu-arch=sm_61)
endif()

# Distributed master-worker layer. OFF by default: when off, none of the
# distributed sources, headers, or third-party transport deps are compiled, so
# the standalone single-machine build is completely unaffected (zero overhead,
# zero extra dependencies). Mirrors the SUSHI_ENABLE_CUDA pattern above.
option(SUSHI_ENABLE_DISTRIBUTED "Build the master-worker distributed execution layer" OFF)
if(SUSHI_ENABLE_DISTRIBUTED)
    message(STATUS "Distributed layer enabled (master-worker offload).")
endif()

# --- Subdirectories ---
add_subdirectory(src)

# Example apps (currently the distributed master/worker demo). The subdir only
# adds targets when SUSHI_ENABLE_DISTRIBUTED is on, so standalone is unaffected.
add_subdirectory(examples)

# --- Testing ---
enable_testing()

# Find GTest
find_package(GTest CONFIG REQUIRED)

add_subdirectory(tests)