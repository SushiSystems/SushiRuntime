/**************************************************************************/
/* execution_engine.cpp                                                   */
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
 * @file execution_engine.cpp
 * @brief Runs compiled graphs on a persistent scheduler and reports their profiling.
 * @author Mustafa Garip
 */

#include <memory>
#include <mutex>
#include <condition_variable>
#include <exception>
#include <cstddef>
#include <cstdint>
#include <chrono>
#include <vector>
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
         * @brief Holds a reusable completion gate for a graph run (src/execution/README.md).
         */
        struct CompletionLatch
        {
            std::mutex              mutex;          /**< Guards the fields below. */
            std::condition_variable cv;            /**< Signalled when the run quiesces. */
            bool                    done = false;   /**< True once the sentinel reported in. */
            bool                    failed = false; /**< Whether the finished run failed. */
            std::exception_ptr      error;          /**< First task exception, when failed. */

            /** @brief Clears the gate for the next step before roots are submitted. */
            void reset()
            {
                std::lock_guard<std::mutex> lock(mutex);
                done = false;
                failed = false;
                error = nullptr;
            }

            /**
             * @brief Publishes a finished run's outcome and wakes the waiter.
             * @param run_failed Whether a task threw during the run.
             * @param err        The first exception when @p run_failed, else null.
             */
            void signal(bool run_failed, std::exception_ptr err)
            {
                {
                    std::lock_guard<std::mutex> lock(mutex);
                    failed = run_failed;
                    error = std::move(err);
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

        /**
         * @brief Owns the scheduler, completion gate and rebalancer (src/execution/README.md).
         */
        struct ExecutionEngine::Impl : public Core::RefCounted
        {
            /** @brief Persistent scheduler that survives multiple graph executions. */
            SushiRuntime::Scheduler::TaskScheduler scheduler;

            /** @brief Reusable per-step completion gate (replaces promise/future). */
            CompletionLatch completion;
            std::unique_ptr<SushiRuntime::Scheduler::WorkloadCoordinator> rebalancer;

            /** @brief Whether the background rebalancer should be running. */
            std::atomic<bool> rebalancer_on{false};

            /** @brief Guards against double-shutdown. */
            std::atomic<bool> is_shutdown{false};

            /**
             * @brief Serialises graph runs: execute() shares one CompletionLatch,
             *        sentinel and scheduler-arm state, so two concurrent runs would
             *        clobber each other (a reset erasing the other's pending
             *        completion, a sentinel waking the wrong waiter). Detect and
             *        reject overlap rather than corrupt it.
             */
            std::atomic<bool> run_in_progress{false};

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

                // The coordinator always exists and is off by default (src/execution/README.md).
                rebalancer = std::make_unique<SushiRuntime::Scheduler::WorkloadCoordinator>(
                    scheduler,
                    scheduler,
                    std::make_unique<SushiRuntime::Scheduler::ThermalProxyStrategy>()
                );

                if (ctx.settings().rebalancer)
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
         * @brief Requests cancellation of the active run.
         *
         * Forwards to the scheduler, which makes remaining tasks drain without
         * executing while the sentinel still fires, so the blocked execute()
         * returns promptly.
         */
        void ExecutionEngine::cancel()
        {
            impl_->scheduler.request_cancellation();
        }

        /**
         * @brief Reports whether the active run has been cancelled.
         * @return True once cancel() was called and before reset_cancellation().
         */
        bool ExecutionEngine::is_cancelled() const
        {
            return impl_->scheduler.run_cancelled();
        }

        /**
         * @brief Clears the cancellation flag so the next run starts clean.
         */
        void ExecutionEngine::reset_cancellation()
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
        void ExecutionEngine::set_rebalancer_enabled(bool enabled)
        {
            if (!impl_->rebalancer) return;
            impl_->rebalancer_on.store(enabled, std::memory_order_relaxed);
            if (enabled) impl_->rebalancer->start();
            else         impl_->rebalancer->stop();
        }

        /**
         * @brief Reports whether the background rebalancer is currently running.
         */
        bool ExecutionEngine::rebalancer_enabled() const
        {
            return impl_->rebalancer && impl_->rebalancer_on.load(std::memory_order_relaxed);
        }

        /**
         * @brief Reports the worker pool the scheduler built, after clamping.
         */
        WorkerPoolInfo ExecutionEngine::worker_pool() const
        {
            WorkerPoolInfo info;
            info.count      = impl_->scheduler.num_workers;
            info.first_core = impl_->scheduler.first_core();
            info.pinning    = impl_->scheduler.pinning();
            return info;
        }

        /**
         * @brief Zeroes node and per-worker profiling accumulators before a run.
         */
        void ExecutionEngine::reset_profiling(CompiledGraph& graph, std::size_t expected_invocations)
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
        void ExecutionEngine::collect_profiling(CompiledGraph& graph,
                                                Core::RunReport& report) const
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

            // node_timings has one row per dispatched registration_index (src/execution/README.md).
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

                Core::NodeTiming t;
                for (auto& n : graph.all_nodes)
                    if (!n->is_sentinel && n->registration_index == idx) { t.plan_index = n->plan_index; t.name = n->metadata.name; break; }
                t.invocations = static_cast<std::size_t>(agg[idx].invocations);
                t.device_ms   = agg[idx].device_ns / 1.0e6;
                t.host_ms     = agg[idx].host_ns / 1.0e6;
                report.node_timings.push_back(std::move(t));
            }

            double polling_ms_total = 0.0;
            const std::size_t workers = impl_->scheduler.get_worker_count();
            for (std::size_t i = 0; i < workers; ++i)
            {
                const auto s = impl_->scheduler.get_worker_times(static_cast<int>(i));
                Core::WorkerTiming w;
                w.worker_id   = static_cast<int>(i);
                w.busy_ms     = s.busy_ns / 1.0e6;
                w.stealing_ms = s.stealing_ns / 1.0e6;
                w.polling_ms  = s.polling_ns / 1.0e6;
                w.idle_ms     = s.idle_ns / 1.0e6;
                polling_ms_total += w.polling_ms;
                report.worker_timings.push_back(w);
            }

            // Completion polling is scheduler overhead between kernels (src/execution/README.md).
            report.scheduling_overhead_ms = polling_ms_total;
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
         * @brief Runs a compiled graph on the persistent worker pool and waits.
         */
        void ExecutionEngine::execute(CompiledGraph& graph)
        {
            // execute() is submit() then complete(), nothing more (src/execution/README.md).
            submit(graph);
            complete();
        }

        /**
         * @brief Starts a run on the worker pool without waiting for it (src/execution/README.md).
         * @pre No other run is in progress on this engine.
         */
        void ExecutionEngine::submit(CompiledGraph& graph)
        {
            SR_THROW_IF(impl_->is_shutdown.load(std::memory_order_acquire), "Execute called after shutdown.");
            SR_THROW_IF(graph.roots.empty(), "Received a compiled graph with no root nodes.");

            // An overlapping run throws and leaves the owner's flag set (src/execution/README.md).
            SR_THROW_IF(impl_->run_in_progress.exchange(true, std::memory_order_acquire),
                        "ExecutionEngine::submit() is not reentrant: a graph run is "
                        "already in progress on this engine.");

            // The guard releases the flag only on an early exit (src/execution/README.md).
            struct SubmitGuard
            {
                std::atomic<bool>* flag;
                ~SubmitGuard() { if (flag) flag->store(false, std::memory_order_release); }
            } submit_guard{&impl_->run_in_progress};

            // DEBUG, not INFO: a run happens once per replayed step (src/execution/README.md).
            SR_LOG_DEBUG("Executing graph ({} root nodes).", graph.roots.size());

            // The sentinel's host_work is wired once per plan (src/execution/README.md).
            if (!graph.sentinel->host_work)
            {
                graph.sentinel->metadata.name = "graph_completion_sentinel";
                graph.sentinel->is_sentinel   = true;
                graph.sentinel->mode          = SushiRuntime::Scheduler::Task::ExecutionMode::HOST;

                auto* latch     = &impl_->completion;
                auto* sched_ptr = &impl_->scheduler;
                // Fixed for the sentinel's lifetime; a changed request rewires a fresh one.
                const bool profiling = graph.profiling;
                graph.sentinel->host_work =
                    [latch, sched_ptr, profiling]
                    (sycl::queue&, const std::vector<sycl::event>& deps) -> sycl::event
                    {
                        // Deps are waited, then the latch is signalled (src/execution/README.md).
                        const sycl::event no_event{};
                        for (auto e : deps)
                            if (e != no_event)
                                e.wait();

                        // The outcome is read before re-arming clears it (src/execution/README.md).
                        const bool failed = sched_ptr->run_failed();
                        std::exception_ptr err = failed ? sched_ptr->run_error() : nullptr;

                        // The pool stays armed through the drain tail (src/execution/README.md).
                        sched_ptr->arm_for_execution(profiling);

                        latch->signal(failed, std::move(err));
                        return {};
                    };
            }

            // The latch is reset before roots are submitted (src/execution/README.md).
            impl_->completion.reset();
            // Wired before arming; null when this plan does not request profiling.
            impl_->scheduler.set_profiling_ledger(graph.profiling ? &graph.profiling_ledger : nullptr);
            impl_->scheduler.arm_for_execution(graph.profiling);
            // The step counter advances here, not in arm_for_execution() (src/execution/README.md).
            impl_->scheduler.advance_dispatch_step();

            // One batched injector pass submits every root (src/execution/README.md).
            impl_->scheduler.submit_batch(graph.roots);

            // Handed off successfully: the run now owns the flag until complete().
            submit_guard.flag = nullptr;
        }

        /**
         * @brief Waits for the submitted run, drains the pool, releases the engine.
         *
         * The blocking half of a run. The flag is released before a task exception
         * is rethrown, so a failed run leaves the engine usable — the pool survives
         * a task throwing, and refusing every later run would turn one bad kernel
         * into a dead runtime.
         */
        void ExecutionEngine::complete()
        {
            struct ReleaseGuard
            {
                std::atomic<bool>& flag;
                ~ReleaseGuard() { flag.store(false, std::memory_order_release); }
            } release_guard{impl_->run_in_progress};

            impl_->completion.wait();

            // The pool drains before the caller may destroy the graph (src/execution/README.md).
            impl_->scheduler.wait_quiescent();

            // The pool is disarmed here, under the submission guard (src/execution/README.md).
            impl_->scheduler.disarm();

            if (impl_->completion.failed)
            {
                SR_LOG_ERROR("Graph execution aborted due to a task error.");
                std::rethrow_exception(impl_->completion.error);
            }

            SR_LOG_DEBUG("Graph execution completed."); // per-step; see the DEBUG note above
        }

        /**
         * @brief Whether the submitted run's sentinel has already reported in.
         *
         * Note that this answers "has the DAG finished", not "is it safe to destroy
         * the plan" — the pool still has bookkeeping to drain, which complete()
         * waits for. Use it to decide whether to keep overlapping work, never as a
         * substitute for complete().
         */
        bool ExecutionEngine::run_finished()
        {
            return impl_->completion.poll();
        }

        /**
         * @brief Executes a compiled graph, taking ownership of it (src/execution/README.md).
         * @param graph The compiled graph to execute via move semantics.
         */
        void IExecutionEngine::execute(CompiledGraph&& graph)
        {
            CompiledGraph local = std::move(graph);
            execute(local);
        }

    } // namespace Execution
} // namespace SushiRuntime
