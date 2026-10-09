# SushiRuntimeModule.cmake
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026-present Mustafa Garip & Sushi Systems
# Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.
# Commercial use requires a licence from Sushi Systems.

# One function declares a module. A module is an INTERFACE target that carries its include
# root and the roots of the modules it depends on, and an OBJECT library of its sources that
# is compiled with those roots only. cmake/SushiRuntimeLibrary.cmake composes the object
# libraries into the one shared library.

include("${CMAKE_CURRENT_LIST_DIR}/SushiRuntimeTiers.cmake")

# Refuses a dependency the tier table does not allow.
function(sushiruntime_check_module_edge consumer dependency)
    sushiruntime_module_tier("${dependency}" dependency_tier)
    if(dependency_tier STREQUAL "")
        message(FATAL_ERROR
            "sushiruntime_add_module(${consumer}): depends on '${dependency}', which "
            "SUSHIRUNTIME_MODULE_TIERS (cmake/SushiRuntimeTiers.cmake) does not list.")
    endif()
    sushiruntime_module_tier("${consumer}" consumer_tier)
    sushiruntime_tier_index("${consumer_tier}" consumer_index)
    sushiruntime_tier_index("${dependency_tier}" dependency_index)
    if(dependency_index GREATER consumer_index)
        message(FATAL_ERROR
            "sushiruntime_add_module(${consumer}): upward dependency ${consumer} "
            "(${consumer_tier}) -> ${dependency} (${dependency_tier}). Move the shared "
            "code down a tier.")
    endif()
    if(dependency_index EQUAL consumer_index)
        sushiruntime_same_tier_edge_allowed("${consumer}" "${dependency}" allowed)
        if(NOT allowed)
            message(FATAL_ERROR
                "sushiruntime_add_module(${consumer}): undeclared edge ${consumer} -> "
                "${dependency} inside the ${consumer_tier} tier. List it in "
                "SUSHIRUNTIME_SAME_TIER_EDGES (cmake/SushiRuntimeTiers.cmake) or move "
                "the shared code down a tier.")
        endif()
    endif()
    if(NOT TARGET sushiruntime_module_${dependency})
        message(FATAL_ERROR
            "sushiruntime_add_module(${consumer}): '${dependency}' is not declared yet. "
            "It is switched off, or it comes after ${consumer} in "
            "SUSHIRUNTIME_MODULE_TIERS; a dependency is listed before its consumer.")
    endif()
endfunction()

# sushiruntime_add_module(
#     NAME <module> TIER <tier>
#     [HEADER_ONLY] [SYCL]
#     [SOURCES <file under source/>...]
#     [DEPENDS <module>...] [PRIVATE_DEPENDS <module>...]
#     [EXTERNAL <third-party target>...])
#
# DEPENDS are the modules the public headers include; PRIVATE_DEPENDS the ones only
# source/ includes. SYCL marks a module whose sources include a SYCL header.
function(sushiruntime_add_module)
    cmake_parse_arguments(ARG "HEADER_ONLY;SYCL" "NAME;TIER"
        "SOURCES;DEPENDS;PRIVATE_DEPENDS;EXTERNAL" ${ARGN})
    if("${ARG_NAME}" STREQUAL "" OR "${ARG_TIER}" STREQUAL "")
        message(FATAL_ERROR "sushiruntime_add_module(): NAME and TIER are required.")
    endif()
    if(ARG_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR
            "sushiruntime_add_module(${ARG_NAME}): unexpected '${ARG_UNPARSED_ARGUMENTS}'.")
    endif()
    sushiruntime_module_tier("${ARG_NAME}" table_tier)
    if(NOT "${ARG_TIER}" STREQUAL "${table_tier}")
        message(FATAL_ERROR
            "sushiruntime_add_module(${ARG_NAME}): declares TIER '${ARG_TIER}'; "
            "cmake/SushiRuntimeTiers.cmake says '${table_tier}'.")
    endif()

    set(sources "")
    foreach(source IN LISTS ARG_SOURCES)
        list(APPEND sources "${CMAKE_CURRENT_SOURCE_DIR}/source/${source}")
    endforeach()
    if(ARG_HEADER_ONLY AND sources)
        message(FATAL_ERROR
            "sushiruntime_add_module(${ARG_NAME}): HEADER_ONLY and SOURCES exclude each other.")
    endif()
    if(NOT ARG_HEADER_ONLY AND NOT sources)
        message(FATAL_ERROR
            "sushiruntime_add_module(${ARG_NAME}): no SOURCES; mark it HEADER_ONLY.")
    endif()

    foreach(dependency IN LISTS ARG_DEPENDS ARG_PRIVATE_DEPENDS)
        sushiruntime_check_module_edge("${ARG_NAME}" "${dependency}")
    endforeach()

    set(target "sushiruntime_module_${ARG_NAME}")
    set(include_root "${CMAKE_CURRENT_SOURCE_DIR}/include")
    add_library(${target} INTERFACE)
    target_include_directories(${target} INTERFACE "$<BUILD_INTERFACE:${include_root}>")
    foreach(dependency IN LISTS ARG_DEPENDS)
        target_link_libraries(${target} INTERFACE "sushiruntime_module_${dependency}")
    endforeach()
    set_property(GLOBAL APPEND PROPERTY SUSHIRUNTIME_INSTALLED_INCLUDE_ROOTS "${include_root}")

    if(sources)
        set(objects "${target}_objects")
        add_library(${objects} OBJECT ${sources})
        target_link_libraries(${objects} PRIVATE
            ${target} sushiruntime_deterministic_fp IntelSYCL::SYCL_CXX ${ARG_EXTERNAL})
        foreach(dependency IN LISTS ARG_PRIVATE_DEPENDS)
            target_link_libraries(${objects} PRIVATE "sushiruntime_module_${dependency}")
        endforeach()
        target_include_directories(${objects} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/source")
        target_compile_definitions(${objects} PRIVATE
            SUSHIRUNTIME_EXPORTS NOMINMAX WIN32_LEAN_AND_MEAN
            $<$<BOOL:${SR_ENABLE_DISTRIBUTED}>:SR_DISTRIBUTED>)
        sushiruntime_apply_build_policy(${objects})
        set_target_properties(${objects} PROPERTIES
            CXX_STANDARD 17
            CXX_STANDARD_REQUIRED ON
            POSITION_INDEPENDENT_CODE ON)
        if(ARG_SYCL)
            add_sycl_to_target(TARGET ${objects} SOURCES ${sources})
        endif()
        set_property(GLOBAL APPEND PROPERTY SUSHIRUNTIME_MODULE_OBJECT_TARGETS ${objects})
        set_property(GLOBAL APPEND PROPERTY
            SUSHIRUNTIME_MODULE_EXTERNAL_LIBRARIES ${ARG_EXTERNAL})
    endif()
endfunction()
