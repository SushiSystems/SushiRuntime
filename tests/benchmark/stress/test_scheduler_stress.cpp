/****************************************************************/
/* test_scheduler_stress.cpp                                    */
/* SushiRuntime - https://github.com/SushiSystems/SushiRuntime  */
/* Copyright (c) 2026 Sushi Systems                             */
/* Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.    */
/* Commercial use requires a licence from Sushi Systems.        */
/****************************************************************/

#include <atomic>
#include <vector>
#include <thread>
#include <chrono>
#include <gtest/gtest.h>
#include <SushiRuntime/execution/runtime_context.hpp>
#include <SushiRuntime/scheduler/task_scheduler.hpp>
#include <SushiRuntime/scheduler/task.hpp>

#include "../../common/test_helpers.hpp"

using namespace SushiRuntime;
using namespace SushiRuntime::Execution;
using namespace SushiRuntime::Scheduler;
using SushiRuntime::Test::AtomicCounterTask;
using SushiRuntime::Test::PriorityTrackingTask;

/**
 * @brief Tests scheduler stability and deque resizing under heavy load.
 * 
 * Submits one million tasks at once.
 */
TEST(Stress_TaskSchedulerStress, OneMillionTaskBomb)
{
    RuntimeContext ctx;
    TaskScheduler scheduler(ctx);

    std::atomic<size_t> executed_count{0};
    constexpr size_t TOTAL_TASKS = 1'000'000;

    scheduler.start();
    scheduler.arm_for_execution();

    for (size_t i = 0; i < TOTAL_TASKS; ++i)
        scheduler.submit(new AtomicCounterTask(executed_count));

    auto start_time = std::chrono::steady_clock::now();
    while (executed_count.load(std::memory_order_acquire) < TOTAL_TASKS)
    {
        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::seconds>(now - start_time).count() > 30)
            FAIL() << "Scheduler failed to process 1 million tasks within 30 seconds!";
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    scheduler.stop();
    EXPECT_EQ(executed_count.load(), TOTAL_TASKS);
}

/**
 * @brief Tests submission under contention.
 * 
 * Uses 8 producer threads submitting at once.
 */
TEST(Stress_TaskSchedulerStress, MultiProducerContention)
{
    RuntimeContext ctx;
    TaskScheduler scheduler(ctx);

    std::atomic<size_t> executed_count{0};
    constexpr int    NUM_PRODUCERS      = 8;
    constexpr size_t TASKS_PER_PRODUCER = 50'000;

    scheduler.start();
    scheduler.arm_for_execution();

    std::vector<std::thread> producers;
    for (int p = 0; p < NUM_PRODUCERS; ++p)
    {
        producers.emplace_back([&scheduler, &executed_count]()
        {
            for (size_t i = 0; i < TASKS_PER_PRODUCER; ++i)
                scheduler.submit(new AtomicCounterTask(executed_count));
        });
    }

    for (auto& t : producers)
        if (t.joinable())
            t.join();

    auto start_time = std::chrono::steady_clock::now();
    while (executed_count.load(std::memory_order_acquire) < (NUM_PRODUCERS * TASKS_PER_PRODUCER))
    {
        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::seconds>(now - start_time).count() > 30)
            FAIL() << "Multi-producer tasks were not processed within the time limit.";
        std::this_thread::yield();
    }

    scheduler.stop();
    EXPECT_EQ(executed_count.load(), NUM_PRODUCERS * TASKS_PER_PRODUCER);
}

/**
 * @brief Tests repeatedly arming and stopping the scheduler.
 * 
 * Helps to shake out races, deadlocks, and leaks.
 */
TEST(Stress_TaskSchedulerStress, RapidArmAndStopThrashing)
{
    RuntimeContext ctx;

    for (int i = 0; i < 500; ++i)
    {
        TaskScheduler scheduler(ctx);
        std::atomic<size_t> executed_count{0};

        scheduler.start();
        scheduler.arm_for_execution();

        for (int j = 0; j < 1000; ++j)
            scheduler.submit(new AtomicCounterTask(executed_count));

        // Stop abruptly without waiting — exercises the drain in stop().
        scheduler.stop();
    }

    SUCCEED();
}

/**
 * @brief Tests high-priority task bypassing.
 * 
 * A high-priority task must bypass the queue even behind many low-priority ones.
 */
TEST(Stress_TaskSchedulerStress, PriorityBypassUnderLoad)
{
    RuntimeContext ctx;
    TaskScheduler scheduler(ctx);

    std::vector<int> execution_order;
    std::mutex order_mutex;

    scheduler.start();
    // Do NOT arm yet, so tasks stay in the injector.

    for (int i = 0; i < 50; ++i)
        scheduler.submit(new PriorityTrackingTask(0, execution_order, order_mutex));

    scheduler.submit(new PriorityTrackingTask(100, execution_order, order_mutex));

    scheduler.arm_for_execution();

    auto start_time = std::chrono::steady_clock::now();
    while (true)
    {
        {
            std::lock_guard<std::mutex> lock(order_mutex);
            if (execution_order.size() >= 51)
                break;
        }
        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::seconds>(now - start_time).count() > 5)
            break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    scheduler.stop();

    ASSERT_EQ(execution_order.size(), 51);

    bool found_high_early = false;
    int check_window = static_cast<int>(scheduler.num_workers) * 2;
    if (check_window > 51)
        check_window = 51;

    for (int i = 0; i < check_window; ++i)
    {
        if (execution_order[i] == 100)
        {
            found_high_early = true;
            break;
        }
    }

    EXPECT_TRUE(found_high_early) << "High priority task was not executed in the first batch!";
}
