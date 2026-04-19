#pragma once
#include "rebalancer_interfaces.hpp"
#include <memory>
#include <thread>
#include <atomic>
#include <chrono>

namespace SushiRuntime 
{
    namespace Scheduler 
    {
        class WorkloadCoordinator 
        {
            private:
                const ITelemetryProvider& telemetry_;
                ITaskMigrator& migrator_;
                std::unique_ptr<IRebalanceStrategy> strategy_;
                
                std::atomic<bool> running_{false};
                std::thread coordinator_thread_;

                void monitor_loop() 
                {
                    // Koordinatör OS Jitter yaratmasın diye düşük öncelikli çalışabilir veya izole edilebilir.
                    while (running_.load(std::memory_order_relaxed)) 
                    {
                        // Stratejiyi çalıştır ve gerekirse müdahale et
                        strategy_->evaluate_and_rebalance(telemetry_, migrator_);
                        
                        // İşlemciyi boğmamak için bekle (Her 5 milisaniyede bir kalp atışı)
                        std::this_thread::sleep_for(std::chrono::milliseconds(5));
                    }
                }

            public:
                WorkloadCoordinator(const ITelemetryProvider& t, ITaskMigrator& m, std::unique_ptr<IRebalanceStrategy> s)
                    : telemetry_(t), migrator_(m), strategy_(std::move(s)) {}

                ~WorkloadCoordinator() { stop(); }

                void start() 
                {
                    if (!running_.exchange(true)) 
                        coordinator_thread_ = std::thread(&WorkloadCoordinator::monitor_loop, this);
                }

                void stop() 
                {
                    if (running_.exchange(false)) 
                        if (coordinator_thread_.joinable()) 
                            coordinator_thread_.join();
                }

                // OCP: Çalışma zamanında strateji değiştirilebilir!
                void set_strategy(std::unique_ptr<IRebalanceStrategy> new_strategy) 
                {
                    strategy_ = std::move(new_strategy);
                }
        };
    }
}