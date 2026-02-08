#pragma once

#include "../core/sushi_ptr.h"
#include "../common/common.h"
#include "device.h"

namespace SushiBLAS 
{
    class DeviceManager 
    {
    private:
        sushi_ptr<Device> devices[MAX_SUPPORTED_DEVICES];
        int device_count = 0;
        
        DeviceManager() = default;

    public:
        static DeviceManager& instance();
        
        void probe_devices();

        bool register_device(sushi_ptr<Device> dev);

        sushi_ptr<Device> get_device(int index);

        int get_count() const;
        void list_devices();
        
        void reset();
    };
}