#include <gtest/gtest.h>
#include "../../src/device/device_manager.h"
#include "../../src/stream/stream.h"
#include "../../src/event/event.h"

using namespace SushiBLAS;

class PipelineTest : public ::testing::TestWithParam<int> 
{
protected:
    void SetUp() override 
    {
        DeviceManager::instance().probe_devices();
    }
};

TEST_P(PipelineTest, BasicSync) 
{
    int dev_id = GetParam();
    auto dev = DeviceManager::instance().get_device(dev_id);
    auto stream = dev->create_stream();
    auto event = dev->create_event("test_event");

    // Wait on an event that is already done or newly created
    stream->wait(event.get()); 
    stream->synchronize();
    
    EXPECT_TRUE(stream->is_completed());
}

TEST_P(PipelineTest, StreamEventRecording) 
{
    int dev_id = GetParam();
    auto dev = DeviceManager::instance().get_device(dev_id);
    auto stream = dev->create_stream();
    auto event = dev->create_event("record_event");

    // A newly created event might be already completed (CUDA default) or not (CPU default).
    // We focus on the record -> synchronize -> completed flow.
    
    event->record(stream.get());
    stream->synchronize();
    
    // Tiny retry loop because CUDA event status might lag slightly in query
    int retries = 0;
    while (!event->is_completed() && retries++ < 100) {
        std::this_thread::sleep_for(std::chrono::microseconds(10));
    }

    EXPECT_TRUE(event->is_completed());
}

TEST_P(PipelineTest, CrossStreamDependency) 
{
    int dev_id = GetParam();
    auto dev = DeviceManager::instance().get_device(dev_id);
    auto s1 = dev->create_stream();
    auto s2 = dev->create_stream();
    auto e1 = dev->create_event("e1");

    // s1 does work (simulated) and records e1
    e1->record(s1.get());
    
    // s2 waits for s1's work via e1
    s2->wait(e1.get());
    
    s2->synchronize();
    EXPECT_TRUE(e1->is_completed());
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
    PipelineTest,
    ::testing::ValuesIn(GetActiveDeviceIndices())
);
