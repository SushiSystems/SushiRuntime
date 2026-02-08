#include <cstring>
#include <cuda_runtime.h>

#include "../common/logging/logger.h"
#include "../stream/cuda_stream.h"
#include "../event/cuda_event.h"
#include "cuda_device.h"

namespace SushiBLAS 
{
    CUDADevice::CUDADevice(int32_t id) : Device(id) 
    {
        #ifdef USE_CUDA

            SB_CUDA_CHECK(cudaSetDevice(id));
            cached_info_ = get_hardware_info();

        #else
            cached_info_.device_name = "CUDA Disabled";
        #endif

        default_stream_ = create_stream();
    }

    CUDADevice::~CUDADevice() 
    {
        synchronize();
    }

    int CUDADevice::get_id() const 
    { 
        return device_id; 
    }

    const char* CUDADevice::get_name() const 
    { 
        return cached_info_.device_name.c_str(); 
    }


    sushi_ptr<Stream> CUDADevice::create_stream() 
    {
        #ifdef USE_CUDA

            cudaSetDevice(device_id);
            return make_sushi<CUDAStream>(device_id);

        #else
            return nullptr;
        #endif
    }

    sushi_ptr<Stream> CUDADevice::get_default_stream() 
    {
        return default_stream_;
    }

    sushi_ptr<Event> CUDADevice::create_event(const char* name) 
    {
        #ifdef USE_CUDA

            cudaSetDevice(device_id);
            // We use default flags (0) instead of DisableTiming to ensure standard behavior if needed, 
            // but DisableTiming is fine. The key is SetDevice.
            return make_sushi<CUDAEvent>(name);

        #else
            return nullptr;
        #endif
    }

    void CUDADevice::synchronize() 
    {
        #ifdef USE_CUDA

            cudaSetDevice(device_id);
            cudaDeviceSynchronize();

        #endif
    }

    void CUDADevice::copy_h2d(void* dst, const void* src, size_t size, Stream* stream) 
    {
        #ifdef USE_CUDA

            SB_THROW_IF(stream == nullptr, "H2D Copy requires a valid stream (cannot be null)");

            cudaMemcpyAsync(dst, src, size, cudaMemcpyHostToDevice, 
                            static_cast<cudaStream_t>(stream->get_native_handle()));
                            
        #endif
    }

    void CUDADevice::copy_d2h(void* dst, const void* src, size_t size, Stream* stream) 
    {
        #ifdef USE_CUDA
            // AŞAMA 0.3 FIX
            SB_THROW_IF(stream == nullptr, "D2H Copy requires a valid stream (cannot be null)");

            cudaMemcpyAsync(dst, src, size, cudaMemcpyDeviceToHost, 
                            static_cast<cudaStream_t>(stream->get_native_handle()));
        #endif
    }

    void CUDADevice::copy_d2d(void* dst, Device* src_dev, const void* src, size_t size, Stream* stream) 
    {
        #ifdef USE_CUDA
            // AŞAMA 0.3 FIX
            SB_THROW_IF(stream == nullptr, "D2D Copy requires a valid stream (cannot be null)");

            if (src_dev->get_id() == this->device_id) 
            {
                cudaMemcpyAsync(dst, src, size, cudaMemcpyDeviceToDevice, 
                                static_cast<cudaStream_t>(stream->get_native_handle()));
            } 
            else 
            {
                SB_LOG_ERROR("Cross-device copy not implemented");
                // TODO: Add Peer-to-Peer (P2P) access check and cudaMemcpyPeerAsync usage.
                // TODO: If P2P is not available, transfer should be performed via a staging buffer on the Host.
            }
        #endif
    }

    void* CUDADevice::allocate_raw(size_t size) 
    {
        #ifdef USE_CUDA

            cudaSetDevice(device_id);
            void* ptr = nullptr;
            cudaError_t err = cudaMalloc(&ptr, size);
            
            // TODO: Add retry or cache eviction strategy in case of OOM (Out of Memory).
            return (err == cudaSuccess) ? ptr : nullptr;

        #else
            return nullptr;
        #endif
    }

    void CUDADevice::free_raw(void* ptr) 
    {
        #ifdef USE_CUDA

            cudaSetDevice(device_id);
            cudaFree(ptr);

        #endif
    }

    static int get_cores_per_sm(int major, int minor) 
    {
        switch (major) 
        {
            case 2: return (minor == 1) ? 48 : 32;                  // Fermi
            case 3: return 192;                                     // Kepler
            case 5: return 128;                                     // Maxwell
            case 6: return (minor == 1 || minor == 2) ? 128 : 64;   // Pascal
            case 7: return (minor == 5) ? 64 : 64;                  // Volta / Turing
            case 8: return (minor == 0) ? 64 : 128;                 // Ampere
            case 9: return 128;                                     // Hopper
            default: return 64;
        }
    }

    HardwareInfo CUDADevice::get_hardware_info() const
    {
        if (!cached_info_.device_name.empty() && cached_info_.device_name != "CUDA Disabled") 
        {
            return cached_info_;
        }

        HardwareInfo info;

        #ifdef USE_CUDA

            cudaDeviceProp prop;
            cudaGetDeviceProperties(&prop, device_id);
            
            info.device_name = prop.name;
            info.major_version = prop.major;
            info.minor_version = prop.minor;
            info.is_cuda = true;
            
            // CUDA Cores = SMs * CoresPerSM
            info.core_count = prop.multiProcessorCount * get_cores_per_sm(prop.major, prop.minor);
            
            info.max_threads_per_block = prop.maxThreadsPerBlock;
            info.total_global_memory = prop.totalGlobalMem;
            info.shared_memory_per_block = prop.sharedMemPerBlock;
            info.clock_rate_khz = prop.clockRate;
            info.l2_cache_size = prop.l2CacheSize;

        #else
            info.device_name = "CUDA Disabled";
        #endif

        return info;
    }

} // namespace SushiBLAS