# CMakeLists.txt
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026-present Mustafa Garip & Sushi Systems
# Licensed under the Apache License 2.0. See LICENSE.

sushiruntime_add_module(
    NAME partition_session TIER engine INTERNAL
    SYCL
    SOURCES
        graph_partition_problem.cpp
        journal_replay.cpp
        partition_apply.cpp
        partition_ledger.cpp
        partition_session.cpp
        placement_journal.cpp
        rebalance_decision.cpp
        transfer_path_tally.cpp
    DEPENDS
        core
        handle
        log
        memory
        report
        task
        transfer
        device_profile
        partitioning
    PRIVATE_DEPENDS
        coherence)
