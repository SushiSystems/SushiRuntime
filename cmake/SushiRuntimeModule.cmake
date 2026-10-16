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

# sushiruntime_add_header_check(MODULE <module> INCLUDE_ROOT <dir> [EXCLUDE <header>...]
#                               [EXTERNAL <third-party target>...])
#
# Compiles every public header of a module first and alone, with the include roots of the
# module's DEPENDS only. A header that forgot one of its own includes, or that includes a
# module the public surface did not declare, fails to compile here. A module that is not
# INTERNAL has only modules of its kind under DEPENDS, so a header it installs that includes
# an internal one fails here too. The headers are globbed so that the check does not depend
# on someone listing a new one. EXTERNAL names the third-party targets an internal module's
# headers include; an installed header includes none.
function(sushiruntime_add_header_check)
    cmake_parse_arguments(ARG "" "MODULE;INCLUDE_ROOT" "EXCLUDE;EXTERNAL" ${ARGN})

    file(GLOB_RECURSE headers CONFIGURE_DEPENDS RELATIVE "${ARG_INCLUDE_ROOT}"
        "${ARG_INCLUDE_ROOT}/*.hpp" "${ARG_INCLUDE_ROOT}/*.h")
    if(ARG_EXCLUDE)
        list(REMOVE_ITEM headers ${ARG_EXCLUDE})
    endif()
    if(NOT headers)
        return()
    endif()

    set(units "")
    foreach(header IN LISTS headers)
        string(MAKE_C_IDENTIFIER "${header}" unit_name)
        set(unit "${CMAKE_CURRENT_BINARY_DIR}/header_check/${unit_name}.cpp")
        file(CONFIGURE OUTPUT "${unit}" CONTENT "#include <${header}>\n" @ONLY)
        list(APPEND units "${unit}")
    endforeach()

    set(check "sushiruntime_module_${ARG_MODULE}_header_check")
    add_library(${check} OBJECT ${units})
    target_link_libraries(${check} PRIVATE
        "sushiruntime_module_${ARG_MODULE}" sushiruntime_deterministic_fp IntelSYCL::SYCL_CXX
        ${ARG_EXTERNAL})
    target_compile_definitions(${check} PRIVATE
        NOMINMAX WIN32_LEAN_AND_MEAN
        $<$<BOOL:${SR_ENABLE_DISTRIBUTED}>:SR_DISTRIBUTED>)
    sushiruntime_apply_build_policy(${check})
    set_target_properties(${check} PROPERTIES
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED ON
        POSITION_INDEPENDENT_CODE ON)
    add_sycl_to_target(TARGET ${check} SOURCES ${units})
endfunction()

