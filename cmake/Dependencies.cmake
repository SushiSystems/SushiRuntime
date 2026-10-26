# Dependencies.cmake
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026-present Mustafa Garip & Sushi Systems
# Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.
# Commercial use requires a licence from Sushi Systems.

# Dependencies.cmake — third-party library discovery.
#
# Single responsibility: find the non-SYCL external dependencies and expose them
# as imported targets. SYCL discovery lives in cmake/Sycl.cmake (it is a toolchain
# concern); GTest is found in the root only when SR_BUILD_TESTS is on, since it is
# needed by the tests subtree alone.

# On Windows the pkgconf shipped by vcpkg is the one that knows about the
# vcpkg-installed hwloc. Resolve it relative to VCPKG_ROOT instead of a fixed drive
# path, and only when present.
if(WIN32 AND VCPKG_ROOT)
    set(VCPKG_TOOL_PATH "${VCPKG_ROOT}/installed/x64-windows/tools/pkgconf/pkgconf.exe")
    if(EXISTS "${VCPKG_TOOL_PATH}")
        set(PKG_CONFIG_EXECUTABLE "${VCPKG_TOOL_PATH}" CACHE FILEPATH "" FORCE)
        message(STATUS "Using explicit PKG_CONFIG: ${PKG_CONFIG_EXECUTABLE}")
    endif()
endif()

# HWLOC (Hardware Locality) — via vcpkg with pkg-config. Exposed as the imported
# target PkgConfig::HWLOC.
find_package(PkgConfig REQUIRED)
pkg_check_modules(HWLOC REQUIRED IMPORTED_TARGET hwloc)
message(STATUS "Found hwloc: ${HWLOC_INCLUDEDIR}")

# SushiLog, the one logger of the process. The CLI builds or reuses its package and passes
# SushiLog_DIR; nothing is built in this tree and nothing is downloaded.
find_package(SushiLog CONFIG REQUIRED)
message(STATUS "Found SushiLog ${SushiLog_VERSION}: ${SushiLog_DIR}")

# SushiProfiler, the stack's profiler, exposed as SushiProfiler::SushiProfiler. The CLI builds
# or reuses its package and passes SushiProfiler_DIR; it is needed only when the runtime
# reports to it.
if(SR_ENABLE_PROFILER)
    find_package(SushiProfiler CONFIG REQUIRED)
    message(STATUS "Found SushiProfiler ${SushiProfiler_VERSION}: ${SushiProfiler_DIR}")
endif()
