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

# Compile policy. BuildPolicy composes the warning, architecture, floating-point
# and sanitizer targets and exposes sushiruntime_apply_build_policy(<target>), which
# every target in the tree calls. It is a function rather than a directory-scope
# link_libraries() so the in-tree-only flags stay out of the installed export
# set — see cmake/BuildPolicy.cmake for the boundary this draws.
include(BuildPolicy)

include(Dependencies)       # hwloc (+ Windows pkgconf resolution)
sushiruntime_sycl_configure()      # find_package(SYCL) dispatch + add_sycl_to_target shim + gcc-toolchain

# GPU backend selection (Strategy). GpuBackend defines the mechanism; DetectGpu
# resolves the vendor and includes exactly one backend file; finalize renders the
# accumulated offload declarations into compiler/linker flags. Adding a vendor is
# a new file under cmake/gpu/ + one branch in DetectGpu — nothing here changes.
include(gpu/GpuBackend)
include(gpu/DetectGpu)
sushiruntime_gpu_finalize()

# Install layout and the package's own location. GNUInstallDirs is what makes an
# install respect a distribution's conventions (lib64, multiarch) instead of
# hardcoding "lib"; SR_INSTALL_CMAKEDIR is where find_package(SushiRuntime) looks.
include(GNUInstallDirs)
set(SR_INSTALL_CMAKEDIR "${CMAKE_INSTALL_LIBDIR}/cmake/SushiRuntime"
    CACHE STRING "Where the SushiRuntime CMake package files are installed")

# The version literals in include/SushiRuntime/version.hpp are hand-written, so
# they are greppable and need no generated header on the include path. That trade
# is only safe if they cannot silently disagree with the version the build and
# the package advertise — so the disagreement is a configure-time error, here,
# rather than a consumer discovering it from a version check that lies.
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/include/SushiRuntime/version.hpp"
     SR_VERSION_STRING_LINE
     REGEX "^#define SUSHIRUNTIME_VERSION_STRING ")
string(REGEX MATCH "\"([0-9]+\\.[0-9]+\\.[0-9]+)\"" _sr_unused "${SR_VERSION_STRING_LINE}")
if(NOT CMAKE_MATCH_1 STREQUAL PROJECT_VERSION)
    message(FATAL_ERROR
        "Version mismatch: project() says ${PROJECT_VERSION}, "
        "include/SushiRuntime/version.hpp says ${CMAKE_MATCH_1}. "
        "Update both — they are the same fact stated twice.")
endif()

# --- Subdirectories -----------------------------------------------------------
add_subdirectory(src)

# Example apps (currently the distributed master/worker demo). The subdir only
# adds targets when SR_ENABLE_DISTRIBUTED is on, so standalone is unaffected.
add_subdirectory(examples)

# --- Package files --------------------------------------------------------------
# Written after add_subdirectory(src) because the Config template interpolates
# options that the subdirectories may still be resolving when this file starts.
include(CMakePackageConfigHelpers)

configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/SushiRuntimeConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/SushiRuntimeConfig.cmake"
    INSTALL_DESTINATION "${SR_INSTALL_CMAKEDIR}"
)

# SameMinorVersion: the project is pre-1.0 and its API is not frozen, so a
# consumer asking for 0.2 must not be handed 0.3. Revisit at the v1.0.0 freeze,
# where SameMajorVersion becomes the honest compatibility statement.
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/SushiRuntimeConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMinorVersion
)

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/SushiRuntimeConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/SushiRuntimeConfigVersion.cmake"
    DESTINATION "${SR_INSTALL_CMAKEDIR}"
)

# --- Testing ------------------------------------------------------------------
if(SR_BUILD_TESTS)
    enable_testing()
    find_package(GTest CONFIG REQUIRED)
    add_subdirectory(tests)
endif()
