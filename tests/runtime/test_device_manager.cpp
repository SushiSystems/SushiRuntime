#include <gtest/gtest.h>
#include "../../src/device/device_manager.h"
#include "../../src/device/hardware_info.h"

using namespace SushiBLAS;

TEST(DeviceManagerTest, ProbeDevices) 
{
    DeviceManager& manager = DeviceManager::instance();
    manager.probe_devices();
    
    int count = manager.get_count();
    EXPECT_GE(count, 1); // At least CPU should be found
    
    manager.list_devices();
}

TEST(DeviceManagerTest, GetDevices) 
{
    DeviceManager& manager = DeviceManager::instance();
    int count = manager.get_count();
    
    for (int i = 0; i < count; ++i) 
    {
        auto dev = manager.get_device(i);
        ASSERT_NE(dev, nullptr);
        
        const char* name = dev->get_name();
        EXPECT_NE(name, nullptr);
        
        HardwareInfo info = dev->get_hardware_info();
        EXPECT_GT(info.total_global_memory, 0ULL);
    }
}
