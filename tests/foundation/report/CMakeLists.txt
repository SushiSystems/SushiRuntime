# CMakeLists.txt
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026-present Mustafa Garip & Sushi Systems
# Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.
# Commercial use requires a licence from Sushi Systems.

sushiruntime_add_module_tests(
    MODULE report
    SOURCES
        test_clock_pairing.cpp
        test_dispatch_width.cpp
        test_drop_counts.cpp
        test_node_event_record.cpp
        test_node_table.cpp
        test_run_observer.cpp
        test_run_report.cpp)
