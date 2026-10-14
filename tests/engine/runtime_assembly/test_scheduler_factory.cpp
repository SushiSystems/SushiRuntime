/**************************************************************************/
/* test_scheduler_factory.cpp                                             */
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
 * @file test_scheduler_factory.cpp
 * @brief Tests make_scheduler(): a scheduler over a context that runs what it is given.
 * @see tests/engine/runtime_assembly/README.md
 * @author Mustafa Garip
 */

#include <atomic>
#include <chrono>
#include <cstddef>
#include <memory>
#include <thread>

#include <gtest/gtest.h>

#include <SushiRuntime/execution/runtime_context.hpp>
#include <SushiRuntime/probe_tasks/atomic_counter_task.hpp>
#include <SushiRuntime/scheduler/task_scheduler.hpp>

#include <SushiRuntime/runtime_assembly/context_factory.hpp>
#include <SushiRuntime/runtime_assembly/scheduler_factory.hpp>

using namespace SushiRuntime;
using namespace SushiRuntime::Test;

/** @brief Tests that the scheduler runs every task submitted, then stops. */
TEST(Unit_SchedulerFactory, TheSchedulerRunsWhatItIsGiven)
{
    constexpr std::size_t K_TASKS = 16;
    const std::shared_ptr<Internal::Execution::RuntimeContext> context = make_context();
    Internal::Scheduler::TaskScheduler scheduler = make_scheduler(*context);
    std::atomic<std::size_t> executed{0};

    scheduler.start();
    scheduler.arm_for_execution();
    for (std::size_t index = 0; index < K_TASKS; ++index)
        scheduler.submit(new AtomicCounterTask(executed));

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    while (executed.load() < K_TASKS && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    scheduler.stop();
    EXPECT_EQ(executed.load(), K_TASKS);
}
