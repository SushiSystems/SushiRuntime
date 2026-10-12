/**************************************************************************/
/* scheduler_execution_engine.cpp                                         */
/**************************************************************************/
/*                          This file is part of:                         */
/*                              SushiRuntime                              */
/*              https://github.com/SushiSystems/SushiRuntime              */
/*                         https://sushisystems.io                        */
/**************************************************************************/
/* Copyright (c) 2026-present Mustafa Garip & Sushi Systems               */
/*                                                                        */
/* Licensed under the PolyForm Noncommercial License 1.0.0 (the           */
/* "License"); you may not use this file except in compliance with the    */
/* License. You may obtain a copy of the License at                       */
/*                                                                        */
/*     https://polyformproject.org/licenses/noncommercial/1.0.0           */
/*                                                                        */
/* Noncommercial use is free. Commercial use requires a separate licence  */
/* from Sushi Systems; see COMMERCIAL.md. The software is provided        */
/* "as is", without warranty of any kind.                                 */
/**************************************************************************/

/**
 * @file scheduler_execution_engine.cpp
 * @brief Runs compiled graphs on a persistent scheduler and reports their profiling.
 * @author Mustafa Garip
 */

#include <atomic>
#include <memory>
#include <mutex>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <chrono>
#include <optional>
#include <utility>
#include <vector>
#include <SushiRuntime/core/error.hpp>
#include <SushiRuntime/core/result.hpp>
#include <SushiRuntime/device/device_queue_set.hpp>
#include <SushiRuntime/logging/logger.hpp>
#include <SushiRuntime/sycl/sycl_compat.hpp>
#include <SushiRuntime/task/node.hpp>
#include <SushiRuntime/graph/compiled_graph.hpp>
#include <SushiRuntime/machine/worker_pool.hpp>
#include <SushiRuntime/scheduler/workload_coordinator.hpp>
#include <SushiRuntime/scheduler/thermal_proxy_strategy.hpp>
#include <SushiRuntime/scheduler/task_scheduler.hpp>
#include <SushiRuntime/execution/runtime_context.hpp>
#include <SushiRuntime/execution/runtime_settings.hpp>
#include <SushiRuntime/execution/scheduler_execution_engine.hpp>

namespace SushiRuntime
{
    namespace Internal::Execution
    {
        /**
         * @brief Holds a reusable completion gate for a graph run (modules/engine/execution/README.md).
         */
        struct CompletionLatch
        {
            std::mutex              mutex;          /**< Guards the fields below. */
            std::condition_variable cv;            /**< Signalled when the run quiesces. */
            bool                    done = false;   /**< True once the sentinel reported in. */
            std::optional<Error>    error;          /**< First failure of the finished run. */

            /** @brief Clears the gate for the next step before roots are submitted. */
            void reset()
            {
                std::lock_guard<std::mutex> lock(mutex);
                done = false;
                error.reset();
            }

            /**
             * @brief Publishes a finished run's outcome and wakes the waiter.
             * @param failure The first failure of the run, or nothing when no task failed.
             */
            void signal(std::optional<Error> failure)
            {
                {
                    std::lock_guard<std::mutex> lock(mutex);
                    error = std::move(failure);
                    done = true;
                }
                cv.notify_one();
            }

            /** @brief Blocks the caller until signal() reports the run done. */
            void wait()
            {
                std::unique_lock<std::mutex> lock(mutex);
                cv.wait(lock, [this] { return done; });
            }

            /**
             * @brief Reports whether the run has finished, without blocking.
             *
             * The asynchronous path's way of asking "would waiting stall me?", so a
             * caller can choose between overlapping more work and settling up.
             * @return True once signal() has published an outcome.
             */
            bool poll()
            {
                std::lock_guard<std::mutex> lock(mutex);
                return done;
            }
        };

        namespace
        {
            /** @brief Bit of the run marker a scheduled run holds from submit() to complete(). */
            constexpr std::uint32_t K_SCHEDULED_RUN = 1u;

            /** @brief Amount one chained run adds to the run marker while it lasts. */
            constexpr std::uint32_t K_CHAINED_RUN = 2u;

            /**
             * @brief Clears the scheduled-run bit of a run marker on scope exit, unless disarmed.
             * @see modules/engine/execution/README.md
             */
            class ScheduledRunRelease
            {
                public:
                    /** @brief Binds the guard to @p marker, whose scheduled-run bit is set. */
                    explicit ScheduledRunRelease(std::atomic<std::uint32_t>& marker) noexcept
                        : marker_(&marker)
                    {

                    }

