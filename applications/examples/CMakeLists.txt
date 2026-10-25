# CMakeLists.txt
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026-present Mustafa Garip & Sushi Systems
# Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.
# Commercial use requires a licence from Sushi Systems.

cmake_minimum_required(VERSION 3.20)

# Each example is one self-contained binary linked against the library, with the
# SYCL policy of cmake/SushiRuntimeSyclTarget.cmake applied. The standalone
# tutorials always build; the distributed demo only when the master-worker layer
# is enabled (it links the distributed entry points). Target names follow the
# file names: quickstart.cpp -> sr_quickstart,
# <topic>_demo.cpp -> sr_<topic>_demo. The adapter example is the one target
# built from two source files, both under adapter/.

# On Windows the examples need sushiruntime.dll beside them to run from the build
# tree. Staged ONCE by a target every example depends on, not by a POST_BUILD step
# on each: every example writes to the same output directory, so per-target copies
# race in a parallel build — several links finish at the same moment, each tries
# to write (and vcpkg's applocal pass to read) the same sushiruntime.dll, and
# Windows answers the loser with a sharing violation. It only bites a clean build,
# because an incremental one rarely links two examples at once, which is exactly
# the kind of failure that shows up on someone else's machine.
if(WIN32)
    # Where the example binaries land. The per-target copy this replaces got it
    # from $<TARGET_FILE_DIR:...>, which is per-configuration under a multi-config
    # generator; resolved here instead, since the staging target is not tied to
    # any one example.
    if(CMAKE_CONFIGURATION_TYPES)
        set(SR_EXAMPLES_STAGE_DIR "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>")
    else()
        set(SR_EXAMPLES_STAGE_DIR "${CMAKE_CURRENT_BINARY_DIR}")
    endif()

    add_custom_target(sushiruntime_examples_runtime_dll
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:sushiruntime>
            $<TARGET_FILE:SushiLog::SushiLog>
            "${SR_EXAMPLES_STAGE_DIR}"
        COMMENT "Staging sushiruntime.dll and sushilog.dll next to the examples")
    add_dependencies(sushiruntime_examples_runtime_dll sushiruntime)
endif()

# sushiruntime_add_example(<name> DEVICE_SOURCES <file>... [SOURCES <file>...])
#   Builds one example from its DEVICE_SOURCES, the units that instantiate a kernel, and its
#   SOURCES, the units that do not, with C++17, the library link and the Windows DLL staging.
function(sushiruntime_add_example name)
    cmake_parse_arguments(ARG "" "" "SOURCES;DEVICE_SOURCES" ${ARGN})
    set(host_sources "")
    set(device_sources "")
    foreach(source IN LISTS ARG_SOURCES)
        list(APPEND host_sources "${CMAKE_CURRENT_SOURCE_DIR}/${source}")
    endforeach()
    foreach(source IN LISTS ARG_DEVICE_SOURCES)
        list(APPEND device_sources "${CMAKE_CURRENT_SOURCE_DIR}/${source}")
    endforeach()
    add_executable(${name} ${device_sources} ${host_sources})
    sushiruntime_apply_build_policy(${name})
    target_link_libraries(${name} PRIVATE SushiRuntime::SushiRuntime)
    set_target_properties(${name} PROPERTIES
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED ON)
    sushiruntime_apply_sycl(${name}
        SOURCES ${device_sources} ${host_sources} DEVICE_SOURCES ${device_sources})
    if(WIN32)
        # A dependency, so the staging runs before this target links and its
        # applocal pass reads the directory.
        add_dependencies(${name} sushiruntime_examples_runtime_dll)
    endif()
endfunction()

sushiruntime_add_example(sr_quickstart DEVICE_SOURCES quickstart.cpp)
sushiruntime_add_example(sr_vecadd_demo DEVICE_SOURCES vecadd_demo.cpp)
sushiruntime_add_example(sr_stencil_demo DEVICE_SOURCES stencil_demo.cpp)
sushiruntime_add_example(sr_coupled_fields_demo DEVICE_SOURCES coupled_fields_demo.cpp)
sushiruntime_add_example(sr_game_engine_demo DEVICE_SOURCES game_engine_demo.cpp)
sushiruntime_add_example(sr_streaming_world_demo DEVICE_SOURCES streaming_world_demo.cpp)
sushiruntime_add_example(sr_operating_path_demo DEVICE_SOURCES operating_path_demo.cpp)
sushiruntime_add_example(sr_errors_demo DEVICE_SOURCES errors_demo.cpp)
sushiruntime_add_example(sr_tasks_demo DEVICE_SOURCES tasks_demo.cpp)
sushiruntime_add_example(sr_stepping_demo DEVICE_SOURCES stepping_demo.cpp)
sushiruntime_add_example(sr_data_access_demo DEVICE_SOURCES data_access_demo.cpp)
sushiruntime_add_example(sr_extension_demo DEVICE_SOURCES extension_demo.cpp)
sushiruntime_add_example(sr_host_hooks_demo DEVICE_SOURCES host_hooks_demo.cpp)
sushiruntime_add_example(sr_adapter_demo
    DEVICE_SOURCES adapter/sushiruntime_adapter.cpp
    SOURCES adapter/adapter_demo.cpp)

if(SR_ENABLE_DISTRIBUTED)
    sushiruntime_add_example(sr_distributed_demo DEVICE_SOURCES distributed_demo.cpp)
    sushiruntime_add_example(sr_distributed_worker DEVICE_SOURCES distributed_worker.cpp)
endif()
