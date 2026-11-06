/**************************************************************************/
/* test_recording_rebalance_strategy.cpp                                  */
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
 * @file test_recording_rebalance_strategy.cpp
 * @brief Tests RecordingRebalanceStrategy, the in-memory RebalanceStrategy.
 * @author Mustafa Garip
 * @see tests/engine/scheduler/README.md
 */

#include <gtest/gtest.h>

#include <SushiRuntime/scheduler/in_memory_telemetry.hpp>
#include <SushiRuntime/scheduler/recording_rebalance_strategy.hpp>
#include <SushiRuntime/scheduler/recording_task_migrator.hpp>

using namespace SushiRuntime::Internal::Scheduler;

/**
 * @brief The recording strategy counts its evaluations and asks for no migration.
 */
TEST(Small_RecordingRebalanceStrategy, CountsEvaluationsAndMigratesNothing)
{
    const InMemoryTelemetry telemetry({9, 0}, {0, 0});
    RecordingTaskMigrator migrator;
    RecordingRebalanceStrategy strategy;

    strategy.evaluate_and_rebalance(telemetry, migrator);
    strategy.evaluate_and_rebalance(telemetry, migrator);

    EXPECT_EQ(strategy.evaluations(), 2u);
    EXPECT_TRUE(migrator.requests().empty());
}
