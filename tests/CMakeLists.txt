cmake_minimum_required(VERSION 3.20)

# Create the test executable
add_executable(sushiruntime_tests
    test_main.cpp

    core/test_logger.cpp
)

# Link against GTest and the main library
target_link_libraries(sushiruntime_tests PRIVATE
    GTest::gtest
    GTest::gtest_main
    sushiruntime
)

# Enable discover tests (requires enable_testing() in root)
include(GoogleTest)
gtest_discover_tests(sushiruntime_tests)
