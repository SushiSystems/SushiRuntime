# CMakeLists.txt
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026-present Mustafa Garip & Sushi Systems
# Licensed under the Apache License 2.0. See LICENSE.

sushiruntime_add_module_tests(
    MODULE transfer
    SOURCES
        compile_copy_submitter.cpp
        compile_peer_access.cpp
        compile_pinned_host_memory.cpp
        compile_space_queues.cpp
        compile_transfer_log_category.cpp
        compile_transfer_observer.cpp
        compile_transfer_path.cpp
        test_file_path.cpp
        test_host_staged_path.cpp
        test_peer_path.cpp
        test_queue_copy_submitter.cpp
        test_same_context_path.cpp
        test_staging_ring.cpp
        test_staging_rings.cpp
        test_transfer_cost.cpp
        test_transfer_observer_slot.cpp
        test_transfer_path_list.cpp)
