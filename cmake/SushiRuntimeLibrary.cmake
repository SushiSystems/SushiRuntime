# SushiRuntimeLibrary.cmake
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026-present Mustafa Garip & Sushi Systems
# Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.
# Commercial use requires a licence from Sushi Systems.

# Composes the module object libraries into the one shared library, writes the forwarding
# headers of moved include paths. cmake/SushiRuntimeInstall.cmake installs and exports it.

include("${CMAKE_CURRENT_LIST_DIR}/SushiRuntimeMovedHeaders.cmake")

# Old include paths resolve through this root. Only the library target carries it, so a
# module cannot include an old path.
set(SR_FORWARDING_INCLUDE_ROOT "${PROJECT_BINARY_DIR}/forwarding/include")

# Writes one forwarding header per row of SUSHIRUNTIME_MOVED_HEADERS.
function(sushiruntime_write_forwarding_headers)
    foreach(row IN LISTS SUSHIRUNTIME_MOVED_HEADERS)
        string(REPLACE "|" ";" paths "${row}")
        list(POP_FRONT paths old_path)
        list(GET paths 0 SR_FORWARDING_FIRST_TARGET)
        get_filename_component(SR_FORWARDING_FILE_NAME "${old_path}" NAME)
        set(SR_FORWARDING_INCLUDES "")
        foreach(new_path IN LISTS paths)
            string(APPEND SR_FORWARDING_INCLUDES "#include <${new_path}>\n")
        endforeach()
        configure_file(
            "${SR_CMAKE_DIR}/SushiRuntimeForwardingHeader.hpp.in"
            "${SR_FORWARDING_INCLUDE_ROOT}/${old_path}"
            @ONLY)
    endforeach()
endfunction()

# Declares the shared library `sushiruntime` (alias SushiRuntime::SushiRuntime) from the
# object libraries the modules recorded.
function(sushiruntime_compose_library)
    sushiruntime_write_forwarding_headers()

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
    get_property(include_roots  GLOBAL PROPERTY SUSHIRUNTIME_INSTALLED_INCLUDE_ROOTS)
    get_property(externals      GLOBAL PROPERTY SUSHIRUNTIME_MODULE_EXTERNAL_LIBRARIES)
    foreach(objects IN LISTS object_targets)
        target_sources(sushiruntime PRIVATE "$<TARGET_OBJECTS:${objects}>")
    endforeach()
    foreach(include_root IN LISTS include_roots)
        target_include_directories(sushiruntime PUBLIC "$<BUILD_INTERFACE:${include_root}>")
    endforeach()
    target_include_directories(sushiruntime PUBLIC
        "$<BUILD_INTERFACE:${SR_FORWARDING_INCLUDE_ROOT}>"
        "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>")

    sushiruntime_apply_build_policy(sushiruntime)

    # The floating-point contract is PUBLIC: the kernels are header templates a consumer
    # instantiates in its own translation units. See cmake/Determinism.cmake.
    target_link_libraries(sushiruntime PUBLIC
        sushiruntime_deterministic_fp
        IntelSYCL::SYCL_CXX)

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
