#include <gtest/gtest.h>
#include "../src/device/device_manager.h"

using namespace SushiBLAS;

TEST(DeviceManagerTest, SingletonAndRegistry) {
    auto& manager = DeviceManager::instance();
    manager.probe_devices();
    
    int count = manager.get_count();
    EXPECT_GE(count, 1); // En azından CPU olmalı

    auto cpu = manager.get_device(0);
    EXPECT_STREQ(cpu->get_name(), "Host CPU");
}