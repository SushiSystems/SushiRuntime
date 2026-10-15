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
/* Licensed under the PolyForm Noncommercial License 1.0.0 (the           */
/* "License"); you may not use this file except in compliance with the    */
/* License. You may obtain a copy of the License at                       */
/*                                                                        */
/*     https://polyformproject.org/licenses/noncommercial/1.0.0           */
/*                                                                        */
/* Noncommercial use is free. Commercial use requires a separate licence  */
/* from Sushi Systems; see COMMERCIAL.md. The software is provided        */
/* "as is", without warranty of any kind.                                 */
/**************************************************************************/

/**
 * @file test_priority_tracking_task.cpp
 * @brief Tests PriorityTrackingTask: it reports its priority and records it when it runs.
 * @see tests/engine/probe_tasks/README.md
 * @author Mustafa Garip
 */

#include <mutex>
#include <vector>

#include <gtest/gtest.h>
#include <sycl/sycl.hpp>

#include <SushiRuntime/task/task_types.hpp>

#include <SushiRuntime/probe_tasks/priority_tracking_task.hpp>

using namespace SushiRuntime;
using namespace SushiRuntime::Test;

/** @brief Tests that the task records its priority in run order. */
TEST(Medium_PriorityTrackingTask, RecordsItsPriorityWhenItRuns)
{
    std::vector<int> order;
    std::mutex order_mutex;
    PriorityTrackingTask low(1, order, order_mutex);
    PriorityTrackingTask high(9, order, order_mutex);
    EXPECT_EQ(high.priority(), 9);

    sycl::queue queue;
    (void)high.execute_host(queue, {});
    (void)low.execute_host(queue, {});
    EXPECT_EQ(order, (std::vector<int>{9, 1}));
    EXPECT_EQ(low.execution_mode(), Internal::Scheduler::Task::ExecutionMode::Host);
    EXPECT_FALSE(low.is_splittable());
}
