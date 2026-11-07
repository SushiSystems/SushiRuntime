# CMakeLists.txt
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026-present Mustafa Garip & Sushi Systems
# Licensed under the Apache License 2.0. See LICENSE.

sushiruntime_add_module_tests(
    MODULE dispatch_executor
    SOURCES
        compile_dispatch_executor.cpp
        compile_dispatch_executor_log_category.cpp
        compile_dispatch_job.cpp
        compile_dispatch_recorder.cpp
        compile_dispatch_settler.cpp
        compile_held_tasks.cpp
        compile_worker_buffer_cache.cpp
        test_dispatch_executor.cpp
        test_dispatch_plan.cpp
        test_worker_buffer_cache.cpp)
