#include <gtest/gtest.h>
#include <thread>
#include <vector>
#include <random>
#include <atomic>
#include <chrono>
#include <memory>
#include <iostream>

#include "../src/device/device_manager.h"
#include "../src/core/sushi_ptr.h"
#include "../src/common/logging/logger.h"

using namespace SushiBLAS;

class InfrastructureStressTest : public ::testing::Test {
protected:
    void SetUp() override {
        DeviceManager::instance().probe_devices();
    }
};

TEST_F(InfrastructureStressTest, BasicParallelAlloc) {
    int num_threads = 4;
    int iterations = 100;
    std::atomic<int> count{0};

    auto worker = [&](int tid) {
        auto dev = DeviceManager::instance().get_device(0);
        for(int i=0; i<iterations; ++i) {
            void* p = dev->allocate(64);
            if(p) {
                count++;
                dev->deallocate(p, nullptr, "basic");
            }
        }
    };

    std::vector<std::thread> threads;
    for(int i=0; i<num_threads; ++i) threads.emplace_back(worker, i);
    for(auto& t : threads) t.join();

    EXPECT_EQ(count.load(), num_threads * iterations);
    std::cout << "[INFO] BasicParallelAlloc passed." << std::endl;
}

TEST_F(InfrastructureStressTest, GlobalSystemStressLoad) {
    int num_threads = 12;
    int iterations = 20;
    
    auto stress_func = [&](int tid) {
        auto& manager = DeviceManager::instance();
        auto dev = manager.get_device(0);
        auto stream = dev->get_default_stream();

        for (int i = 0; i < iterations; ++i) {
            void* ptr = dev->allocate(1024);
            if (!ptr) continue;

            // Simulate some load
            if (i % 10 == 0) {
                auto event = dev->create_event("e");
                event->record(stream.get());
                dev->deallocate(ptr, event, "async");
            } else {
                dev->deallocate(ptr, nullptr, "sync");
            }
        }
    };

    std::vector<std::thread> workers;
    for (int i = 0; i < num_threads; ++i) workers.emplace_back(stress_func, i);
    for (auto& w : workers) w.join();

    std::cout << "[INFO] GlobalSystemStressLoad passed." << std::endl;
}
