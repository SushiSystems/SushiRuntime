#pragma once
#include <vector>
#include <cstdint>

namespace SushiRuntime 
{
    namespace Scheduler 
    {
        // 1. İşçilerin anlık durumunu okuyan arayüz (Interface Segregation)
        class ITelemetryProvider 
        {
            public:
                virtual ~ITelemetryProvider() = default;
                virtual size_t get_worker_count() const = 0;
                virtual size_t get_queue_size(int worker_id) const = 0;
                // İşçinin son 1 milisaniyede bitirdiği iş sayısı (Hız/Throttling göstergesi)
                virtual size_t get_completion_rate(int worker_id) const = 0; 
        };

        // 2. İşçiler arasında zorla görev taşıyan arayüz
        class ITaskMigrator 
        {
            public:
                virtual ~ITaskMigrator() = default;
                // Slow_worker'dan fast_worker'a 'count' kadar görevi kilitlenmeden (lock-free) taşır
                virtual void migrate_tasks(int from_worker_id, int to_worker_id, size_t count) = 0;
        };

        // 3. Strateji Arayüzü (Open/Closed Principle)
        class IRebalanceStrategy 
        {
            public:
                virtual ~IRebalanceStrategy() = default;
                virtual void evaluate_and_rebalance(const ITelemetryProvider& telemetry, ITaskMigrator& migrator) = 0;
        };
    }
}