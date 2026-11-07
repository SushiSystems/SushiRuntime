/**************************************************************************/
/* test_worker_statistics.cpp                                             */
/**************************************************************************/
/*                          This file is part of:                         */
/*                              SushiRuntime                              */
/*              https://github.com/SushiSystems/SushiRuntime              */
/*                         https://sushisystems.io                        */
/**************************************************************************/
/* Copyright (c) 2026-present Mustafa Garip & Sushi Systems               */
/*                                                                        */
/* Licensed under the Apache License, Version 2.0 (the "License"); you    */
/* may not use this file except in compliance with the License. You may   */
/* obtain a copy of the License at                                        */
/*                                                                        */
/*     https://www.apache.org/licenses/LICENSE-2.0                        */
/*                                                                        */
/* Unless required by applicable law or agreed to in writing, software    */
/* distributed under the License is distributed on an "AS IS" BASIS,      */
/* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or        */
/* implied. See the License for the specific language governing           */
/* permissions and limitations under the License.                         */
/**************************************************************************/

/**
 * @file test_worker_statistics.cpp
 * @brief Tests WorkerStatistics: a worker that did nothing reports nothing.
 * @see tests/extension/worker_types/README.md
 * @author Mustafa Garip
 */

#include <gtest/gtest.h>

#include <SushiRuntime/distributed/worker_statistics.hpp>

using namespace SushiRuntime::Distributed;

/** @brief Tests that every counter of a new WorkerStatistics is zero. */
TEST(Small_WorkerStatistics, EveryCounterStartsAtZero)
{
    const WorkerStatistics statistics;

    EXPECT_EQ(statistics.tasks_run, 0u);
    EXPECT_EQ(statistics.tasks_failed_kernel, 0u);
    EXPECT_EQ(statistics.tasks_failed_output_allocation, 0u);
    EXPECT_EQ(statistics.tasks_failed_input_unavailable, 0u);
    EXPECT_EQ(statistics.tasks_failed_unknown_operation, 0u);
    EXPECT_EQ(statistics.tasks_failed_shutting_down, 0u);
    EXPECT_EQ(statistics.cache_bytes, 0u);
    EXPECT_EQ(statistics.cache_evictions, 0u);
    EXPECT_EQ(statistics.registrations, 0u);
    EXPECT_EQ(statistics.reconnects, 0u);
}
