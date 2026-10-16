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

# SushiLog, the one logger of the process, exposed as SushiLog::SushiLog. The CLI resolves its
# pin in cli/sushistack.deps.toml and passes SUSHILOG_DIR: a checkout is built in this tree, an
# install prefix is found as a package. Nothing is downloaded here.
if(NOT TARGET SushiLog::SushiLog)
    if(SUSHILOG_DIR AND EXISTS "${SUSHILOG_DIR}/CMakeLists.txt")
        set(SL_BUILD_TESTS OFF)
        add_subdirectory("${SUSHILOG_DIR}" "${PROJECT_BINARY_DIR}/sushilog")
    else()
        find_package(SushiLog CONFIG HINTS "${SUSHILOG_DIR}")
    endif()
endif()
if(NOT TARGET SushiLog::SushiLog)
    message(FATAL_ERROR
        "SushiLog was not found (SUSHILOG_DIR='${SUSHILOG_DIR}'). Run `sr setup`, which "
        "fetches the version cli/sushistack.deps.toml pins, or set SUSHILOG_DIR to a SushiLog "
        "checkout or install prefix.")
endif()
message(STATUS "Found SushiLog: ${SUSHILOG_DIR}")
