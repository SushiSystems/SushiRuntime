# SushiRuntimeSyclTarget.cmake
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026-present Mustafa Garip & Sushi Systems
# Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.
# Commercial use requires a licence from Sushi Systems.

# The SYCL policy of this tree's targets, in one place. A unit compiles for the device only
# when it instantiates a kernel and its target names it in DEVICE_SOURCES; every other unit
# compiles for the host, and finds <sycl/sycl.hpp> through the include directory cmake/Sycl.cmake
# puts on IntelSYCL::SYCL_CXX. An executable or a shared library links with the device options,
# which is what pulls the device images out of the objects that carry them. The device targets
# come from sushiruntime_gpu_offload (cmake/gpu/GpuBackend.cmake). Included after
# sushiruntime_gpu_finalize().

# SR_SYCL_PER_SOURCE: whether a single unit can be given the device pass. It can on intel-llvm,
# where -fsycl is an option of one unit, once the toolchain's SYCL headers are found. On the
# other toolchains add_sycl_to_target() works on whole targets, and every unit takes the pass.
if(SR_SYCL_TOOLCHAIN STREQUAL "intel-llvm" AND SR_SYCL_INCLUDE_DIR)
    set(SR_SYCL_PER_SOURCE ON)
else()
    set(SR_SYCL_PER_SOURCE OFF)
    message(STATUS "SushiRuntime: every unit takes the device pass "
                   "(toolchain='${SR_SYCL_TOOLCHAIN}', sycl headers='${SR_SYCL_INCLUDE_DIR}')")
endif()

if(CMAKE_CXX_FLAGS MATCHES "(^| )-fsycl( |$)")
    message(WARNING
        "CMAKE_CXX_FLAGS holds -fsycl, so every unit of this tree takes the device pass. "
        "SushiRuntime gives -fsycl to the units that instantiate a kernel; drop it from "
        "CMAKE_CXX_FLAGS.")
endif()

# sushiruntime_sycl_device_options(<out_compile> <out_link>)
#   Returns the options a device unit compiles with and a binary links with: -fsycl on
#   intel-llvm, where no package target carries it, and the device targets of
#   sushiruntime_gpu_offload.
function(sushiruntime_sycl_device_options out_compile out_link)
    set(compile "")
    set(link "")
    if(SR_SYCL_TOOLCHAIN STREQUAL "intel-llvm")
        set(compile -fsycl)
        set(link -fsycl)
    endif()
    if(TARGET sushiruntime_gpu_offload)
        get_target_property(offload_compile sushiruntime_gpu_offload INTERFACE_COMPILE_OPTIONS)
        get_target_property(offload_link sushiruntime_gpu_offload INTERFACE_LINK_OPTIONS)
        if(offload_compile)
            list(APPEND compile ${offload_compile})
        endif()
        if(offload_link)
            list(APPEND link ${offload_link})
        endif()
    endif()
    set(${out_compile} "${compile}" PARENT_SCOPE)
    set(${out_link} "${link}" PARENT_SCOPE)
endfunction()

# sushiruntime_apply_sycl(<target> [SOURCES <file>...] [DEVICE_SOURCES <file>...])
#   Applies the SYCL policy to <target>. DEVICE_SOURCES, each a source of <target>, compile with
#   the device options; the other units compile for the host. An executable or a shared library
#   links with the device options. SOURCES are the units that include SYCL; a toolchain without
#   per-source compilation registers them with add_sycl_to_target() and compiles every unit of
#   <target> with the device options.
function(sushiruntime_apply_sycl target)
    cmake_parse_arguments(ARG "" "" "SOURCES;DEVICE_SOURCES" ${ARGN})
    if(ARG_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR
            "sushiruntime_apply_sycl(${target}): unexpected '${ARG_UNPARSED_ARGUMENTS}'.")
    endif()

    get_target_property(target_sources ${target} SOURCES)
    foreach(source IN LISTS ARG_DEVICE_SOURCES)
        if(NOT source IN_LIST target_sources)
            message(FATAL_ERROR
                "sushiruntime_apply_sycl(${target}): DEVICE_SOURCES names '${source}', which is "
                "not a source of ${target}. List it under the target's sources as well.")
        endif()
    endforeach()

    sushiruntime_sycl_device_options(device_compile device_link)
    get_target_property(type ${target} TYPE)
    if(type STREQUAL "EXECUTABLE" OR type STREQUAL "SHARED_LIBRARY")
        target_link_options(${target} PRIVATE ${device_link})
    endif()

    if(NOT SR_SYCL_PER_SOURCE)
        if(ARG_SOURCES)
            add_sycl_to_target(TARGET ${target} SOURCES ${ARG_SOURCES})
        endif()
        target_compile_options(${target} PRIVATE ${device_compile})
        return()
    endif()

    if(ARG_DEVICE_SOURCES)
        set_source_files_properties(${ARG_DEVICE_SOURCES} TARGET_DIRECTORY ${target}
            PROPERTIES COMPILE_OPTIONS "${device_compile}")
    endif()
endfunction()
