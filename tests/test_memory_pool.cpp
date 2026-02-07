#include <gtest/gtest.h>
#include <thread>
#include <vector>
#include "../src/device/device_manager.h"
#include "../src/core/sushi_ptr.h"

using namespace SushiBLAS;

class MemoryPoolTest : public ::testing::Test {
protected:
    void SetUp() override {
        DeviceManager::instance().probe_devices();
    }
};

TEST_F(MemoryPoolTest, MultithreadedIsolation) {
    auto cpu_dev = DeviceManager::instance().get_device(0);
    ASSERT_NE(cpu_dev, nullptr);

    auto stress_func = [&](int iterations) {
        for(int i = 0; i < iterations; ++i) {
            void* ptr = cpu_dev->allocate(256);
            EXPECT_NE(ptr, nullptr);
            cpu_dev->deallocate(ptr, nullptr, "test_tag");
        }
    };

    std::vector<std::thread> threads;
    for(int i = 0; i < 4; ++i) {
        threads.emplace_back(stress_func, 100);
    }

    for(auto& t : threads) t.join();
}