/**************************************************************************/
/* test_in_memory_telemetry.cpp                                           */
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
 * @file test_in_memory_telemetry.cpp
 * @brief Tests InMemoryTelemetry, the in-memory TelemetryProvider.
 * @author Mustafa Garip
 * @see tests/engine/scheduler/README.md
 */

#include <gtest/gtest.h>

#include <SushiRuntime/scheduler/in_memory_telemetry.hpp>

using namespace SushiRuntime::Internal::Scheduler;

/**
 * @brief In-memory telemetry counts one worker per held queue depth.
 */
TEST(Small_InMemoryTelemetry, CountsOneWorkerPerQueueDepth)
{
    const InMemoryTelemetry telemetry({4, 0, 7}, {1, 2, 3});

    EXPECT_EQ(telemetry.worker_count(), 3u);
}


/**
 * @brief In-memory telemetry answers each worker with the depth and total held for it.
 */
TEST(Small_InMemoryTelemetry, AnswersEachWorkerWithItsHeldValues)
{
    const InMemoryTelemetry telemetry({4, 0, 7}, {1, 2, 3});

    EXPECT_EQ(telemetry.queue_size(2), 7u);
    EXPECT_EQ(telemetry.completed_task_count(1), 2u);
}