                    /** @brief Clears the bit unless disarm() was called. */
                    ~ScheduledRunRelease()
                    {
                        if (marker_)
                            marker_->fetch_and(~K_SCHEDULED_RUN, std::memory_order_acq_rel);
                    }

                    ScheduledRunRelease(const ScheduledRunRelease&) = delete;
                    ScheduledRunRelease& operator=(const ScheduledRunRelease&) = delete;
                    ScheduledRunRelease(ScheduledRunRelease&&) = delete;
                    ScheduledRunRelease& operator=(ScheduledRunRelease&&) = delete;

                    /** @brief Leaves the bit set for the scope that ends the run. */
                    void disarm() noexcept
                    {
                        marker_ = nullptr;
                    }

                private:
                    /** @brief The marker to clear; null once disarmed. */
                    std::atomic<std::uint32_t>* marker_;
            };
        } // namespace

        /**
         * @brief Owns the scheduler, completion gate and rebalancer (modules/engine/execution/README.md).
         */
        struct SchedulerExecutionEngine::Impl : public RefCounted
        {
            /** @brief Persistent scheduler that survives multiple graph executions. */
            SushiRuntime::Internal::Scheduler::TaskScheduler scheduler;

            /** @brief The context's device queues, asked for their asynchronous errors. */
            Internal::Device::DeviceQueueSet& devices;

            /** @brief Reusable per-step completion gate. */
            CompletionLatch completion;
            std::unique_ptr<SushiRuntime::Internal::Scheduler::WorkloadCoordinator> rebalancer;

            /** @brief Whether the background rebalancer should be running. */
            std::atomic<bool> rebalancer_on{false};

            /** @brief Guards against double-shutdown. */
            std::atomic<bool> is_shutdown{false};

            /**
             * @brief Counts the runs in progress: K_SCHEDULED_RUN while a scheduled run holds
             *        the engine, plus K_CHAINED_RUN for each live chained run.
             * @see modules/engine/execution/README.md
             */
            std::atomic<std::uint32_t> run_marker{0};

            /**
             * @brief Constructs the internal implementation.
             *
             * Starts the task scheduler and workload rebalancer.
             *
             * @param context The runtime context to attach to.
             */
            Impl(RuntimeContext& context)
                : scheduler(worker_pool_settings(context.settings()), context.topology(),
                            context.devices(), context.memory()),
                  devices(context.devices())
            {
                scheduler.start();

                // The coordinator always exists and is off by default (modules/engine/execution/README.md).
                rebalancer = std::make_unique<SushiRuntime::Internal::Scheduler::WorkloadCoordinator>(
                    scheduler,
                    scheduler,
                    std::make_unique<SushiRuntime::Internal::Scheduler::ThermalProxyStrategy>()
                );

                if (context.settings().rebalancer)
                {
                    rebalancer_on.store(true, std::memory_order_relaxed);
                    rebalancer->start();
                    SR_LOG_INFO("Workload rebalancer started.");
                }
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

        /**
         * @brief Constructs an SchedulerExecutionEngine.
         *
         * @param context The runtime context the engine is bound to.
         */
        SchedulerExecutionEngine::SchedulerExecutionEngine(RuntimeContext& context)
            : impl_(make_sushi<Impl>(context))
        {}

        /**
         * @brief Destroys the SchedulerExecutionEngine.
         *
         * Ensures that the engine is shut down before destruction.
         */
        SchedulerExecutionEngine::~SchedulerExecutionEngine()
        {
            // Pimpl destructor will call do_shutdown() if not already done.
        }

        /**
         * @brief Shuts down the execution engine.
         *
         * Stops the task scheduler and the workload rebalancer gracefully.
         */
        void SchedulerExecutionEngine::shutdown()
        {
            impl_->do_shutdown();
        }

        /**
         * @brief Requests cancellation of the active run.
         *
         * Forwards to the scheduler, which makes remaining tasks drain without
         * executing while the sentinel still fires, so the blocked execute()
         * returns promptly.
         */
        void SchedulerExecutionEngine::cancel() noexcept
        {
            impl_->scheduler.request_cancellation();
        }

        /**
         * @brief Reports whether the active run has been cancelled.
         * @return True once cancel() was called and before reset_cancellation().
         */
        bool SchedulerExecutionEngine::is_cancelled() const
        {
            return impl_->scheduler.run_cancelled();
        }

        /**
         * @brief Clears the cancellation flag so the next run starts clean.
         */
        void SchedulerExecutionEngine::reset_cancellation()
        {
            impl_->scheduler.reset_cancellation();
        }

        /**
         * @brief Turns the background thermal rebalancer on or off.
         *
         * start()/stop() are idempotent on the coordinator, so toggling is safe.
         * The flag records intent for rebalancer_enabled() without reaching into the
         * coordinator's internal state.
         */
        void SchedulerExecutionEngine::set_rebalancer_enabled(bool enabled)
        {
            if (!impl_->rebalancer) return;
            impl_->rebalancer_on.store(enabled, std::memory_order_relaxed);
            if (enabled) impl_->rebalancer->start();
            else         impl_->rebalancer->stop();
        }

        /**
         * @brief Reports whether the background rebalancer is currently running.
         */
        bool SchedulerExecutionEngine::rebalancer_enabled() const
        {
            return impl_->rebalancer && impl_->rebalancer_on.load(std::memory_order_relaxed);
        }

        /**
         * @brief Reports the worker pool the scheduler built, after clamping.
         */
        WorkerPool SchedulerExecutionEngine::worker_pool() const
        {
            WorkerPool info;
            info.count      = impl_->scheduler.worker_count();
            info.first_core = impl_->scheduler.first_core();
            info.pinning    = impl_->scheduler.pinning();
            return info;
        }

        /**
         * @brief Zeroes node and per-worker profiling accumulators before a run.
         */
        void SchedulerExecutionEngine::reset_profiling(Internal::Graph::CompiledGraph& graph, std::size_t expected_invocations)
        {
            impl_->scheduler.reset_worker_times();
            // Once per run, before any step's arm_for_execution() draws from it.
            impl_->scheduler.reset_dispatch_step();

            // reserve() folds in the last run's true per-row demand, so growth is outside the poller.
            graph.profiling_ledger.reserve(graph.profiling_multiplicities,
                                           static_cast<std::uint32_t>(expected_invocations));
            graph.profiling_ledger.set_run_start(
                static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::steady_clock::now().time_since_epoch()).count()));
        }

