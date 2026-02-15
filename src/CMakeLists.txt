cmake_minimum_required(VERSION 3.20)

# --- Gather Sources ---
# Note: Recursive GLOB is convenient for development but explicit listing is preferred for production stability.
# For now, GLOB is used to capture new files automatically.
file(GLOB_RECURSE SRC_FILES 
    CONFIGURE_DEPENDS 
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
)

# --- Define Library ---
add_library(sushiruntime SHARED ${SRC_FILES})

# --- Include Directories ---
# --- Include Directories ---
target_include_directories(sushiruntime PUBLIC 
    "${CMAKE_SOURCE_DIR}/include"
)


# --- Link Libraries ---
target_link_libraries(sushiruntime PRIVATE
    PkgConfig::HWLOC
    IntelSYCL::SYCL_CXX
)

# --- Apply SYCL to specific source files ---
# Only files that use SYCL need special compilation flags
add_sycl_to_target(
    TARGET sushiruntime
    SOURCES
        memory/usm_allocator.cpp
        graph/task_graph.cpp
        execution/runtime_context.cpp
)

# --- Properties ---
set_target_properties(sushiruntime PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
    CXX_STANDARD 20
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
