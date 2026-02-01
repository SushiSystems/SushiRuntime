#pragma once

#include <cuda_runtime.h>

#include "../stream/stream.h"
#include "event.h"

namespace SushiBLAS 
{
    class CUDAEvent final : public Event 
    {
        private:
            cudaEvent_t handle_;
            const char* name_;

        public:
            CUDAEvent(const char* name) : name_(name) 
            {
                cudaEventCreateWithFlags(&handle_, cudaEventDisableTiming);
            }

            ~CUDAEvent() 
            {
                if (handle_) cudaEventDestroy(handle_);
            }

            void record(Stream* stream) override 
            {
                cudaStream_t s = static_cast<cudaStream_t>(stream->get_native_handle());
                cudaEventRecord(handle_, s);
            }

            bool is_completed() const override 
            {
                return cudaEventQuery(handle_) == cudaSuccess;
            }

            void synchronize() override 
            {
                cudaEventSynchronize(handle_);
            }

            const char* get_name() const override { return name_; }

            float elapsed_time(Event* start) override 
            {
                float ms = 0.0f;
                cudaEventElapsedTime(&ms, static_cast<CUDAEvent*>(start)->handle_, handle_);
                return ms;
            }

            void* get_native_handle() const { return (void*)handle_; }
    };
} // namespace SushiBLAS