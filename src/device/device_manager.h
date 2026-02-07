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
        
        // AŞAMA 3.1 FIX: Global active device state kaldırıldı.
        // int active_device_idx = 0; // SİLİNDİ

        DeviceManager() = default;

    public:
        static DeviceManager& instance();
        
        void probe_devices();

        bool register_device(sushi_ptr<Device> dev);

        // AŞAMA 3.1 FIX: Explicit Access
        // "Current Device" kavramı kaldırıldı. Artık index ile spesifik cihaz istenmeli.
        // Bu, multithread ortamda "state kirlenmesini" önler.
        sushi_ptr<Device> get_device(int index);

        // void set_device(int idx); // SİLİNDİ (Global state manipülasyonu yasak)

        int get_count() const;
        void list_devices();
    };
}