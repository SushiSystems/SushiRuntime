# SushiRuntimeLibrary.cmake
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026-present Mustafa Garip & Sushi Systems
# Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.
# Commercial use requires a licence from Sushi Systems.

# Composes the module object libraries into the one shared library.
# cmake/SushiRuntimeInstall.cmake installs and exports it.

# Declares the shared library `sushiruntime` (alias SushiRuntime::SushiRuntime) from the
# object libraries the modules recorded, and `sushiruntime_internal_headers`, which adds
# the include roots of the INTERNAL modules for the programs of this tree that test them.
function(sushiruntime_compose_library)
    # Before 1.0 a minor release may break the ABI, so the minor number is part of the name
    # a program is linked against.
    if(PROJECT_VERSION_MAJOR EQUAL 0)
        set(SR_SOVERSION "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}")
    else()
        set(SR_SOVERSION "${PROJECT_VERSION_MAJOR}")
    endif()

    add_library(sushiruntime SHARED)
    # One spelling for an in-tree consumer (add_subdirectory) and an installed one
    # (find_package).
    add_library(SushiRuntime::SushiRuntime ALIAS sushiruntime)

    get_property(object_targets GLOBAL PROPERTY SUSHIRUNTIME_MODULE_OBJECT_TARGETS)
    get_property(public_roots   GLOBAL PROPERTY SUSHIRUNTIME_PUBLIC_INCLUDE_ROOTS)
    get_property(internal_roots GLOBAL PROPERTY SUSHIRUNTIME_INTERNAL_INCLUDE_ROOTS)
    get_property(externals      GLOBAL PROPERTY SUSHIRUNTIME_MODULE_EXTERNAL_LIBRARIES)
    foreach(objects IN LISTS object_targets)
        target_sources(sushiruntime PRIVATE "$<TARGET_OBJECTS:${objects}>")
    endforeach()
    # A consumer of the build tree sees the roots an installed one does, and no other.
    foreach(include_root IN LISTS public_roots)
        target_include_directories(sushiruntime PUBLIC "$<BUILD_INTERFACE:${include_root}>")
    endforeach()
    target_include_directories(sushiruntime PUBLIC
        "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>")

    # Neither exported nor installed: cmake/SushiRuntimeTests.cmake links it to a test.
    add_library(sushiruntime_internal_headers INTERFACE)
    foreach(include_root IN LISTS internal_roots)
        target_include_directories(sushiruntime_internal_headers INTERFACE "${include_root}")
    endforeach()
    target_link_libraries(sushiruntime_internal_headers INTERFACE sushiruntime)

    sushiruntime_apply_build_policy(sushiruntime)

    # The floating-point contract is PUBLIC: the kernels are header templates a consumer
    # instantiates in its own translation units. See cmake/Determinism.cmake.
    target_link_libraries(sushiruntime PUBLIC
        sushiruntime_deterministic_fp
        IntelSYCL::SYCL_CXX)

    # A library a public header includes is part of the link interface a consumer sees.
    get_property(public_externals GLOBAL PROPERTY SUSHIRUNTIME_PUBLIC_EXTERNAL_LIBRARIES)
    list(REMOVE_DUPLICATES public_externals)
    target_link_libraries(sushiruntime PUBLIC ${public_externals})

    # A third-party library of a module is an implementation detail of the shared
    # library. BUILD_INTERFACE keeps it out of the exported link interface, where a
    # consumer could not define its target.
    list(REMOVE_DUPLICATES externals)
    foreach(external IN LISTS externals)
        target_link_libraries(sushiruntime PRIVATE "$<BUILD_INTERFACE:${external}>")
    endforeach()

    # No header reads SR_DISTRIBUTED; it tells a consumer whether
    # <SushiRuntime/distributed/cluster.hpp> has a library behind it.
    if(SR_ENABLE_DISTRIBUTED)
        target_compile_definitions(sushiruntime PUBLIC SR_DISTRIBUTED)
    endif()

    set_target_properties(sushiruntime PROPERTIES
        VERSION ${PROJECT_VERSION}
        SOVERSION ${SR_SOVERSION}
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED ON
        POSITION_INDEPENDENT_CODE ON
        LINKER_LANGUAGE CXX
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN ON
        OUTPUT_NAME "sushiruntime"
        # install(EXPORT ... NAMESPACE SushiRuntime::) would otherwise produce
        # SushiRuntime::sushiruntime, a different spelling from the in-tree alias.
        EXPORT_NAME SushiRuntime)
endfunction()
