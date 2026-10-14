/**************************************************************************/
/* test_atomic_counter_task.cpp                                           */
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
 * @file test_atomic_counter_task.cpp
 * @brief Tests AtomicCounterTask: a host task that counts each run and never splits.
 * @see tests/engine/probe_tasks/README.md
 * @author Mustafa Garip
 */

#include <atomic>
#include <cstddef>
#include <vector>

#include <gtest/gtest.h>
#include <sycl/sycl.hpp>

#include <SushiRuntime/task/task_types.hpp>

#include <SushiRuntime/probe_tasks/atomic_counter_task.hpp>

using namespace SushiRuntime;
using namespace SushiRuntime::Test;

/** @brief Tests that each run adds one, and the task is a host task that does not split. */
TEST(Unit_AtomicCounterTask, EachRunAddsOne)
{
    std::atomic<std::size_t> counter{0};
    AtomicCounterTask task(counter);
    sycl::queue queue;
    (void)task.execute_host(queue, {});
    (void)task.execute_host(queue, {});
    EXPECT_EQ(counter.load(), 2u);
    EXPECT_EQ(task.execution_mode(), Internal::Scheduler::Task::ExecutionMode::Host);
    EXPECT_FALSE(task.is_splittable());
    EXPECT_FALSE(task.split().has_value());
}
