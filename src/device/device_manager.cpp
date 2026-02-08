#include <iostream>
#include "device_manager.h"
#include "cpu_device.h"

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
        if (device_count > 0) return;

        // TODO: Implement NUMA awareness for multi-socket systems. 
        // Currently all logical cores are treated as a single device.
        // On multi-socket servers, this causes cross-socket latency.
        register_device(make_sushi<CPUDevice>());

        #ifdef USE_CUDA

            int cuda_count = 0;
            cudaError_t err = cudaGetDeviceCount(&cuda_count);
            
            if (err == cudaSuccess) 
            {
                for (int i = 0; i < cuda_count; ++i) 
                {   
                    if (device_count >= MAX_SUPPORTED_DEVICES) break; 
                    
                    register_device(make_sushi<CUDADevice>(i));
                }
            } 
            else 
            {
                SB_LOG_WARN("CUDA devices not found or driver error: {}", cudaGetErrorString(err));
            }

        #endif
    }

    bool DeviceManager::register_device(sushi_ptr<Device> dev) 
    {
        if (device_count >= MAX_SUPPORTED_DEVICES) 
        {
            SB_LOG_ERROR("Maximum number of devices reached.");
            return false;
        }

        devices[device_count++] = dev;
        return true;
    }

    sushi_ptr<Device> DeviceManager::get_device(int index) 
    {
        if (index >= 0 && index < device_count) 
        {
            return devices[index];
        }
        
        SB_LOG_ERROR("Invalid device index requested: {}", index);
        return nullptr;
    }

    int DeviceManager::get_count() const 
    { 
        return device_count;
    }

    void DeviceManager::list_devices() 
    {
        std::cout << "\n================================ Registered Devices ================================" << std::endl;
        
        for(int i = 0; i < device_count; ++i) 
        {
            auto info = devices[i]->get_hardware_info();
            
            std::cout << "[" << i << "] " << info.device_name;
            if (info.is_cuda) 
            {
                std::cout << " (CUDA Compute " << info.major_version << "." << info.minor_version << ")";
            }
            std::cout << "\n    |-- Physical ID : " << devices[i]->get_id() << std::endl;
            
            if (info.is_cuda) 
            {
                std::cout << "    |-- CUDA Cores  : " << info.core_count << " cores";
            } 
            else 
            {
                std::cout << "    |-- Logic Cores : " << info.core_count << " threads";
            }

            if (info.max_threads_per_block > 0 && info.is_cuda) 
            {
                 std::cout << " (" << info.max_threads_per_block << " max threads/block)";
            }
            std::cout << "\n    |-- Total Memory: " << (info.total_global_memory / (1024 * 1024)) << " MB" << std::endl;
            
            if (info.clock_rate_khz > 0) 
            {
                std::cout << "    |-- Clock Rate  : " << (info.clock_rate_khz / 1000) << " MHz" << std::endl;
            }
            std::cout << "    `-- Ref Count   : " << devices[i]->get_ref_count() << "\n" << std::endl;
        }
        std::cout << "====================================================================================" << std::endl;
    }
    void DeviceManager::reset() 
    {
        SB_LOG_INFO("DeviceManager: Resetting devices...");
        for (int i = 0; i < device_count; ++i) 
        {
            SB_LOG_INFO("DeviceManager: Destroying device {}", i);
            devices[i] = nullptr;
        }
        device_count = 0;
        SB_LOG_INFO("DeviceManager reset complete.");
    }

} // namespace SushiBLAS