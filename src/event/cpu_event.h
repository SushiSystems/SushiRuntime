#pragma once

#include <string>
#include <atomic> 
#include <thread>

#include "event.h"
#include "../common/logging/logger.h"

namespace SushiBLAS 
{
    /**
     * @brief CPUEvent: Host tarafında işlem tamamlanma sinyali.
     * * @note MODEL:
     * - CPUStream senkron çalıştığı için, record() çağrıldığı anda event TAMAMLANMIŞ sayılır.
     * - is_completed() sorgusu, record() sonrası her zaman true döner.
     * - Thread-safety std::atomic ile sağlanmıştır.
     */
    class CPUEvent final : public Event 
    {
        private:
            std::atomic<bool> completed{false};
            std::string name_;

        public:
            CPUEvent(const char* name) : name_(name ? name : "CPUEvent"), completed(false) {}
            
            // Event tetiklendiğinde (Stream o noktaya ulaştığında) çağrılır.
            void record(Stream* /*unused*/) override 
            {
                // Memory Order Release: Bu event'i bekleyen diğer thread'ler, 
                // bu noktadan önceki tüm memory yazımlarını görebilsin.
                completed.store(true, std::memory_order_release);
            }

            bool is_completed() const override 
            {
                // Memory Order Acquire: Release ile senkronize olur.
                return completed.load(std::memory_order_acquire);
            }

            void synchronize() override 
            {
                // Busy-wait loop (veya thread yield)
                // CPUStream senkron olduğu için genelde bu döngüye girmeden geçer.
                // Ancak başka bir thread event'i resetleyip tekrar kullanırsa bekleme olabilir.
                int spins = 0;
                while(!is_completed()) 
                { 
                    if(spins++ > 1000) std::this_thread::yield();
                }
            }

            const char* get_name() const override { return name_.c_str(); }
            
            float elapsed_time(Event* /*start*/) override 
            {
                // TODO: std::chrono ile hassas ölçüm eklenecek.
                return 0.0f;
            }
    };
} // namespace SushiBLAS