        /**
         * @brief Reads the accumulators into a report's profiling breakdown.
         */
        void SchedulerExecutionEngine::collect_profiling(Internal::Graph::CompiledGraph& graph,
                                                RunReport& report) const
        {
            // One capture path (F4): node_timings below is aggregated from these same records.
            graph.profiling_ledger.collect(report);
            report.clock = graph.profiling_ledger.pairing();

            const auto& host_native = graph.profiling_host_native;
            const std::int64_t offset = static_cast<std::int64_t>(report.clock.steady_ns) -
                                        static_cast<std::int64_t>(report.clock.event_ns);
            for (auto& rec : report.node_events)
            {
                if (rec.node_index >= host_native.size() || !host_native[rec.node_index])
                    continue;
                auto convert = [offset](std::uint64_t steady_ns) noexcept -> std::uint64_t
                {
                    return static_cast<std::uint64_t>(static_cast<std::int64_t>(steady_ns) - offset);
                };
                rec.submit_ns = convert(rec.submit_ns);
                rec.start_ns  = convert(rec.start_ns);
                rec.end_ns    = convert(rec.end_ns);
            }

            // node_timings has one row per dispatched registration_index (modules/engine/execution/README.md).
            struct NodeAgg { std::uint64_t invocations = 0; std::uint64_t device_ns = 0; std::uint64_t host_ns = 0; };
            std::vector<NodeAgg> agg(graph.profiling_multiplicities.size());
            for (const auto& rec : report.node_events)
            {
                if (rec.node_index >= agg.size()) continue;
                auto& a = agg[rec.node_index];
                ++a.invocations;
                const std::uint64_t dur = rec.end_ns > rec.start_ns ? rec.end_ns - rec.start_ns : 0;
                if (rec.node_index < host_native.size() && host_native[rec.node_index])
                    a.host_ns += dur;
                else
                    a.device_ns += dur;
            }

            // t.plan_index stays Node::plan_index; the first node sharing the index (F5) supplies it.
            for (std::size_t idx = 0; idx < agg.size(); ++idx)
            {
                if (agg[idx].invocations == 0) continue;

                NodeTiming t;
                for (auto& n : graph.all_nodes)
                {
                    if (n->is_sentinel || n->registration_index != idx)
                        continue;
                    t.plan_index = n->plan_index;
                    t.name = n->metadata.name;
                    t.operation = n->metadata.op_id;
                    break;
                }
                t.invocations = static_cast<std::size_t>(agg[idx].invocations);
                t.device_ms   = agg[idx].device_ns / 1.0e6;
                t.host_ms     = agg[idx].host_ns / 1.0e6;
                report.node_timings.push_back(std::move(t));
            }

            double polling_ms_total = 0.0;
            const std::size_t workers = impl_->scheduler.worker_count();
            for (std::size_t i = 0; i < workers; ++i)
            {
                const auto s = impl_->scheduler.worker_times(static_cast<int>(i));
                WorkerTiming w;
                w.worker_id   = static_cast<int>(i);
                w.busy_ms     = s.busy_ns / 1.0e6;
                w.stealing_ms = s.stealing_ns / 1.0e6;
                w.polling_ms  = s.polling_ns / 1.0e6;
                w.idle_ms     = s.idle_ns / 1.0e6;
                polling_ms_total += w.polling_ms;
                report.worker_timings.push_back(w);
            }

            // Completion polling is scheduler overhead between kernels (modules/engine/execution/README.md).
            report.scheduling_overhead_ms = polling_ms_total;
        }

