#include <iostream>
#include "src/device/device_manager.h"

int main() {
    std::cout << "--- SushiBLAS Device Probing ---" << std::endl;
    
    // Initialize Device Manager
    SushiBLAS::DeviceManager::instance().probe_devices();
    
    // List all detected hardware
    SushiBLAS::DeviceManager::instance().list_devices();

    return 0;
}
