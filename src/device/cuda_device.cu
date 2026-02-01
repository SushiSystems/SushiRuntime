#include <cstring>
#include <cuda_runtime.h>

#include "../common/logging/logger.h"
#include "cuda_stream.h"
#include "cuda_event.h"
#include "device.h"

namespace SushiBLAS 
{
    class CUDADevice final : public Device 
    {
        private:
            sushi_ptr<Stream> default_stream_;
            char device_name_[128];

        public:
            CUDADevice(int32_t id) : Device(id) 
            {
                #ifdef USE_CUDA
                    SB_CUDA_CHECK(cudaSetDevice(id));
                #endif
                
                cudaDeviceProp prop;
                cudaGetDeviceProperties(&prop, id);
                std::strncpy(device_name_, prop.name, sizeof(device_name_));

                default_stream_ = create_stream();
            }

            ~CUDADevice() override 
            {
                synchronize();
            }

            const char* get_name() const override { return device_name_; }
            int get_id() const override { return device_id; }

            sushi_ptr<Stream> create_stream() override 
            {
                cudaSetDevice(device_id);
                return make_sushi<CUDAStream>(device_id);
            }

            sushi_ptr<Stream> get_default_stream() override 
            {
                return default_stream_;
            }

            sushi_ptr<Event> create_event(const char* name) override 
            {
                cudaSetDevice(device_id);
                return make_sushi<CUDAEvent>(name);
            }

            void synchronize() override 
            {
                cudaSetDevice(device_id);
                cudaDeviceSynchronize();
            }

            // Data Transfer Logic
            void copy_h2d(void* dst, const void* src, size_t size, Stream* stream) override 
            {
                cudaMemcpyAsync(dst, src, size, cudaMemcpyHostToDevice, 
                                static_cast<cudaStream_t>(stream->get_native_handle()));
            }

            void copy_d2h(void* dst, const void* src, size_t size, Stream* stream) override 
            {
                cudaMemcpyAsync(dst, src, size, cudaMemcpyDeviceToHost, 
                                static_cast<cudaStream_t>(stream->get_native_handle()));
            }

            void copy_d2d(void* dst, Device* src_dev, const void* src, size_t size, Stream* stream) override 
            {
                // Eğer aynı GPU içindeyse basit kopyalama
                if (src_dev->get_id() == this->device_id) 
                {
                    cudaMemcpyAsync(dst, src, size, cudaMemcpyDeviceToDevice, 
                                    static_cast<cudaStream_t>(stream->get_native_handle()));
                } 
                else 
                {
                    // TODO: Peer-to-peer copy between different GPUs: cudaMemcpyPeerAsync can be added later.
                    // For now, assume we copy via host (slow path).
                }
            }

        protected:
            void* allocate_raw(size_t size) override 
            {
                cudaSetDevice(device_id);

                void* ptr = nullptr;
                cudaError_t err = cudaMalloc(&ptr, size);
                return (err == cudaSuccess) ? ptr : nullptr;
            }

            void free_raw(void* ptr) override 
            {
                cudaSetDevice(device_id);
                cudaFree(ptr);
            }
    };
} // namespace SushiBLAS