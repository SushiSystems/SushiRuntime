/**************************************************************************/
/* test_recording_task_migrator.cpp                                       */
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
 * @file test_recording_task_migrator.cpp
 * @brief Tests RecordingTaskMigrator, the in-memory TaskMigrator.
 * @author Mustafa Garip
 * @see tests/engine/scheduler/README.md
 */

#include <vector>

#include <gtest/gtest.h>

#include <SushiRuntime/scheduler/recording_task_migrator.hpp>

using namespace SushiRuntime::Internal::Scheduler;

/**
 * @brief The recording migrator keeps every request in arrival order.
 */
TEST(Small_RecordingTaskMigrator, KeepsEveryRequestInOrder)
{
    RecordingTaskMigrator migrator;

    migrator.migrate_tasks(1, 3);
    migrator.migrate_tasks(0, 5);

    const std::vector<MigrationRequest> expected{{1, 3}, {0, 5}};
    EXPECT_EQ(migrator.requests(), expected);
}

