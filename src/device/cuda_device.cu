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
            
            cudaDeviceProp prop;
            cudaGetDeviceProperties(&prop, id);
            std::strncpy(device_name_, prop.name, sizeof(device_name_));
        #else
            std::strncpy(device_name_, "CUDA Disabled", sizeof(device_name_));
        #endif

        default_stream_ = create_stream();
    }

    CUDADevice::~CUDADevice() 
    {
        synchronize();
    }

    const char* CUDADevice::get_name() const { return device_name_; }
    int CUDADevice::get_id() const { return device_id; }

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
            // AŞAMA 0.3 FIX: Explicit Stream Requirement
            SB_THROW_IF(stream == nullptr, "CUDA Device: H2D Copy requires a valid stream (cannot be null)");

            cudaMemcpyAsync(dst, src, size, cudaMemcpyHostToDevice, 
                            static_cast<cudaStream_t>(stream->get_native_handle()));
        #endif
    }

    void CUDADevice::copy_d2h(void* dst, const void* src, size_t size, Stream* stream) 
    {
        #ifdef USE_CUDA
            // AŞAMA 0.3 FIX
            SB_THROW_IF(stream == nullptr, "CUDA Device: D2H Copy requires a valid stream (cannot be null)");

            cudaMemcpyAsync(dst, src, size, cudaMemcpyDeviceToHost, 
                            static_cast<cudaStream_t>(stream->get_native_handle()));
        #endif
    }

    void CUDADevice::copy_d2d(void* dst, Device* src_dev, const void* src, size_t size, Stream* stream) 
    {
        #ifdef USE_CUDA
            // AŞAMA 0.3 FIX
            SB_THROW_IF(stream == nullptr, "CUDA Device: D2D Copy requires a valid stream (cannot be null)");

            if (src_dev->get_id() == this->device_id) 
            {
                cudaMemcpyAsync(dst, src, size, cudaMemcpyDeviceToDevice, 
                                static_cast<cudaStream_t>(stream->get_native_handle()));
            } 
            else 
            {
                SB_LOG_ERROR("CUDA Device: Cross-device copy not implemented");
                // TODO: Peer-to-Peer (P2P) access check ve cudaMemcpyPeerAsync kullanımı eklenecek.
                // TODO: Eğer P2P yoksa Host üzerinden staging buffer ile taşıma yapılmalı.
            }
        #endif
    }

    void* CUDADevice::allocate_raw(size_t size) 
    {
        #ifdef USE_CUDA
            cudaSetDevice(device_id);
            void* ptr = nullptr;
            cudaError_t err = cudaMalloc(&ptr, size);
            
            // TODO: OOM (Out of Memory) durumunda retry veya cache boşaltma stratejisi eklenebilir.
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
} // namespace SushiBLAS