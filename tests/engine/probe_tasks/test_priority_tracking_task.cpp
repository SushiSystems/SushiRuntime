/**************************************************************************/
/* test_priority_tracking_task.cpp                                        */
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
 * @file test_priority_tracking_task.cpp
 * @brief Tests PriorityTrackingTask: it records its priority when it runs.
 * @see tests/engine/probe_tasks/README.md
 * @author Mustafa Garip
 */

#include <mutex>
#include <vector>

#include <gtest/gtest.h>

#include <SushiRuntime/probe_tasks/priority_tracking_task.hpp>

#include "support/runtime_environment.hpp"

using namespace SushiRuntime;
using namespace SushiRuntime::Test;

/** @brief Fixture of the tests that hand the program's queue to a host task. */
class Medium_PriorityTrackingTask : public TestSupport::RuntimeTest
{
};

/** @brief Tests that the task records its priority in run order. */
TEST_F(Medium_PriorityTrackingTask, RecordsItsPriorityWhenItRuns)
{
    std::vector<int> order;
    std::mutex order_mutex;
    PriorityTrackingTask low(1, order, order_mutex);
    PriorityTrackingTask high(9, order, order_mutex);

    (void)high.execute_host(queue(), {});
    (void)low.execute_host(queue(), {});

    EXPECT_EQ(order, (std::vector<int>{9, 1}));
}