# sushiruntime_add_module(
#     NAME <module> TIER <tier>
#     [INTERNAL] [HEADER_ONLY] [SYCL]
#     [SOURCES <file under source/>...]
#     [DEPENDS <module>...] [PRIVATE_DEPENDS <module>...]
#     [EXTERNAL <third-party target>...] [PUBLIC_EXTERNAL <library target>...]
#     [HEADER_CHECK_EXCLUDE <header path under include/>...])
#
# DEPENDS are the modules the public headers include; PRIVATE_DEPENDS the ones only
# source/ includes. SYCL marks a module whose sources include a SYCL header.
# INTERNAL marks a module whose headers are for the other modules of the library: its
# include root reaches them and the tests, and is not installed. A module without the mark
# is installed, and may name an INTERNAL module under PRIVATE_DEPENDS only.
# HEADER_CHECK_EXCLUDE names a header the header check must not compile alone. The check
# is built for every module that is not INTERNAL, and for the others under SR_BUILD_TESTS.
# PUBLIC_EXTERNAL names a library the public headers include; it reaches every consumer of
# the module and of the library.
# TEST_ONLY marks an INTERNAL module whose sources build into the test programs and never
# into the library.
function(sushiruntime_add_module)
    cmake_parse_arguments(ARG "INTERNAL;HEADER_ONLY;SYCL;TEST_ONLY" "NAME;TIER"
        "SOURCES;DEPENDS;PRIVATE_DEPENDS;EXTERNAL;PUBLIC_EXTERNAL;HEADER_CHECK_EXCLUDE" ${ARGN})
    if("${ARG_NAME}" STREQUAL "" OR "${ARG_TIER}" STREQUAL "")
        message(FATAL_ERROR "sushiruntime_add_module(): NAME and TIER are required.")
    endif()
    if(ARG_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR
            "sushiruntime_add_module(${ARG_NAME}): unexpected '${ARG_UNPARSED_ARGUMENTS}'.")
    endif()
    if(ARG_TEST_ONLY AND NOT ARG_INTERNAL)
        message(FATAL_ERROR
            "sushiruntime_add_module(${ARG_NAME}): TEST_ONLY needs INTERNAL; a module only "
            "the tests link is never installed.")
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

    # A dependency is declared before its consumer, so the list is complete for this module.
    get_property(internal_modules GLOBAL PROPERTY SUSHIRUNTIME_INTERNAL_MODULES)
    if(ARG_INTERNAL)
        set_property(GLOBAL APPEND PROPERTY SUSHIRUNTIME_INTERNAL_MODULES "${ARG_NAME}")
    else()
        foreach(dependency IN LISTS ARG_DEPENDS)
            if(dependency IN_LIST internal_modules)
                message(FATAL_ERROR
                    "sushiruntime_add_module(${ARG_NAME}): lists the INTERNAL module "
                    "'${dependency}' under DEPENDS. ${ARG_NAME} is installed and "
                    "${dependency} is not, so no header of ${ARG_NAME} may include it. Move "
                    "it to PRIVATE_DEPENDS, or move the type the header needs to a public "
                    "module.")
            endif()
        endforeach()
    endif()

    set(target "sushiruntime_module_${ARG_NAME}")
    set(include_root "${CMAKE_CURRENT_SOURCE_DIR}/include")
    add_library(${target} INTERFACE)
    target_include_directories(${target} INTERFACE "$<BUILD_INTERFACE:${include_root}>")
    foreach(dependency IN LISTS ARG_DEPENDS)
        target_link_libraries(${target} INTERFACE "sushiruntime_module_${dependency}")
    endforeach()
    if(ARG_PUBLIC_EXTERNAL)
        target_link_libraries(${target} INTERFACE ${ARG_PUBLIC_EXTERNAL})
        set_property(GLOBAL APPEND PROPERTY
            SUSHIRUNTIME_PUBLIC_EXTERNAL_LIBRARIES ${ARG_PUBLIC_EXTERNAL})
    endif()
    # A public root reaches every consumer of the library and is installed; an internal
    # one reaches the modules that name it and the programs of this tree that test it.
    if(ARG_INTERNAL)
        set_property(GLOBAL APPEND PROPERTY SUSHIRUNTIME_INTERNAL_INCLUDE_ROOTS "${include_root}")
    else()
        set_property(GLOBAL APPEND PROPERTY SUSHIRUNTIME_PUBLIC_INCLUDE_ROOTS "${include_root}")
    endif()

    if(sources)
        set(objects "${target}_objects")
        add_library(${objects} OBJECT ${sources})
        target_link_libraries(${objects} PRIVATE
            ${target} sushiruntime_deterministic_fp IntelSYCL::SYCL_CXX ${ARG_EXTERNAL})
        foreach(dependency IN LISTS ARG_PRIVATE_DEPENDS)
            target_link_libraries(${objects} PRIVATE "sushiruntime_module_${dependency}")
        endforeach()
        target_include_directories(${objects} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/source")
        # A test-only module is compiled as a consumer of the library, so it imports the
        # library's symbols instead of exporting them.
        target_compile_definitions(${objects} PRIVATE
            $<$<NOT:$<BOOL:${ARG_TEST_ONLY}>>:SUSHIRUNTIME_EXPORTS> NOMINMAX WIN32_LEAN_AND_MEAN
            $<$<BOOL:${SR_ENABLE_DISTRIBUTED}>:SR_DISTRIBUTED>)
        sushiruntime_apply_build_policy(${objects})
        set_target_properties(${objects} PROPERTIES
            CXX_STANDARD 17
            CXX_STANDARD_REQUIRED ON
            POSITION_INDEPENDENT_CODE ON
            CXX_VISIBILITY_PRESET hidden
            VISIBILITY_INLINES_HIDDEN ON)
        if(ARG_SYCL)
            add_sycl_to_target(TARGET ${objects} SOURCES ${sources})
        endif()
        if(ARG_TEST_ONLY)
            target_link_libraries(${objects} PRIVATE sushiruntime_internal_headers)
            set_property(GLOBAL APPEND PROPERTY SUSHIRUNTIME_TEST_OBJECT_TARGETS ${objects})
        else()
            set_property(GLOBAL APPEND PROPERTY SUSHIRUNTIME_MODULE_OBJECT_TARGETS ${objects})
            set_property(GLOBAL APPEND PROPERTY
                SUSHIRUNTIME_MODULE_EXTERNAL_LIBRARIES ${ARG_EXTERNAL})
        endif()
    endif()

    if(SR_BUILD_TESTS OR NOT ARG_INTERNAL)
        set(header_externals "")
        if(ARG_INTERNAL)
            set(header_externals ${ARG_EXTERNAL})
        endif()
        sushiruntime_add_header_check(
            MODULE ${ARG_NAME} INCLUDE_ROOT "${include_root}"
            EXCLUDE ${ARG_HEADER_CHECK_EXCLUDE} EXTERNAL ${header_externals})
    endif()
endfunction()
