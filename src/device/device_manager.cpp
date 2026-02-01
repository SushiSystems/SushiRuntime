#include <iostream>

#include "../core/sushi_ptr.h"
#include "cpu_device.cpp"
#include "device.h"

#ifdef USE_CUDA

    #include "cuda_device.h"

    #include <cuda_runtime.h>

#endif

#define MAX_SUPPORTED_DEVICES 16

namespace SushiBLAS 
{

    class DeviceManager 
    {
        private:
            sushi_ptr<Device> devices[MAX_SUPPORTED_DEVICES];
            int device_count = 0;
            int active_device_idx = 0;

            DeviceManager() = default;

        public:
            static DeviceManager& instance() 
            {
                static DeviceManager inst;
                return inst;
            }

            void probe_devices() 
            {   
                // first register cpu device
                register_device(make_sushi<CPUDevice>());

                #ifdef USE_CUDA

                int cuda_count = 0;
                cudaError_t err = cudaGetDeviceCount(&cuda_count);
                
                if (err == cudaSuccess) 
                {
                    for (int i = 0; i < cuda_count; ++i) 
                    {
                        if (device_count >= MAX_SUPPORTED_DEVICES) break;
                        // pass device id to CUDADevice constructor
                        register_device(make_sushi<CUDADevice>(i));
                    }
                } 
                else 
                {
                    std::cerr << "CUDA devices not found or driver error." << std::endl;
                }

                #endif
            }

            bool register_device(sushi_ptr<Device> dev) 
            {
                if (device_count >= MAX_SUPPORTED_DEVICES) return false;

                devices[device_count++] = dev;
                return true;
            }

            sushi_ptr<Device> get_current_device() 
            {
                return devices[active_device_idx];
            }

            void set_device(int idx) 
            {
                if (idx >= 0 && idx < device_count) 
                {
                    active_device_idx = idx;
                }
            }

            int get_count() const { return device_count; }

            // debug
            void list_devices() 
            {
                std::cout << "--- Registered Devices ---" << std::endl;

                for(int i = 0; i < device_count; ++i) 
                {
                    std::cout << i << ": " << devices[i]->get_name() 
                            << " (Refs: " << devices[i]->get_ref_count() << ")" << std::endl;
                }
            }
    };
} // namespace SushiBLAS