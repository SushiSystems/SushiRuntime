cmake_minimum_required(VERSION 3.20)

# Create the test executable
add_executable(sushiruntime_tests
    test_main.cpp
    core/test_logger.cpp
    graph/test_graph.cpp
    memory/test_memory.cpp
    topology/test_topology.cpp
    scheduler/test_scheduler.cpp
    performance/test_performance.cpp
    execution/test_sycl_execution.cpp
)

# Link against GTest and the main library
target_link_libraries(sushiruntime_tests PRIVATE
    GTest::gtest
    GTest::gtest_main
    sushiruntime
)

# Enable discover tests (requires enable_testing() in root)
if(WIN32)
    add_custom_command(TARGET sushiruntime_tests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:sushiruntime>
            $<TARGET_FILE_DIR:sushiruntime_tests>
    )
endif()

include(GoogleTest)
gtest_discover_tests(sushiruntime_tests DISCOVERY_MODE PRE_TEST)