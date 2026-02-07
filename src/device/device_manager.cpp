#include "device_manager.h"
#include "cpu_device.h"
#include <iostream>

#ifdef USE_CUDA
    #include <cuda_runtime.h>
    #include "cuda_device.h"
#endif

namespace SushiBLAS 
{
    DeviceManager& DeviceManager::instance() 
    {
        static DeviceManager inst;
        return inst;
    }

    void DeviceManager::probe_devices() 
    {   
        // Zaten taranmışsa tekrar ekleme yapma (Singleton güvenliği)
        if (device_count > 0) return;

        // 0. Cihaz her zaman CPU olsun (Logical Index 0 = Host)
        register_device(make_sushi<CPUDevice>());

        #ifdef USE_CUDA
            int cuda_count = 0;
            cudaError_t err = cudaGetDeviceCount(&cuda_count);
            
            if (err == cudaSuccess) 
            {
                for (int i = 0; i < cuda_count; ++i) 
                {   
                    if (device_count >= MAX_SUPPORTED_DEVICES) break; 
                    
                    // CUDA ID'leri 0, 1, 2... diye gider.
                    // Manager içinde bunlar 1, 2, 3... indexlerine yerleşir.
                    register_device(make_sushi<CUDADevice>(i));
                }
            } 
            else 
            {
                SB_LOG_WARN("Device Manager: CUDA devices not found or driver error: {}", cudaGetErrorString(err));
            }
        #endif
    }

    bool DeviceManager::register_device(sushi_ptr<Device> dev) 
    {
        if (device_count >= MAX_SUPPORTED_DEVICES) 
        {
            SB_LOG_ERROR("Device Manager: Maximum number of devices reached.");
            return false;
        }

        devices[device_count++] = dev;
        return true;
    }

    // AŞAMA 3.1 FIX: Thread-Safe Getter
    sushi_ptr<Device> DeviceManager::get_device(int index) 
    {
        if (index >= 0 && index < device_count) 
        {
            return devices[index];
        }
        
        SB_LOG_ERROR("Device Manager: Invalid device index requested: {}", index);
        return nullptr;
    }

    int DeviceManager::get_count() const 
    { 
        return device_count;
    }

    void DeviceManager::list_devices() 
    {
        std::cout << "--- Registered Devices ---" << std::endl;
        std::cout << "Index | Name            | Physical ID | Refs" << std::endl;
        std::cout << "------------------------------------------" << std::endl;
        
        for(int i = 0; i < device_count; ++i) 
        {
            // Index: Kullanıcının get_device(i) ile çağırdığı numara.
            // Physical ID: CUDA API veya OS tarafındaki ID (CPU için -1).
            std::cout << "  " << i << "   | " 
                      << devices[i]->get_name() << " \t| "
                      << devices[i]->get_id() << "          | " 
                      << devices[i]->get_ref_count() << std::endl;
        }
    }
} // namespace SushiBLAS