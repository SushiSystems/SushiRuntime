# SushiRuntimeInstall.cmake
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026-present Mustafa Garip & Sushi Systems
# Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.
# Commercial use requires a licence from Sushi Systems.

# Runtime dependency staging for the installed package.
#
# install(TARGETS) puts sushiruntime.dll in the prefix and stops there, which is
# enough to *build* an out-of-tree consumer and not enough to *run* one: the
# library imports sycl8.dll and hwloc-15.dll directly, and the SYCL runtime loads
# its Unified Runtime adapters by name after that. On a developer machine those
# are all on PATH already, so the shortfall is invisible until the package
# reaches a machine where they are not.
#
# These libraries are third-party redistributables, so they install under their
# own component rather than the default set. Staging a tree locally is not
# distribution and `sr package` asks for the component; anyone producing an
# actual redistributable has to name it, and that is where the licensing question
# belongs. The SYCL toolchain bundle ships no licence or redistribution file, so
# that question is open — see docs/slop/ENGINE_BACKBONE_REFACTOR.md BB-5d.

set(SUSHIRUNTIME_REDIST_COMPONENT "sushiruntime_redist"
    CACHE STRING "Install component carrying third-party runtime libraries")

# Libraries the target graph cannot see. $<TARGET_RUNTIME_DLLS> resolves imported
# targets that name a runtime artifact, which covers neither of the two cases
# here: the UR adapters are dlopen'd by sycl8 at device-discovery time and appear
# in no import table, and hwloc arrives as PkgConfig::HWLOC, a pkg-config-derived
# target carrying link flags and a search path but no IMPORTED_LOCATION.
# Libraries an installed consumer cannot start without. These are the direct
# imports of sushiruntime.dll, minus what the target graph can already resolve:
# hwloc arrives as PkgConfig::HWLOC, a pkg-config-derived target carrying link
# flags and a search path but no IMPORTED_LOCATION.
set(SUSHIRUNTIME_REQUIRED_RUNTIME_LIBRARIES
    hwloc-15
    CACHE STRING "Runtime libraries the installed package cannot start without")

# The SYCL runtime, also a direct import, but its major version is part of the
# file name — sycl8.dll on one bundle, sycl9.dll on the next — so it is matched
# by pattern. Pinning the version produced a package that installed cleanly and
# then could not start, which is how a toolchain upgrade slipped past everything
# except the packaging oracle. The patterns deliberately do not match the debug
# variants (sycl9d.dll).
set(SUSHIRUNTIME_RUNTIME_LIBRARY_PATTERNS
    "sycl.dll" "sycl[0-9].dll" "sycl[0-9][0-9].dll"
    CACHE STRING "Filename patterns for the versioned SYCL runtime")

# Unified Runtime adapters. The SYCL runtime dlopen's these by name at device
# discovery, so they appear in no import table, and a bundle ships only the ones
# whose backend it supports — a newer bundle folded the loader into the SYCL
# runtime and dropped ur_loader and ur_adapter_opencl entirely. Absence is
# therefore normal and silent: it means that backend is not offered, not that the
# package is broken. Whether the package actually runs is the oracle's question,
# not this list's.
set(SUSHIRUNTIME_OPTIONAL_RUNTIME_LIBRARIES
    ur_loader
    ur_adapter_opencl
    ur_adapter_level_zero
    ur_adapter_level_zero_v2
    ur_win_proxy_loader
    CACHE STRING "Runtime libraries staged when present, skipped when not")

