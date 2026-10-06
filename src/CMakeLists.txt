# CMakeLists.txt
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026-present Mustafa Garip & Sushi Systems
# Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.
# Commercial use requires a licence from Sushi Systems.

cmake_minimum_required(VERSION 3.20)

# --- Gather Sources ---
# Note: Recursive GLOB is convenient for development but explicit listing is preferred for production stability.
# For now, GLOB is used to capture new files automatically.
file(GLOB_RECURSE SRC_FILES
    CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
)

# The distributed layer is opt-in. Drop its sources from the default build so
# the standalone library carries no distributed code unless explicitly enabled.
file(GLOB_RECURSE DIST_SRC_FILES
    CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/distributed/*.cpp"
)
list(REMOVE_ITEM SRC_FILES ${DIST_SRC_FILES})
if(SR_ENABLE_DISTRIBUTED)
    list(APPEND SRC_FILES ${DIST_SRC_FILES})
endif()

# --- Define Library ---
add_library(sushiruntime SHARED ${SRC_FILES})

# A namespaced alias so an in-tree consumer (add_subdirectory) and an out-of-tree
# one (find_package) spell the dependency identically. Without it the two spell
# the same target two ways and a project that switches between them has to edit
# its link lines.
add_library(SushiRuntime::SushiRuntime ALIAS sushiruntime)

# --- Include Directories ---
# Two interfaces, because the headers live in two places over their life: in this
# tree while building it, under the install prefix afterwards. A bare absolute
# path would be baked into the exported target and point at this machine's source
# tree on the consumer's.
target_include_directories(sushiruntime PUBLIC
    "$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>"
    "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
)


# --- Compile Policy ---
# Warnings, architecture targeting and sanitizers, PRIVATE to this tree.
sushiruntime_apply_build_policy(sushiruntime)

# The floating-point contract is PUBLIC because it is part of what the library
# promises: the kernels are header templates a consumer instantiates in its own
# translation units, so byte-equal replay only holds if the consumer compiles
# them under the same FP semantics. See cmake/Determinism.cmake.
target_link_libraries(sushiruntime PUBLIC
    sushiruntime_deterministic_fp
)

# --- Link Libraries ---
target_link_libraries(sushiruntime PUBLIC
    IntelSYCL::SYCL_CXX
)

# hwloc is an implementation detail of a SHARED library: the consumer links
# sushiruntime, and sushiruntime has already resolved hwloc. CMake nevertheless
# records a PRIVATE dependency in the interface as $<LINK_ONLY:...> (it must, for
# the static-library case), which would make the exported target reference a
# PkgConfig:: target the consumer has no reason to be able to define. Wrapping it
# in BUILD_INTERFACE keeps it for this tree and drops it from the install
# interface, which is what the dependency actually is.
target_link_libraries(sushiruntime PRIVATE
    "$<BUILD_INTERFACE:PkgConfig::HWLOC>"
)

# --- Compile Definitions ---
target_compile_definitions(sushiruntime PRIVATE
    SUSHIRUNTIME_EXPORTS
    NOMINMAX
    WIN32_LEAN_AND_MEAN
)

# SR_DISTRIBUTED is PUBLIC, and this is not a leftover from before NodeExtensions
# fixed Node's layout — it is load-bearing for two independent reasons, both of
# which survive that fix.
#
# 1. It is how the distributed API reaches consumers at all. This library
#    compiles none of include/SushiRuntime/api/, so every `#ifdef SR_DISTRIBUTED`
#    there is consumer-compiled surface: Buffer/State registration
#    (`buffer.hpp`, `state.hpp`), the whole cluster API (`cluster.hpp`,
#    `runtime.hpp`), and DynamicGraph::rearm()'s reset of each node's offload
#    attempt counter (`dynamic_graph.hpp`). Making the define PRIVATE would
#    compile the cluster API out of every consumer and silently break offload
#    retry — a functional regression, not an ABI cleanup.
# 2. NodeExtensions still changes SIZE with the flag. Node's own layout no longer
#    does (the extensions live behind a unique_ptr), but that pointer's deleter is
#    instantiated in whichever translation unit destroys the Node — and consumers
#    do destroy Nodes, from header-inline API code. A consumer compiled without
#    the flag would size-delete a heap object the library allocated with a
#    different definition.
#
# Making it PRIVATE therefore needs more than flipping the keyword: NodeExtensions
# would have to become opaque (forward-declared here, defined and deleted in the
# library), and the distributed API surface would need a deliberate opt-in path
# for consumers. Until that is designed and an ABI check backs it, PUBLIC is the
# correct and honest visibility. The installed package reports the build's choice
# as SushiRuntime_DISTRIBUTED so a consumer can at least see what it linked.
if(SR_ENABLE_DISTRIBUTED)
    target_compile_definitions(sushiruntime PUBLIC SR_DISTRIBUTED)
    # The raw-socket TCP transport needs Winsock on Windows.
    if(WIN32)
        target_link_libraries(sushiruntime PRIVATE ws2_32)
    endif()
endif()


# --- Apply SYCL to specific source files ---
# Only files that use SYCL need special compilation flags
set(SR_SYCL_SOURCES
    memory/usm_allocator.cpp
    graph/task_graph.cpp
    execution/runtime_context.cpp
    interop/interop.cpp
)
if(SR_ENABLE_DISTRIBUTED)
    # These distributed TUs pull in <sycl/sycl.hpp> (kernels, worker execution,
    # Node/Task headers) and need SYCL compilation flags under the oneAPI path.
    list(APPEND SR_SYCL_SOURCES
        distributed/kernel_registry.cpp
        distributed/worker_agent.cpp
        distributed/distributed_coordinator.cpp
    )
endif()

add_sycl_to_target(
    TARGET sushiruntime
    SOURCES ${SR_SYCL_SOURCES}
)

# --- Properties ---
set_target_properties(sushiruntime PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    POSITION_INDEPENDENT_CODE ON
    WINDOWS_EXPORT_ALL_SYMBOLS ON
    OUTPUT_NAME "sushiruntime"
    # Without this, install(EXPORT ... NAMESPACE SushiRuntime::) combines the
    # namespace with the target's real (lowercase) name and produces
    # SushiRuntime::sushiruntime - a different spelling than the in-tree
    # ALIAS (SushiRuntime::SushiRuntime, above), which defeats D3's whole
    # point that add_subdirectory/find_package consumers spell the dependency
    # identically. Found via SB-6's tests/package/ verification in sushiblas
    # (this project's own tests/package/main.cpp already assumed the
    # capitalized name and would have failed the same way, since BB-4.6 has
    # never actually been run before now).
    EXPORT_NAME SushiRuntime
)

# --- Install & export ------------------------------------------------------
#
# What makes SushiRuntime consumable out of tree. The export set carries exactly
# two targets: the library, and the floating-point contract it links PUBLIC.
# Everything else the build applies — warnings, -Werror, sanitizers, -march — is
# PRIVATE and never reaches here, which is the boundary cmake/BuildPolicy.cmake
# exists to draw. `sushiruntime_deterministic_fp` must be in the set rather than
# merely referenced by it: CMake refuses to export a target whose interface names
# a non-imported target that no export set contains, and it is right to — the
# consumer could not otherwise define it.
install(TARGETS sushiruntime sushiruntime_deterministic_fp
    EXPORT SushiRuntimeTargets
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
    INCLUDES DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
)

install(DIRECTORY "${PROJECT_SOURCE_DIR}/include/"
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
)

install(EXPORT SushiRuntimeTargets
    FILE SushiRuntimeTargets.cmake
    NAMESPACE SushiRuntime::
    DESTINATION "${SR_INSTALL_CMAKEDIR}"
)

# The same targets, usable from a build tree without installing. A consumer can
# point CMAKE_PREFIX_PATH at the build directory during development.
export(EXPORT SushiRuntimeTargets
    FILE "${PROJECT_BINARY_DIR}/SushiRuntimeTargets.cmake"
    NAMESPACE SushiRuntime::
)
