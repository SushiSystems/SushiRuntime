# CMakeLists.txt
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026-present Mustafa Garip & Sushi Systems
# Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.
# Commercial use requires a licence from Sushi Systems.

cmake_minimum_required(VERSION 3.20)

# SushiRuntime — compiler-agnostic, GPU-vendor-agnostic SYCL 2020 runtime.
#
# This file is intentionally thin: it orchestrates a sequence of focused modules
# under cmake/, each with a single responsibility, rather than doing the work
# itself. The ordering matters and is the one thing this file owns:
#   * Vcpkg + Sycl run BEFORE project() — they shape the toolchain file and the
#     compiler that project()'s probe validates.
#   * everything else runs AFTER project().
# See cmake/ for the details of each stage.
# Modules are included by absolute path, never by bare name. CMAKE_MODULE_PATH is
# one flat, first-match-wins list shared with whoever add_subdirectory's this tree,
# and the engine appends its own cmake/ before adding the runtime — so include(X)
# here resolves to *its* X.cmake whenever both repos own that name. Two of ours
# collided in exactly this way: ProjectOptions.cmake and Vcpkg.cmake, which left
# SR_ENABLE_DISTRIBUTED and SR_BUILD_TESTS undefined in every engine build and the
# runtime's policy block unrun, silently, because both options default to OFF.
# Those two are now prefixed as well, but the absolute path is the fix: it does not
# depend on a name staying unique.
#
# CMAKE_MODULE_PATH is still appended for find_package(<Pkg>) module-mode lookups.
set(SR_CMAKE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
list(APPEND CMAKE_MODULE_PATH "${SR_CMAKE_DIR}")

# --- Pre-project: toolchain shaping -------------------------------------------
include("${SR_CMAKE_DIR}/SushiRuntimeVcpkg.cmake")   # (Windows) locate vcpkg + set the toolchain file
include("${SR_CMAKE_DIR}/Sycl.cmake")    # select the SYCL toolchain + compiler; oneAPI Windows linker fixups

# CXX only: the project has no C sources, and the AdaptiveCpp driver (acpp) is a
# C++-only compiler — enabling C would make CMake's C-compiler probe fail there.
project(SushiRuntime VERSION 0.3.0 LANGUAGES CXX)
message(STATUS ">>> SYCL toolchain: ${SR_SYCL_TOOLCHAIN} (${CMAKE_CXX_COMPILER})")

# --- Post-project: policy, deps, and the GPU backend --------------------------
include("${SR_CMAKE_DIR}/SushiRuntimeProjectOptions.cmake")     # policies, C++17 baseline, cross-cutting options

# Compile policy. BuildPolicy composes the warning, architecture, floating-point
# and sanitizer targets and exposes sushiruntime_apply_build_policy(<target>), which
# every target in the tree calls. It is a function rather than a directory-scope
# link_libraries() so the in-tree-only flags stay out of the installed export
# set — see cmake/BuildPolicy.cmake for the boundary this draws.
include("${SR_CMAKE_DIR}/BuildPolicy.cmake")

include("${SR_CMAKE_DIR}/Dependencies.cmake")       # hwloc (+ Windows pkgconf resolution)
sushiruntime_sycl_configure()      # find_package(SYCL) dispatch + add_sycl_to_target shim + gcc-toolchain

# GPU backend selection (Strategy). GpuBackend defines the mechanism; DetectGpu
# resolves the vendor and includes exactly one backend file; finalize renders the
# accumulated offload declarations into compiler/linker flags. Adding a vendor is
# a new file under cmake/gpu/ + one branch in DetectGpu — nothing here changes.
include("${SR_CMAKE_DIR}/gpu/GpuBackend.cmake")
include("${SR_CMAKE_DIR}/gpu/DetectGpu.cmake")
sushiruntime_gpu_finalize()

# Declares the install layout (GNUInstallDirs) the library target reads, and the
# function that installs it once the modules exist.
include("${SR_CMAKE_DIR}/SushiRuntimeInstall.cmake")
include("${SR_CMAKE_DIR}/SushiRuntimeVersionCheck.cmake")

# --- Modules, the library and the programs --------------------------------------
# The test function is declared before the modules, because a module declares its own
# test program. Each module is compiled with the include roots it declared; the tier
# table in cmake/SushiRuntimeTiers.cmake decides which dependencies are legal.
include("${SR_CMAKE_DIR}/SushiRuntimeTests.cmake")
include("${SR_CMAKE_DIR}/SushiRuntimeModule.cmake")
include("${SR_CMAKE_DIR}/SushiRuntimeLibrary.cmake")
add_subdirectory(modules)

add_subdirectory(applications/examples)

# --- Install and package --------------------------------------------------------
# After add_subdirectory(modules): the rules name the library target and the include
# roots the modules recorded.
sushiruntime_install_package()
include("${SR_CMAKE_DIR}/SushiRuntimePackaging.cmake")

# --- Testing ------------------------------------------------------------------
# Tests that cross module boundaries; a module's own tests are declared in its folder.
if(SR_BUILD_TESTS)
    add_subdirectory(tests)
endif()