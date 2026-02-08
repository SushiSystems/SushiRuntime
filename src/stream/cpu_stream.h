#pragma once

#include <iostream>

#include "../scheduler/task_scheduler.h"
#include "../common/logging/logger.h"
#include "../event/cpu_event.h"
#include "../device/device.h"
#include "stream.h"

namespace SushiBLAS 
{
    class CPUStream final : public Stream 
    {
        private:
            std::vector<sushi_ptr<Event>> dependencies_;
            mutable std::mutex dep_mutex_;

        public:
            CPUStream() = default;

            void synchronize() override 
            { 
                TaskScheduler::instance().synchronize();
            }

            bool is_completed() override 
            { 
                // TODO: Track tasks specific to this stream.
                return true; 
            }
            
            void* get_native_handle() const override { return nullptr; }

            void wait(Event* event) override 
            {
                SB_THROW_IF(event == nullptr, "Event cannot be null");
                
                std::lock_guard<std::mutex> lock(dep_mutex_);
                dependencies_.push_back(sushi_ptr<Event>(event));
            }

            void enqueue(std::function<void()> work, sushi_ptr<Event> signal = nullptr) 
            {
                Task t;
                t.work = std::move(work);
                t.signal_event = std::move(signal);
                
                {
                    std::lock_guard<std::mutex> lock(dep_mutex_);
                    t.dependencies = dependencies_;
                    dependencies_.clear();
                }

                TaskScheduler::instance().submit(std::move(t));
            }

            int64_t get_id() const override { return 0; }
            const char* get_name() const override { return "CPUStream"; }
            DeviceType get_device_type() const override { return DeviceType::CPU; }
    };
} // namespace SushiBLAS