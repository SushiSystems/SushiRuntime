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

# --- Include Directories ---
# --- Include Directories ---
target_include_directories(sushiruntime PUBLIC 
    "${CMAKE_SOURCE_DIR}/include"
)


# --- Link Libraries ---
target_link_libraries(sushiruntime PUBLIC
    IntelSYCL::SYCL_CXX
)

target_link_libraries(sushiruntime PRIVATE
    PkgConfig::HWLOC
)

# --- Compile Definitions ---
target_compile_definitions(sushiruntime PRIVATE
    SUSHIRUNTIME_EXPORTS
    NOMINMAX
    WIN32_LEAN_AND_MEAN
)

# SR_DISTRIBUTED must be PUBLIC: it changes the layout of Graph::Node (adds
# offload fields), so the library and every consumer (tests, apps) must agree.
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
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
    POSITION_INDEPENDENT_CODE ON
    WINDOWS_EXPORT_ALL_SYMBOLS ON
    OUTPUT_NAME "sushiruntime"
)

# --- Install Target ---
install(TARGETS sushiruntime 
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
    INCLUDES DESTINATION include
)

install(DIRECTORY "${CMAKE_SOURCE_DIR}/include/" 
    DESTINATION include
)
