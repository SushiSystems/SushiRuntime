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

            /**
             * @brief Constructs the internal implementation.
             *
             * Starts the task scheduler and workload rebalancer.
             *
             * @param ctx The runtime context to attach to.
             */
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

            /**
             * @brief Performs the actual shutdown sequence.
             *
             * Stops the rebalancer and worker pool, avoiding multiple shutdown calls.
             */
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

        /**
         * @brief Constructs an ExecutionEngine.
         *
         * @param ctx The runtime context the engine is bound to.
         */
        ExecutionEngine::ExecutionEngine(RuntimeContext& ctx)
            : impl_(make_sushi<Impl>(ctx))
        {}

        /**
         * @brief Destroys the ExecutionEngine.
         *
         * Ensures that the engine is shut down before destruction.
         */
        ExecutionEngine::~ExecutionEngine()
        {
            // Pimpl destructor will call do_shutdown() if not already done.
        }

        /**
         * @brief Shuts down the execution engine.
         *
         * Stops the task scheduler and the workload rebalancer gracefully.
         */
        void ExecutionEngine::shutdown()
        {
            impl_->do_shutdown();
        }

        /**
         * @brief Retrieves the current execution metrics.
         *
         * Aggregates completed task and steal counts across all workers.
         *
         * @return ExecutionMetrics A structure containing the execution statistics.
         */
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
        sycl::event ExecutionEngine::execute(CompiledGraph& graph)
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

                    // Read the run outcome before re-arming: arm_for_execution()
                    // clears run_failed_ for the next round, so capture it first.
                    // A failed run still drains every node to this sentinel (the
                    // failed task and all later ones are routed through completion),
                    // so reaching here means the run has quiesced either way.
                    const bool failed = sched_ptr->run_failed();
                    std::exception_ptr err = failed ? sched_ptr->run_error() : nullptr;

                    sched_ptr->arm_for_execution(); // re-arm for the NEXT round

                    completion_event_promise->set_value(sycl::event{});
                    if (failed)
                        completion_signal->set_exception(err);
                    else
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
                // The sentinel always fires — a failed run still drains every node
                // to it — and delivers either set_value (success) or set_exception
                // (the first task error), so get() returns or rethrows here.
                completion_signal_future.wait();
                completion_signal_future.get();
            }
            catch (const std::exception& e)
            {
                SR_LOG_ERROR("Graph execution aborted due to an internal error: {}", e.what());
                throw; // Surface the error to user code; the pool stays re-armable.
            }

            sycl::event completion_event = completion_event_future.get();

            SR_LOG_INFO("Graph execution completed.");
            return completion_event;
        }

        // Defined here, not in the header: constructing the CompiledGraph local
        // instantiates its destructor (sushi_ptr<Node>::~sushi_ptr), which needs the
        // complete Node type. The header only forward-declares Node to stay light.

        /**
         * @brief Executes a compiled graph, taking ownership of it.
         *
         * Defined here rather than the header to keep Node forward-declared.
         * Constructs a local CompiledGraph, which requires the complete Node type
         * for its destructor.
         *
         * @param graph The compiled graph to execute via move semantics.
         * @return sycl::event An event that completes when the graph finishes execution.
         */
        sycl::event IExecutionEngine::execute(CompiledGraph&& graph)
        {
            CompiledGraph local = std::move(graph);
            return execute(local);
        }

        // Defined here for the same reason: touching Node::in_degree needs the
        // complete Node type, which only this translation unit sees.

        /**
         * @brief Captures the base in-degree state of all nodes in the graph.
         *
         * Stores the initial dependencies before any execution begins so that
         * they can be restored upon re-arming. Defined here since touching
         * Node::in_degree requires the complete Node type.
         */
        void CompiledGraph::capture_base_state()
        {
            base_in_degree.clear();
            base_in_degree.reserve(all_nodes.size());
            for (auto& n : all_nodes)
                base_in_degree.push_back(n->in_degree.load(std::memory_order_relaxed));
            sentinel_in_degree = sentinel
                ? sentinel->in_degree.load(std::memory_order_relaxed) : 0;
        }

        /**
         * @brief Restores the captured dependency state to allow re-execution.
         *
         * Resets the in-degree of all nodes (including the sentinel) to their
         * base states and clears any distributed offload attempts.
         */
        void CompiledGraph::rearm() noexcept
        {
            const std::size_t count = all_nodes.size();
            for (std::size_t i = 0; i < count && i < base_in_degree.size(); ++i)
                all_nodes[i]->in_degree.store(base_in_degree[i],
                                              std::memory_order_relaxed);
            if (sentinel)
                sentinel->in_degree.store(sentinel_in_degree,
                                          std::memory_order_relaxed);
#ifdef SR_DISTRIBUTED
            for (auto& n : all_nodes)
                n->offload_attempts_.store(0, std::memory_order_relaxed);
#endif
        }

    } // namespace Execution
} // namespace SushiRuntime
