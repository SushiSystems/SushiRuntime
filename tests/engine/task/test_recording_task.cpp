/**************************************************************************/
/* test_recording_task.cpp                                                */
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
 * @file test_recording_task.cpp
 * @brief Tests RecordingTask, the in-memory host Task.
 * @author Mustafa Garip
 * @see tests/engine/task/README.md
 */

#include <gtest/gtest.h>

#include <SushiRuntime/task/recording_task.hpp>

using namespace SushiRuntime::Internal::Scheduler;

/** @brief Tests that the task reports the name and priority it was built with. */
TEST(Small_RecordingTask, ReportsItsNameAndPriority)
{
    const RecordingTask task("probe", 3);

    EXPECT_STREQ(task.name(), "probe");
    EXPECT_EQ(task.priority(), 3);
}

/** @brief Tests that the task is a host task that submits no device work and never splits. */
TEST(Small_RecordingTask, IsAHostTaskThatNeverSplits)
{
    RecordingTask task;

    EXPECT_EQ(task.execution_mode(), Task::ExecutionMode::Host);
    EXPECT_FALSE(task.submits_device_work());
    EXPECT_FALSE(task.is_splittable());
    EXPECT_FALSE(task.split().has_value());
}

/** @brief Tests that each native run is counted. */
TEST(Small_RecordingTask, CountsEachNativeRun)
{
    RecordingTask task;

    task.execute_native();
    task.execute_native();

    EXPECT_EQ(task.native_runs(), 2u);
}

/** @brief Tests that each completion is counted. */
TEST(Small_RecordingTask, CountsEachCompletion)
{
    RecordingTask task;
    ReadySink sink;

    task.on_complete(sink);

    EXPECT_EQ(task.completions(), 1u);
    EXPECT_TRUE(sink.empty());
}
