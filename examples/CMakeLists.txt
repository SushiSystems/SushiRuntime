cmake_minimum_required(VERSION 3.20)

# ============================================================================
# Examples. Only the distributed demo for now, and only when the distributed
# layer is enabled (it links the master/worker entry points). Standalone builds
# add nothing here.
# ============================================================================

if(SUSHI_ENABLE_DISTRIBUTED)
    add_executable(sr_distributed_demo distributed_demo.cpp)
    target_link_libraries(sr_distributed_demo PRIVATE sushiruntime)
    set_target_properties(sr_distributed_demo PROPERTIES
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED ON)

    # Apply SYCL flags under the Intel oneAPI toolchain (no-op stub elsewhere).
    add_sycl_to_target(TARGET sr_distributed_demo SOURCES distributed_demo.cpp)

    if(WIN32)
        add_custom_command(TARGET sr_distributed_demo POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:sushiruntime>
                $<TARGET_FILE_DIR:sr_distributed_demo>)
    endif()
endif()
