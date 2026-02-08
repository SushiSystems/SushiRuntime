#pragma once

#include <string>
#include <atomic> 
#include <thread>

#include "event.h"
#include "../common/logging/logger.h"

namespace SushiBLAS 
{
    class CPUEvent final : public Event 
    {
        private:
            std::atomic<bool> completed{false};
            std::string name_;

        public:
            CPUEvent(const char* name) : name_(name ? name : "CPUEvent"), completed(false) {}
            
            void record(Stream* /*unused*/) override 
            {
                completed.store(true, std::memory_order_release);
            }

            bool is_completed() const override 
            {
                return completed.load(std::memory_order_acquire);
            }

            void synchronize() override 
            {
                // Busy-wait loop
                int spins = 0;
                while(!is_completed()) 
                { 
                    if(spins++ > 1000) std::this_thread::yield();
                }
            }

            const char* get_name() const override { return name_.c_str(); }
            
            float elapsed_time(Event* /*start*/) override 
            {
                // TODO: Add high precision measurement with std::chrono
                return 0.0f;
            }
    };
} // namespace SushiBLAS