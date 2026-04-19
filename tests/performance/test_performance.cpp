/**************************************************************************/
/* test_performance.cpp                                                   */
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

#include <vector>
#include <thread>
#include <atomic>
#include <chrono>
#include <iostream>
#include <iomanip>
#include <gtest/gtest.h>
#include <SushiRuntime/execution/runtime_context.hpp>
#include <SushiRuntime/memory/usm_allocator.hpp>
#include <SushiRuntime/scheduler/task_scheduler.hpp>
#include <SushiRuntime/scheduler/task.hpp>

using namespace SushiRuntime;
using namespace SushiRuntime::Execution;
using namespace SushiRuntime::Memory;
using namespace SushiRuntime::Scheduler;

class PerfCounterTask : public Task 
{
    private:
        std::atomic<size_t>& counter_;
        
    public:
        explicit PerfCounterTask(std::atomic<size_t>& c) : Task("PerfTask"), counter_(c) {}
        ExecutionMode get_execution_mode() const override   { return ExecutionMode::HOST; }
        std::optional<sushi_ptr<Task>> split() override     { return std::nullopt; }
        bool is_splittable() const override                 { return false; }
        sycl::event execute_host(sycl::queue&, const std::vector<sycl::event>&) override 
        {
            counter_.fetch_add(1, std::memory_order_relaxed);
            return {}; 
        }
};

void print_benchmark_result(const std::string& name, size_t operations, double duration_sec)
{
    double ops_per_sec = operations / duration_sec;
    double ns_per_op = (duration_sec * 1e9) / operations;
    
    std::cout << "[ PERF STAT ] " << std::left << std::setw(30) << name 
              << " | " << std::right << std::setw(12) << static_cast<size_t>(ops_per_sec) << " Ops/sec | "
              << std::fixed << std::setprecision(2) << ns_per_op << " ns/Op" << std::endl;
}

TEST(PerformanceBenchmark, SingleThreadAllocationSpeed)
{
    RuntimeContext ctx;
    auto allocator = ctx.get_allocator();
    
    constexpr size_t ITERATIONS = 1'000'000;
    constexpr size_t ALLOC_SIZE = 128;

    auto start_malloc = std::chrono::high_resolution_clock::now();
    // [DÜZELTME 1]: Her thread kendi lokal volatile sink'ini kullanıyor
    volatile void* local_sink_malloc = nullptr; 
    for (size_t i = 0; i < ITERATIONS; ++i)
    {
        void* ptr = std::malloc(ALLOC_SIZE);
        local_sink_malloc = ptr;
        std::free(ptr);
    }
    auto end_malloc = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> duration_malloc = end_malloc - start_malloc;

    auto start_usm = std::chrono::high_resolution_clock::now();
    volatile void* local_sink_usm = nullptr;
    for (size_t i = 0; i < ITERATIONS; ++i)
    {
        void* ptr = allocator->allocate(ALLOC_SIZE);
        local_sink_usm = ptr;
        allocator->deallocate(ptr);
    }
    auto end_usm = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> duration_usm = end_usm - start_usm;

    std::cout << "\n------------------------------------------------------------\n";
    print_benchmark_result("std::malloc (1 Thread)", ITERATIONS, duration_malloc.count());
    print_benchmark_result("USM Allocator (1 Thread)", ITERATIONS, duration_usm.count());
    std::cout << "------------------------------------------------------------\n";

    SUCCEED();
}

TEST(PerformanceBenchmark, MultiThreadContentionSpeed)
{
    RuntimeContext ctx;
    auto allocator = ctx.get_allocator();
    
    constexpr int NUM_THREADS = 8;
    constexpr size_t ITERATIONS_PER_THREAD = 250'000;
    constexpr size_t ALLOC_SIZE = 256;

    auto start_malloc = std::chrono::high_resolution_clock::now();
    std::vector<std::thread> malloc_threads;
    for (int t = 0; t < NUM_THREADS; ++t)
    {
        malloc_threads.emplace_back([&]()
        {
            // [DÜZELTME 1]: Data Race tamamen engellendi. Thread içi güvenli.
            volatile void* local_sink = nullptr;
            for (size_t i = 0; i < ITERATIONS_PER_THREAD; ++i)
            {
                void* ptr = std::malloc(ALLOC_SIZE);
                local_sink = ptr; 
                std::free(ptr);
            }
        });
    }
    for (auto& t : malloc_threads) t.join();
    auto end_malloc = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> duration_malloc = end_malloc - start_malloc;

    auto start_usm = std::chrono::high_resolution_clock::now();
    std::vector<std::thread> usm_threads;
    for (int t = 0; t < NUM_THREADS; ++t)
    {
        usm_threads.emplace_back([&]()
        {
            volatile void* local_sink = nullptr;
            for (size_t i = 0; i < ITERATIONS_PER_THREAD; ++i)
            {
                void* ptr = allocator->allocate(ALLOC_SIZE);
                local_sink = ptr; 
                allocator->deallocate(ptr);
            }
        });
    }
    for (auto& t : usm_threads) t.join();
    auto end_usm = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> duration_usm = end_usm - start_usm;

    size_t total_operations = NUM_THREADS * ITERATIONS_PER_THREAD;

    std::cout << "\n------------------------------------------------------------\n";
    print_benchmark_result("std::malloc (8 Threads)", total_operations, duration_malloc.count());
    print_benchmark_result("USM Allocator (8 Threads)", total_operations, duration_usm.count());
    std::cout << "------------------------------------------------------------\n";

    SUCCEED();
}

TEST(PerformanceBenchmark, TaskSchedulerThroughput)
{
    RuntimeContext ctx;
    TaskScheduler scheduler(ctx);
    
    std::atomic<size_t> executed_count{0};
    constexpr size_t TOTAL_TASKS = 2'000'000; 

    scheduler.start();
    scheduler.arm_for_execution();

    auto start_time = std::chrono::high_resolution_clock::now();

    for (size_t i = 0; i < TOTAL_TASKS; ++i)
    {
        scheduler.submit(new PerfCounterTask(executed_count));
    }

    while (executed_count.load(std::memory_order_acquire) < TOTAL_TASKS)
    {
        std::this_thread::yield();
    }

    auto end_time = std::chrono::high_resolution_clock::now();
    scheduler.stop();

    std::chrono::duration<double> duration = end_time - start_time;

    std::cout << "\n------------------------------------------------------------\n";
    print_benchmark_result("Task Scheduler (End-to-End)", TOTAL_TASKS, duration.count());
    std::cout << "------------------------------------------------------------\n";

    EXPECT_LT(duration.count(), 10.0) << "Scheduler is too slow.";
}