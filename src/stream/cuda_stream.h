#pragma once

#include <mutex>
#include <vector>
#include <algorithm>
#include <cuda_runtime.h>

#include "../common/logging/logger.h"
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
            
            std::vector<sushi_ptr<Event>> pending_events_;
            std::mutex event_mutex_;

        public:
            CUDAStream(int32_t dev_id) : device_id_(dev_id) 
            {
                cudaStreamCreateWithFlags(&handle_, cudaStreamNonBlocking);
            }

            ~CUDAStream() 
            {
                {
                    std::lock_guard<std::mutex> lock(event_mutex_);
                    pending_events_.clear();
                }

                if (handle_) cudaStreamDestroy(handle_);
            }

            void synchronize() override 
            {
                cudaStreamSynchronize(handle_);
                
                std::lock_guard<std::mutex> lock(event_mutex_);
                pending_events_.clear();
            }

            bool is_completed() override 
            {
                bool done = (cudaStreamQuery(handle_) == cudaSuccess);
                if (done) 
                {
                    std::lock_guard<std::mutex> lock(event_mutex_);
                    pending_events_.clear();
                }
                else
                {
                    garbage_collect_events();
                }
                return done;
            }

            void* get_native_handle() const override 
            {
                return (void*)handle_;
            }

            void wait(Event* event) override 
            {
                SB_THROW_IF(event == nullptr, "Event cannot be null");

                // TODO: Implement strict device compatibility check. CUDA streams can only wait for CUDA events.
                // Use event->get_device_type() to verify compatibility once available in the base Event class.
                cudaStreamWaitEvent(handle_, 
                    static_cast<cudaEvent_t>(static_cast<CUDAEvent*>(event)->get_native_handle()), 0);

                {
                    std::lock_guard<std::mutex> lock(event_mutex_);

                    pending_events_.push_back(sushi_ptr<Event>(event));
                }

                garbage_collect_events();
            }

            int64_t get_id() const override { return reinterpret_cast<int64_t>(handle_); }
            const char* get_name() const override { return "CUDAStream"; }
            DeviceType get_device_type() const override { return DeviceType::CUDA; }

        private:
            void garbage_collect_events()
            {
                std::unique_lock<std::mutex> lock(event_mutex_, std::try_to_lock);
                if (lock.owns_lock())
                {
                    auto it = std::remove_if(pending_events_.begin(), pending_events_.end(), 
                        [](const sushi_ptr<Event>& e) {
                            return e->is_completed();
                        });
                    
                    pending_events_.erase(it, pending_events_.end());
                }
            }
    };
} // namespace SushiBLAS