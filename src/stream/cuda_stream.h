#pragma once

#include <cuda_runtime.h>

#include "../event/cuda_event.h"
#include "../device/device.h"
#include "stream.h"

namespace SushiBLAS 
{
    class CUDAStream final : public Stream 
    {
        private:
            cudaStream_t handle_;
            int32_t device_id_;

        public:
            CUDAStream(int32_t dev_id) : device_id_(dev_id) 
            {
                // Non-blocking stream kullanarak default stream (0) ile senkronizasyonu önlüyoruz.
                cudaStreamCreateWithFlags(&handle_, cudaStreamNonBlocking);
            }

            ~CUDAStream() 
            {
                if (handle_) cudaStreamDestroy(handle_);
            }

            void synchronize() override 
            {
                cudaStreamSynchronize(handle_);
            }

            bool is_completed() override 
            {
                return cudaStreamQuery(handle_) == cudaSuccess;
            }

            void* get_native_handle() const override 
            {
                return (void*)handle_;
            }

            void wait(Event* event) override 
            {
                // GPU tarafında stream'i bekletir, CPU'yu bloklamaz.
                cudaStreamWaitEvent(handle_, 
                    static_cast<cudaEvent_t>(static_cast<CUDAEvent*>(event)->get_native_handle()), 0);
            }

            const char* get_name() const override { return "CUDAStream"; }
            int64_t get_id() const override { return reinterpret_cast<int64_t>(handle_); }
            DeviceType get_device_type() const override { return DeviceType::CUDA; }
    };
} // namespace SushiBLAS