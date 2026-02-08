#include <gtest/gtest.h>
#include "../../src/device/device_manager.h"
#include "../../src/memory/memory_pool.h"
#include <vector>

using namespace SushiBLAS;

class MemoryTest : public ::testing::TestWithParam<int> 
{
protected:
    void SetUp() override 
    {
        DeviceManager::instance().probe_devices();
    }
};

TEST_P(MemoryTest, BasicAllocation) 
{
    int dev_id = GetParam();
    auto dev = DeviceManager::instance().get_device(dev_id);
    ASSERT_NE(dev, nullptr);

    size_t size = 1024 * 1024; // 1MB
    void* ptr = dev->allocate(size);
    ASSERT_NE(ptr, nullptr);

    // Write-test (if CPU)
    if (dev->get_hardware_info().total_global_memory > 0) // Basic check
    {
        // For CPU we can write directly. For CUDA we'd need a kernel or copy.
        // But the task is about runtime, so we just check if it doesn't crash.
    }

    dev->deallocate(ptr, nullptr);
}

TEST_P(MemoryTest, PoolingStress) 
{
    int dev_id = GetParam();
    auto dev = DeviceManager::instance().get_device(dev_id);
    
    std::vector<void*> ptrs;
    const int iterations = 100;
    const size_t size = 4096;

    // Multiple allocations of same size should hit the pool
    for (int i = 0; i < iterations; ++i) 
    {
        ptrs.push_back(dev->allocate(size));
    }

    for (void* ptr : ptrs) 
    {
        dev->deallocate(ptr, nullptr);
    }
    
    // Allocating again should reuse pooled blocks
    void* ptr_reused = dev->allocate(size);
    ASSERT_NE(ptr_reused, nullptr);
    dev->deallocate(ptr_reused, nullptr);
}

TEST_P(MemoryTest, DataCopies) 
{
    int dev_id = GetParam();
    auto dev = DeviceManager::instance().get_device(dev_id);
    
    size_t size = 1024 * sizeof(float);
    std::vector<float> host_data(1024, 1.0f);
    std::vector<float> host_result(1024, 0.0f);

    void* device_ptr = dev->allocate(size);
    auto stream = dev->create_stream();

    dev->copy_h2d(device_ptr, host_data.data(), size, stream.get());
    stream->synchronize();

    dev->copy_d2h(host_result.data(), device_ptr, size, stream.get());
    stream->synchronize();

    for (size_t i = 0; i < 1024; ++i) 
    {
        EXPECT_EQ(host_data[i], host_result[i]);
    }

    dev->deallocate(device_ptr, nullptr);
}

static std::vector<int> GetActiveDeviceIndices() 
{
    DeviceManager::instance().probe_devices();
    std::vector<int> indices;
    for (int i = 0; i < DeviceManager::instance().get_count(); ++i) 
    {
        indices.push_back(i);
    }
    return indices;
}

INSTANTIATE_TEST_SUITE_P(
    AllDevices,
    MemoryTest,
    ::testing::ValuesIn(GetActiveDeviceIndices())
);


