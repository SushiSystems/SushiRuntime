# CMakeLists.txt
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026-present Mustafa Garip & Sushi Systems
# Licensed under the Apache License 2.0. See LICENSE.

sushiruntime_add_module_tests(
    MODULE partition_session
    SOURCES
        compile_partition_session_log_category.cpp
        compile_partitioner_factory.cpp
        test_graph_partition_problem.cpp
        test_journal_replay.cpp
        test_partition_apply.cpp
        test_partition_ledger.cpp
        test_partition_session.cpp
        test_placement_journal.cpp
        test_rebalance_decision.cpp
        test_transfer_path_tally.cpp)