        /**
         * @brief Retrieves the current execution metrics.
         *
         * Aggregates completed task and steal counts across all workers.
         *
         * @return ExecutionMetrics A structure containing the execution statistics.
         */
        ExecutionMetrics SchedulerExecutionEngine::metrics() const
        {
            ExecutionMetrics m;
            m.local_steals  = impl_->scheduler.local_steal_count();
            m.remote_steals = impl_->scheduler.remote_steal_count();
            const size_t workers = impl_->scheduler.worker_count();
            for (size_t i = 0; i < workers; ++i)
                m.tasks_completed += impl_->scheduler.completed_task_count(static_cast<int>(i));
            m.tasks_offloaded = static_cast<std::size_t>(impl_->scheduler.offloaded_task_count());
            return m;
        }

        void SchedulerExecutionEngine::prepare(Internal::Graph::CompiledGraph& graph)
        {
            impl_->scheduler.prepare_offload(graph.all_nodes);
        }

        /** @brief Forwards to the scheduler's set_offload_coordinator(). */
        void SchedulerExecutionEngine::install_offload(Internal::Graph::OffloadCoordinator* offload)
        {
            impl_->scheduler.set_offload_coordinator(offload);
        }

        Internal::Graph::OffloadSink& SchedulerExecutionEngine::offload_sink() noexcept
        {
            return impl_->scheduler;
        }

        /**
         * @brief Runs a compiled graph on the persistent worker pool and waits.
         */
        Result<void> SchedulerExecutionEngine::execute(Internal::Graph::CompiledGraph& graph)
        {
            // execute() is submit() then complete(), nothing more (modules/engine/execution/README.md).
            Result<void> submitted = submit(graph);
            if (!submitted)
                return submitted;
            return complete();
        }

        /**
         * @brief Starts a run on the worker pool without waiting for it (modules/engine/execution/README.md).
         * @pre No other run is in progress on this engine.
         */
        Result<void> SchedulerExecutionEngine::submit(Internal::Graph::CompiledGraph& graph)
        {
            if (impl_->is_shutdown.load(std::memory_order_acquire))
                return make_error(Error{ErrorCode::InvalidState, "Execute called after shutdown."});
            if (graph.roots.empty())
                return make_error(Error{ErrorCode::InvalidGraph,
                                  "Received a compiled graph with no root nodes."});

            // An overlapping run is refused and leaves the owner's bit set (modules/engine/execution/README.md).
            if ((impl_->run_marker.fetch_or(K_SCHEDULED_RUN, std::memory_order_acq_rel) &
                 K_SCHEDULED_RUN) != 0)
                return make_error(Error{ErrorCode::InvalidState,
                                  "ExecutionEngine::submit() is not reentrant: a graph run is "
                                  "already in progress on this engine."});

            // The guard clears the bit only on an early exit (modules/engine/execution/README.md).
            ScheduledRunRelease submit_guard(impl_->run_marker);

            // DEBUG, not INFO: a run happens once per replayed step (modules/engine/execution/README.md).
            SR_LOG_DEBUG("Executing graph ({} root nodes).", graph.roots.size());

            // The sentinel's host_work is wired once per plan (modules/engine/execution/README.md).
            if (!graph.sentinel->host_work)
            {
                graph.sentinel->metadata.name = "graph_completion_sentinel";
                graph.sentinel->is_sentinel   = true;
                graph.sentinel->mode          = SushiRuntime::Internal::Scheduler::Task::ExecutionMode::Host;

                auto* latch     = &impl_->completion;
                auto* sched_ptr = &impl_->scheduler;
                // Fixed for the sentinel's lifetime; a changed request rewires a fresh one.
                const bool profiling = graph.profiling;
                graph.sentinel->host_work =
                    [latch, sched_ptr, profiling]
                    (sycl::queue&, const std::vector<sycl::event>& dependencies) -> sycl::event
                    {
                        // Deps are waited, then the latch is signalled (modules/engine/execution/README.md).
                        const sycl::event no_event{};
                        for (auto e : dependencies)
                            if (e != no_event)
                                e.wait();

                        // The outcome is read before re-arming clears it (modules/engine/execution/README.md).
                        std::optional<Error> failure = sched_ptr->run_error();

                        // The pool stays armed through the drain tail (modules/engine/execution/README.md).
                        sched_ptr->arm_for_execution(profiling);

                        latch->signal(std::move(failure));
                        return {};
                    };
            }

            // The latch is reset before roots are submitted (modules/engine/execution/README.md).
            impl_->completion.reset();
            // Wired before arming; null when this plan does not request profiling.
            impl_->scheduler.set_profiling_ledger(graph.profiling ? &graph.profiling_ledger : nullptr);
            impl_->scheduler.arm_for_execution(graph.profiling);
            // The step counter advances here, not in arm_for_execution() (modules/engine/execution/README.md).
            impl_->scheduler.advance_dispatch_step();

            // One batched injector pass submits every root (modules/engine/execution/README.md).
            impl_->scheduler.submit_batch(graph.roots);

            // Handed off successfully: the run now owns the bit until complete().
            submit_guard.disarm();
            return {};
        }

