# SushiRuntimeLibrary.cmake
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026-present Mustafa Garip & Sushi Systems
# Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.
# Commercial use requires a licence from Sushi Systems.

# Composes the module object libraries into the one shared library.
# cmake/SushiRuntimeInstall.cmake installs and exports it.

# Declares the shared library `sushiruntime` from the object libraries the modules recorded;
# `sushiruntime_umbrella` (alias SushiRuntime::SushiRuntime), which links every public
# module target; and `sushiruntime_internal_headers`, which adds the include roots of the
# INTERNAL modules for the programs of this tree that test them.
function(sushiruntime_compose_library)
    # Before 1.0 a minor release may break the ABI, so the minor number is part of the name
    # a program is linked against.
    if(PROJECT_VERSION_MAJOR EQUAL 0)
        set(SR_SOVERSION "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}")
    else()
        set(SR_SOVERSION "${PROJECT_VERSION_MAJOR}")
    endif()

    add_library(sushiruntime SHARED)
    # The name the export gives it, so a consumer names the file the same way in-tree.
    add_library(SushiRuntime::shared_library ALIAS sushiruntime)

    get_property(object_targets GLOBAL PROPERTY SUSHIRUNTIME_MODULE_OBJECT_TARGETS)
    get_property(internal_roots GLOBAL PROPERTY SUSHIRUNTIME_INTERNAL_INCLUDE_ROOTS)
    get_property(externals      GLOBAL PROPERTY SUSHIRUNTIME_MODULE_EXTERNAL_LIBRARIES)
    foreach(objects IN LISTS object_targets)
        target_sources(sushiruntime PRIVATE "$<TARGET_OBJECTS:${objects}>")
    endforeach()
    # The library carries no include root: each public module target carries its own, so
    # a consumer sees the headers of the modules it links and no other. The library's own
    # sources are compiled by the module object libraries, with their declared roots.
    # One spelling for an in-tree consumer (add_subdirectory) and an installed one
    # (find_package).
    get_property(module_targets GLOBAL PROPERTY SUSHIRUNTIME_EXPORTED_MODULE_TARGETS)
    add_library(sushiruntime_umbrella INTERFACE)
    add_library(SushiRuntime::SushiRuntime ALIAS sushiruntime_umbrella)
    target_link_libraries(sushiruntime_umbrella INTERFACE ${module_targets} sushiruntime)
    set_target_properties(sushiruntime_umbrella PROPERTIES EXPORT_NAME SushiRuntime)

    # Neither exported nor installed: cmake/SushiRuntimeTests.cmake links it to a test.
    add_library(sushiruntime_internal_headers INTERFACE)
    foreach(include_root IN LISTS internal_roots)
        target_include_directories(sushiruntime_internal_headers INTERFACE "${include_root}")
    endforeach()
    target_link_libraries(sushiruntime_internal_headers INTERFACE sushiruntime_umbrella)

    sushiruntime_apply_build_policy(sushiruntime)
    sushiruntime_apply_sycl(sushiruntime)

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
        # The umbrella is SushiRuntime::SushiRuntime; the library itself is named after
        # what it is.
        EXPORT_NAME shared_library)
endfunction()
