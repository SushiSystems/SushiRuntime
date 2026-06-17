/**************************************************************************/
/* execution_engine.cpp                                                   */
/**************************************************************************/
/*                          This file is part of:                         */
/*                              SushiRuntime                              */
/*              https://github.com/SushiSystems/SushiRuntime              */
/*                        https://sushisystems.io                         */
/**************************************************************************/
/* Copyright (c) 2026-present Mustafa Garip & Sushi Systems               */
/*                                                                        */
/* Licensed under the Apache License, Version 2.0 (the "License");        */
/* you may not use this file except in compliance with the License.       */
/* You may obtain a copy of the License at                                */
/*                                                                        */
/*     http://www.apache.org/licenses/LICENSE-2.0                         */
/*                                                                        */
/* Unless required by applicable law or agreed to in writing, software    */
/* distributed under the License is distributed on an "AS IS" BASIS,      */
/* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or        */
/* implied. See the License for the specific language governing           */
/* permissions and limitations under the License.                         */
/**************************************************************************/

#include <future>
#include <memory>
#include <SushiRuntime/core/logger.hpp>
#include <SushiRuntime/core/sycl_compat.hpp>
#include <SushiRuntime/graph/node.hpp>
#include <SushiRuntime/scheduler/workload_coordinator.hpp>
#include <SushiRuntime/scheduler/thermal_proxy_strategy.hpp>
#include <SushiRuntime/scheduler/task_scheduler.hpp>
#include <SushiRuntime/execution/runtime_context.hpp>
#include <SushiRuntime/execution/execution_engine.hpp>

namespace SushiRuntime
{
    namespace Execution
    {
        /**
         * @brief Private implementation of ExecutionEngine (Pimpl).
         *
         * Owns a single persistent TaskScheduler. The scheduler is started
         * once in the constructor and stopped exactly once in shutdown().
         *
         * Madde 7: The scheduler is no longer re-created per graph execution.
         * Madde 18: Shutdown is explicitly ordered via RuntimeContext::Impl::~Impl,
         *           so the scheduler is always stopped before queues are torn down.
         */
        struct ExecutionEngine::Impl : public Core::RefCounted
        {
            /** @brief Persistent scheduler that survives multiple graph executions. */
            SushiRuntime::Scheduler::TaskScheduler scheduler;
            std::unique_ptr<SushiRuntime::Scheduler::WorkloadCoordinator> rebalancer;

            /** @brief Guards against double-shutdown. */
            std::atomic<bool> is_shutdown{false};

            // DÜZELTME: 'context(ctx),' kısmı silindi, sadece scheduler başlatılıyor.
            Impl(RuntimeContext& ctx) : scheduler(ctx)
            {
                scheduler.start();
                
                // Rebalancer'ı ayağa kaldır
                rebalancer = std::make_unique<SushiRuntime::Scheduler::WorkloadCoordinator>(
                    scheduler, 
                    scheduler, 
                    std::make_unique<SushiRuntime::Scheduler::ThermalProxyStrategy>()
                );
                rebalancer->start();
                
                SR_LOG_INFO("Workload Rebalancer started.");
            }

            ~Impl() override
            {
                do_shutdown();
            }

            void do_shutdown()
            {
                if (rebalancer) rebalancer->stop();
                
                bool expected = false;
                if (!is_shutdown.compare_exchange_strong(expected, true,
                                                         std::memory_order_acq_rel))
                    return;

                SR_LOG_INFO("Shutting down worker pool.");
                scheduler.stop();
                SR_LOG_INFO("All workers joined.");
            }
        };

        // ------------------------------------------------------------------ //
        //  ExecutionEngine public interface                                   //
        // ------------------------------------------------------------------ //

        ExecutionEngine::ExecutionEngine(RuntimeContext& ctx)
            : impl_(make_sushi<Impl>(ctx))
        {}

        ExecutionEngine::~ExecutionEngine()
        {
            // Pimpl destructor will call do_shutdown() if not already done.
        }

        void ExecutionEngine::shutdown()
        {
            impl_->do_shutdown();
        }

        ExecutionMetrics ExecutionEngine::metrics() const
        {
            ExecutionMetrics m;
            m.local_steals  = impl_->scheduler.get_local_steal_count();
            m.remote_steals = impl_->scheduler.get_remote_steal_count();
            const size_t workers = impl_->scheduler.get_worker_count();
            for (size_t i = 0; i < workers; ++i)
                m.tasks_completed += impl_->scheduler.get_completion_rate(static_cast<int>(i));
            return m;
        }

#ifdef SR_DISTRIBUTED
        SushiRuntime::Scheduler::TaskScheduler& ExecutionEngine::scheduler_ref()
        {
            return impl_->scheduler;
        }
#endif