# Where to look for them: beside the SYCL compiler, and in the vcpkg tree. Both
# are derived rather than configured, so a machine that can build the runtime can
# also stage it without a second set of paths to keep in sync.
function(sushiruntime_runtime_library_search_directories out_var)
    set(directories "")

    get_filename_component(compiler_directory "${CMAKE_CXX_COMPILER}" DIRECTORY)
    if(compiler_directory)
        list(APPEND directories "${compiler_directory}")
    endif()

    # The vcpkg toolchain file sets VCPKG_INSTALLED_DIR as a normal variable, not
    # an environment one, so a tree configured through a preset has the location
    # even when VCPKG_ROOT never reached this process. Both are consulted because
    # only the second survives a manually configured tree.
    if(NOT VCPKG_TARGET_TRIPLET)
        set(VCPKG_TARGET_TRIPLET "x64-windows")
    endif()
    foreach(installed_root "${VCPKG_INSTALLED_DIR}" "${_VCPKG_INSTALLED_DIR}")
        if(installed_root)
            list(APPEND directories "${installed_root}/${VCPKG_TARGET_TRIPLET}/bin")
        endif()
    endforeach()
    if(DEFINED ENV{VCPKG_ROOT})
        list(APPEND directories
            "$ENV{VCPKG_ROOT}/installed/${VCPKG_TARGET_TRIPLET}/bin")
    endif()

    # hwloc's own resolved location, as a last resort: pkg-config gives a library
    # directory, and vcpkg puts the import library in lib/ with the DLL in bin/.
    if(HWLOC_LIBRARY_DIRS)
        foreach(library_directory IN LISTS HWLOC_LIBRARY_DIRS)
            get_filename_component(vcpkg_prefix "${library_directory}" DIRECTORY)
            list(APPEND directories "${vcpkg_prefix}/bin")
        endforeach()
    endif()

    list(REMOVE_DUPLICATES directories)
    set(${out_var} "${directories}" PARENT_SCOPE)
endfunction()

# Adds install rules for the third-party libraries an installed SushiRuntime
# needs at run time.
#
# A library that cannot be found is a warning, not an error: a lane configured
# without SYCL still has to configure, and the packaging oracle is what turns a
# genuinely incomplete tree into a failure. Failing here instead would only move
# the report to a place where it cannot tell whether the library was missing or
# merely irrelevant to this configuration.
function(sushiruntime_install_runtime_dependencies)
    if(NOT WIN32)
        # The ELF loader follows RPATH and the distributions carry these as real
        # packages, so a POSIX consumer resolves them without a private copy.
        return()
    endif()

    sushiruntime_runtime_library_search_directories(search_directories)

    foreach(library IN LISTS SUSHIRUNTIME_REQUIRED_RUNTIME_LIBRARIES
                              SUSHIRUNTIME_OPTIONAL_RUNTIME_LIBRARIES)
        set(found "found-NOTFOUND")
        find_file(found
            NAMES "${library}.dll"
            PATHS ${search_directories}
            NO_DEFAULT_PATH
        )
        if(found)
            install(FILES "${found}"
                DESTINATION "${CMAKE_INSTALL_BINDIR}"
                COMPONENT "${SUSHIRUNTIME_REDIST_COMPONENT}"
            )
        elseif(library IN_LIST SUSHIRUNTIME_REQUIRED_RUNTIME_LIBRARIES)
            message(WARNING
                "sushiruntime: ${library}.dll not found in [${search_directories}]. "
                "The installed package will not run on a machine that lacks it.")
        endif()
        unset(found CACHE)
    endforeach()

    set(matched_patterns "")
    foreach(directory IN LISTS search_directories)
        foreach(pattern IN LISTS SUSHIRUNTIME_RUNTIME_LIBRARY_PATTERNS)
            file(GLOB matches "${directory}/${pattern}")
            foreach(match IN LISTS matches)
                list(APPEND matched_patterns "${match}")
            endforeach()
        endforeach()
    endforeach()

    if(matched_patterns)
        list(REMOVE_DUPLICATES matched_patterns)
        install(FILES ${matched_patterns}
            DESTINATION "${CMAKE_INSTALL_BINDIR}"
            COMPONENT "${SUSHIRUNTIME_REDIST_COMPONENT}"
        )
    else()
        message(WARNING
            "sushiruntime: no SYCL runtime library matched "
            "[${SUSHIRUNTIME_RUNTIME_LIBRARY_PATTERNS}] in [${search_directories}]. "
            "The installed package will not run on a machine that lacks it.")
    endif()
endfunction()
