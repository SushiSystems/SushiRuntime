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
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# --- Pre-project: toolchain shaping -------------------------------------------
include(Vcpkg)   # (Windows) locate vcpkg + set the toolchain file
include(Sycl)    # select the SYCL toolchain + compiler; oneAPI Windows linker fixups

# CXX only: the project has no C sources, and the AdaptiveCpp driver (acpp) is a
# C++-only compiler — enabling C would make CMake's C-compiler probe fail there.
project(SushiRuntime VERSION 0.1.0 LANGUAGES CXX)
message(STATUS ">>> SYCL toolchain: ${SR_SYCL_TOOLCHAIN} (${CMAKE_CXX_COMPILER})")

# --- Post-project: policy, deps, and the GPU backend --------------------------
include(ProjectOptions)     # policies, C++17 baseline, cross-cutting options
include(CompilerWarnings)   # -> INTERFACE target sushi_warnings
include(Sanitizers)         # -> INTERFACE target sushi_sanitizers

# Apply the warning/sanitizer policy to every target in this tree (library,
# examples, tests) via the policy targets — the target-based equivalent of the
# former global add_compile_options(). Must precede the add_subdirectory() calls.
link_libraries(sushi_warnings sushi_sanitizers)

include(Dependencies)       # hwloc (+ Windows pkgconf resolution)
sushi_sycl_configure()      # find_package(SYCL) dispatch + add_sycl_to_target shim + gcc-toolchain

# GPU backend selection (Strategy). GpuBackend defines the mechanism; DetectGpu
# resolves the vendor and includes exactly one backend file; finalize renders the
# accumulated offload declarations into compiler/linker flags. Adding a vendor is
# a new file under cmake/gpu/ + one branch in DetectGpu — nothing here changes.
include(gpu/GpuBackend)
include(gpu/DetectGpu)
sushi_gpu_finalize()

# --- Subdirectories -----------------------------------------------------------
add_subdirectory(src)

# Example apps (currently the distributed master/worker demo). The subdir only
# adds targets when SR_ENABLE_DISTRIBUTED is on, so standalone is unaffected.
add_subdirectory(examples)

# --- Testing ------------------------------------------------------------------
if(SR_BUILD_TESTS)
    enable_testing()
    find_package(GTest CONFIG REQUIRED)
    add_subdirectory(tests)
endif()