        /**
         * @brief Waits for the submitted run, drains the pool, releases the engine.
         *
         * The blocking half of a run. The flag is released whatever the run returns, so a
         * failed run leaves the engine usable.
         * @see modules/engine/execution/README.md
         */
        Result<void> SchedulerExecutionEngine::complete()
        {
            const ScheduledRunRelease release_guard(impl_->run_marker);

            impl_->completion.wait();

            // The pool drains before the caller may destroy the graph (modules/engine/execution/README.md).
            impl_->scheduler.wait_quiescent();

            // The pool is disarmed here, under the submission guard (modules/engine/execution/README.md).
            impl_->scheduler.disarm();

            // The queues are drained of their asynchronous errors whether or not a task failed.
            Result<void> raised = impl_->devices.raise_asynchronous_errors();

            if (impl_->completion.error)
            {
                SR_LOG_ERROR("Graph execution aborted due to a task error.");
                return make_error(std::move(*impl_->completion.error));
            }
            if (!raised)
            {
                SR_LOG_ERROR("Graph execution failed: a device queue reported an asynchronous "
                             "error.");
                return raised;
            }

            SR_LOG_DEBUG("Graph execution completed."); // per-step; see the DEBUG note above
            return {};
        }

        /**
         * @brief Whether the submitted run's sentinel has already reported in.
         *
         * Note that this answers "has the DAG finished", not "is it safe to destroy
         * the plan" — the pool still has bookkeeping to drain, which complete()
         * waits for. Use it to decide whether to keep overlapping work, never as a
         * substitute for complete().
         */
        bool SchedulerExecutionEngine::run_finished()
        {
            return impl_->completion.poll();
        }

        /**
         * @brief Reports whether a scheduled or a chained run is in progress on this engine.
         */
        bool SchedulerExecutionEngine::run_in_progress() const noexcept
        {
            // acq_rel writes, acquire load: whoever reads the marker sees what its writer did before.
            return impl_->run_marker.load(std::memory_order_acquire) != 0;
        }

        /** @brief Marks one chained run in progress. */
        void SchedulerExecutionEngine::begin_chained_run() noexcept
        {
            impl_->run_marker.fetch_add(K_CHAINED_RUN, std::memory_order_acq_rel);
        }

        /** @brief Clears the mark one begin_chained_run() set. */
        void SchedulerExecutionEngine::end_chained_run() noexcept
        {
            impl_->run_marker.fetch_sub(K_CHAINED_RUN, std::memory_order_acq_rel);
        }

        /**
         * @brief Executes a compiled graph, taking ownership of it (modules/engine/execution/README.md).
         * @param graph The compiled graph to execute via move semantics.
         */
        Result<void> ExecutionEngine::execute(Internal::Graph::CompiledGraph&& graph)
        {
            Internal::Graph::CompiledGraph local = std::move(graph);
            return execute(local);
        }

    } // namespace Execution
} // namespace SushiRuntime
