#include <gtest/gtest.h>
#include "../../src/scheduler/task_scheduler.h"
#include "../../src/device/device_manager.h"
#include <atomic>
#include <chrono>
#include <thread>

using namespace SushiBLAS;

TEST(SchedulerTest, BasicTask) 
{
    std::atomic<int> counter{0};
    TaskScheduler& scheduler = TaskScheduler::instance();

    scheduler.submit(Task([&counter]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        counter++;
    }));

    scheduler.synchronize();
    EXPECT_EQ(counter.load(), 1);
}

TEST(SchedulerTest, TaskDependencies) 
{
    auto dev = DeviceManager::instance().get_device(0);
    auto event = dev->create_event("dep_event");
    
    std::atomic<bool> first_done{false};
    std::atomic<bool> second_done{false};

    TaskScheduler& scheduler = TaskScheduler::instance();

    // Task 1: Signals an event
    scheduler.submit(Task([&first_done, &event]() 
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        first_done = true;
        // In a real scenario, this task might run a kernel on a stream
        // and record the event there. Here we simulate completion.
        // But since we can't manually set event completion in tests easily without a stream,
        // we'll just check the logic of dependency if scheduler supports it.
    }, event));

    // Task 2: Depends on task 1's event
    Task t2([&second_done, &first_done]() 
    {
        EXPECT_TRUE(first_done.load());
        second_done = true;
    });
    t2.dependencies.push_back(event);

    scheduler.submit(std::move(t2));

    scheduler.synchronize();
    EXPECT_TRUE(second_done.load());
}
