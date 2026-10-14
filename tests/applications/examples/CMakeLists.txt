# CMakeLists.txt
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026-present Mustafa Garip & Sushi Systems
# Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.
# Commercial use requires a licence from Sushi Systems.

# The tests of the example headers build one program, sr_examples_tests, with the adapter's
# source, since the backend the adapter test calls is defined there.
set(example_root "${PROJECT_SOURCE_DIR}/applications/examples")
set(example_test_sources
    "${CMAKE_CURRENT_SOURCE_DIR}/adapter/test_diffusion_backend.cpp"
    "${example_root}/adapter/sushiruntime_adapter.cpp")
if(SR_ENABLE_DISTRIBUTED)
    list(APPEND example_test_sources "${CMAKE_CURRENT_SOURCE_DIR}/test_distributed_kernels.cpp")
endif()
sushiruntime_add_test_program(NAME sr_examples_tests MODULE examples SOURCES ${example_test_sources})
target_include_directories(sr_examples_tests PRIVATE "${example_root}")