        /**
         * @brief Executes a compiled graph on the persistent worker pool.
         *
         * Responsibility split (Madde 26 / SRP fix):
         *  - TaskGraph::compile() builds the DAG and the root list.
         *  - ExecutionEngine::execute() handles thread orchestration, the
         *    sentinel node setup, promise/future signalling, and graph cleanup.
         *
         * The persistent scheduler is "re-armed" for this execution round via
         * arm_for_execution() instead of being destroyed and recreated (Madde 7).
         */
        sycl::event ExecutionEngine::execute(CompiledGraph graph)
        {
            SR_THROW_IF(impl_->is_shutdown.load(std::memory_order_acquire), "Execute called after shutdown.");
            SR_THROW_IF(graph.roots.empty(), "Received a compiled graph with no root nodes.");

            SR_LOG_INFO("Executing graph ({} root nodes).", graph.roots.size());

            // -----------------------------------------------------------------
            // Step 1: Wire the sentinel node's host_work before submitting roots.
            //         The sentinel captures the promises and the scheduler pointer
            //         so it can signal completion from inside the worker thread.
            // -----------------------------------------------------------------
            auto completion_signal        = std::make_shared<std::promise<void>>();
            auto completion_signal_future = completion_signal->get_future();
            auto completion_event_promise = std::make_shared<std::promise<sycl::event>>();
            auto completion_event_future  = completion_event_promise->get_future();

            auto* sched_ptr = &impl_->scheduler;

            impl_->scheduler.set_error_callback([completion_signal](std::exception_ptr ep) 
            {
                try 
                {
                    completion_signal->set_exception(ep);
                } 
                catch (const std::future_error&) 
                {
                    // If the sentinel is already completed (set_value called), this will throw an error; ignore it.
                }
            });

            graph.sentinel->metadata.name = "graph_completion_sentinel";
            graph.sentinel->mode          = SushiRuntime::Scheduler::Task::ExecutionMode::HOST;
            graph.sentinel->host_work     =
                [sched_ptr, completion_signal, completion_event_promise]
                (sycl::queue&, const std::vector<sycl::event>& deps) -> sycl::event
                {
                    // The sentinel is the final node: the DAG only dispatches it
                    // once every predecessor's event has completed. Signal completion
                    // directly in C++ rather than through a SYCL host operation —
                    // AdaptiveCpp does not run a custom-operation callback until its
                    // event is waited on, so a host-task-based sentinel would never
                    // fire and execute() would block forever. Wait on the predecessor
                    // events first so any device work is genuinely flushed (this also
                    // drives AdaptiveCpp's queue to completion) before we report done.
                    // Copy each event: AdaptiveCpp's event::wait() is non-const,
                    // and deps is a const vector. sycl::event is a cheap handle.
                    for (auto e : deps)
                        if (e != sycl::event{})
                            e.wait();

                    sched_ptr->arm_for_execution(); // re-arm for the NEXT round
                    completion_event_promise->set_value(sycl::event{});
                    completion_signal->set_value();
                    return {};
                };

            // -----------------------------------------------------------------
            // Step 2: Arm the scheduler for this execution round, then submit roots.
            //         arm_for_execution() resets the internal "done" state without
            //         touching the live worker threads.
            // -----------------------------------------------------------------
            impl_->scheduler.arm_for_execution();

            for (auto* root : graph.roots)
                impl_->scheduler.submit(root);

            // -----------------------------------------------------------------
            // Step 3: Block the calling thread until the sentinel fires.
            // -----------------------------------------------------------------

            try 
            {
                completion_signal_future.wait(); // Hala bloklar, ama artık error_callback ile de açılabilir.
                completion_signal_future.get();  // Eğer exception set edilmişse, BURADA fırlatılır!
            }
            catch (const std::exception& e)
            {
                SR_LOG_ERROR("Graph execution aborted due to an internal error: {}", e.what());
                throw; // Kullanıcı koduna hatayı taşı (Deadlock engellendi)
            }

            sycl::event completion_event = completion_event_future.get();

            SR_LOG_INFO("Graph execution completed.");
            return completion_event;
        }

    } // namespace Execution
} // namespace SushiRuntime
