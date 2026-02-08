#include <gtest/gtest.h>
#include "../src/device/device_manager.h"

int main(int argc, char **argv) 
{
    ::testing::InitGoogleTest(&argc, argv);
    int result = RUN_ALL_TESTS();
    
    // Cleanup ensures correct order of destruction for static singletons and thread_locals
    SushiBLAS::DeviceManager::instance().reset();
    
    return result;
}