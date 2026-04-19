/**************************************************************************/
/* test_scheduler.cpp                                                     */
/**************************************************************************/
/*                          This file is part of:                         */
/*                              SushiRuntime                              */
/*              https://github.com/SushiSystems/SushiRuntime              */
/*                        https://sushisystems.io                         */
/**************************************************************************/
/* Copyright (c) 2026-present Mustafa Garip & Sushi Systems               */
/* All Rights Reserved.                                                   */
/*                                                                        */
/* CONFIDENTIAL: This software is the proprietary information of          */
/* Mustafa Garip & Sushi Systems. Unauthorized copying of this file,      */
/* via any medium is strictly prohibited.                                 */
/*                                                                        */
/* This source code and the intellectual property contained herein        */
/* is confidential and may not be disclosed, copied, or used without      */
/* explicit written permission from the copyright holders.                */
/**************************************************************************/

#include <atomic>
#include <vector>
#include <thread>
#include <chrono>
#include <gtest/gtest.h>
#include <SushiRuntime/execution/runtime_context.hpp>
#include <SushiRuntime/scheduler/task_scheduler.hpp>
#include <SushiRuntime/scheduler/task.hpp>

using namespace SushiRuntime;
using namespace SushiRuntime::Execution;
using namespace SushiRuntime::Scheduler;

/**
 * @brief Lightweight host task for scheduler stress tests. Does not involve the GPU. 
 * Simply increments an atomic counter.
 */
class AtomicCounterTask : public Task 
{
    private:
        std::atomic<size_t>& counter_;
        
    public:
        explicit AtomicCounterTask(std::atomic<size_t>& c) : Task("AtomicCounterTask"), counter_(c) {}

        ExecutionMode get_execution_mode() const override   { return ExecutionMode::HOST; }

        sycl::event execute_host(sycl::queue&, const std::vector<sycl::event>&) override 
        {
            counter_.fetch_add(1, std::memory_order_relaxed);
            return {}; 
        }

        std::optional<sushi_ptr<Task>> split() override     { return std::nullopt; }
        
        bool is_splittable() const override                 { return false; }
};

/**
 * @brief Task that records when it was executed relative to a global sequence.
 */
class PriorityTrackingTask : public Task 
{
    private:
        int priority_;
        std::vector<int>& execution_order_;
        std::mutex& order_mutex_;
        
    public:
        PriorityTrackingTask(int p, std::vector<int>& order, std::mutex& mtx) 
            : Task("PriorityTask"), priority_(p), execution_order_(order), order_mutex_(mtx) {}

        int get_priority() const noexcept override { return priority_; }
        ExecutionMode get_execution_mode() const override { return ExecutionMode::HOST; }

        sycl::event execute_host(sycl::queue&, const std::vector<sycl::event>&) override 
        {
            // Simulate some work
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            
            std::lock_guard<std::mutex> lock(order_mutex_);
            execution_order_.push_back(priority_);
            return {}; 
        }

        std::optional<sushi_ptr<Task>> split() override     { return std::nullopt; }
        bool is_splittable() const override                 { return false; }
};

/**
 * @test OneMillionTaskBomb
 * @brief Tests stability and deque resizing by submitting 1 million tasks 
 * simultaneously to check worker thread performance under heavy load.
 */
TEST(TaskSchedulerStress, OneMillionTaskBomb) 
{
    RuntimeContext ctx; 
    TaskScheduler scheduler(ctx);
    
    std::atomic<size_t> executed_count{0};
    constexpr size_t TOTAL_TASKS = 1'000'000;

    scheduler.start();
    scheduler.arm_for_execution(); 

    // Add 1 million tasks to the injector queue
    for (size_t i = 0; i < TOTAL_TASKS; ++i)
        scheduler.submit(new AtomicCounterTask(executed_count));

    // Wait for the scheduler to drain all tasks with a timeout (30 second limit)
    auto start_time = std::chrono::steady_clock::now();
    while (executed_count.load(std::memory_order_acquire) < TOTAL_TASKS) 
    {
        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::seconds>(now - start_time).count() > 30) 
        {
            FAIL() << "Scheduler failed to process 1 million tasks within 30 seconds!";
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    scheduler.stop();

    // Exactly 1 million tasks should have been executed.
    EXPECT_EQ(executed_count.load(), TOTAL_TASKS);
}

/**
 * @test MultiProducerContention
 * @brief Tests submission logic under heavy contention by submitting tasks 
 * from 8 different threads simultaneously.
 */
TEST(TaskSchedulerStress, MultiProducerContention) 
{
    RuntimeContext ctx;
    TaskScheduler scheduler(ctx);
    
    std::atomic<size_t> executed_count{0};
    constexpr int NUM_PRODUCERS = 8;
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

    // Wait for producer threads to finish
    for (auto& t : producers)
        if (t.joinable()) t.join();

    // Wait for all tasks to be processed
    auto start_time = std::chrono::steady_clock::now();
    while (executed_count.load(std::memory_order_acquire) < (NUM_PRODUCERS * TASKS_PER_PRODUCER)) 
    {
        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::seconds>(now - start_time).count() > 30) 
        {
            FAIL() << "Multi-producer tasks were not processed within the time limit.";
        }
        std::this_thread::yield();
    }

    scheduler.stop();
    EXPECT_EQ(executed_count.load(), NUM_PRODUCERS * TASKS_PER_PRODUCER);
}

/**
 * @test RapidArmAndStopThrashing
 * @brief Tests scheduler lifecycle by repeatedly arming and stopping, 
 * checking for races, deadlocks, or memory leaks.
 */
TEST(TaskSchedulerStress, RapidArmAndStopThrashing) 
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

        // Call stop abruptly without waiting for completion. 
        // This tests the drain mechanism in stop().
        scheduler.stop(); 
    }
    
    // Success if no crash or deadlock occurs
    SUCCEED(); 
}

/**
 * @test PriorityBypassTest
 * @brief Verifies that high-priority tasks bypass the normal queue.
 */
TEST(TaskSchedulerStress, PriorityBypassTest)
{
    RuntimeContext ctx;
    TaskScheduler scheduler(ctx);
    
    std::vector<int> execution_order;
    std::mutex order_mutex;
    
    scheduler.start();
    // Do NOT arm yet to ensure tasks stay in the injector
    
    // 1. Submit 50 low priority tasks
    for (int i = 0; i < 50; ++i)
        scheduler.submit(new PriorityTrackingTask(0, execution_order, order_mutex));
    
    // 2. Submit 1 high priority task
    scheduler.submit(new PriorityTrackingTask(100, execution_order, order_mutex));
    
    // Now arm and let workers grab them
    scheduler.arm_for_execution();
    
    // Wait for all 51 tasks
    auto start_time = std::chrono::steady_clock::now();
    while (true) 
    {
        std::lock_guard<std::mutex> lock(order_mutex);
        if (execution_order.size() >= 51) break;
        
        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::seconds>(now - start_time).count() > 5) break;
        
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    scheduler.stop();
    
    ASSERT_EQ(execution_order.size(), 51);
    
    // The high priority task (100) should be among the first ones executed, 
    // even though it was submitted last.
    // Given we have multiple workers, it should definitely be in the first batch.
    bool found_high_early = false;
    int check_window = static_cast<int>(scheduler.num_workers) * 2; 
    if (check_window > 51) check_window = 51;
    
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