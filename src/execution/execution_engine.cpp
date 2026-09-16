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
         * @brief A reusable completion gate that replaces the per-step promise.
         *
         * execute() used to allocate a fresh std::promise/std::future pair (and a
         * capturing sentinel closure) on every step of a replayed run — millions of
         * heap allocations and a thread hand-off per million-step run, pure jitter on
         * a real-time frame. This latch lives once on the engine and is reset, armed,
         * and waited on each step with no allocation: the sentinel publishes the
         * outcome under the mutex and notifies; the caller waits on the predicate.
         *
         * A condition_variable (not C++20 atomic::wait/a binary_semaphore, which the
         * C++17 baseline lacks) is the portable allocation-free primitive here; the
         * mutex is uncontended in steady state, so the cost is a futex wait, not a
         * malloc. The failure carried alongside is published before done under the
         * same lock, so a waiter that observes done sees a fully-written outcome.
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
         * @brief Private implementation of ExecutionEngine (Pimpl).
         *
         * Owns a single persistent TaskScheduler. The scheduler is started
         * once in the constructor and stopped exactly once in shutdown().
         *
         * The scheduler is persistent rather than re-created per graph execution,
         * and RuntimeContext::Impl's destructor orders shutdown explicitly, so the
         * scheduler is always stopped before the queues it dispatches onto are torn
         * down.
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

                // The coordinator is always constructed so set_rebalancer_enabled()
                // can start it later without a second construction path; whether its
                // heartbeat thread runs is the settings' call. Off by default: the
                // 5 ms migration heartbeat is a jitter and reproducibility cost that
                // only a throughput workload is buying something with.
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

            // Aggregate node_timings from the records above (F4): one row per
            // registration_index actually dispatched, indexed by that index
            // directly rather than a hashed lookup.
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

            // Completion polling is the scheduler's own overhead between kernels;
            // surface it in the long-reserved field so a frame can be tuned against it.
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
         *
         * Responsibility split: TaskGraph::compile() builds the DAG and the root
         * list; the engine owns thread orchestration, the sentinel node setup,
         * completion signalling, and graph cleanup. Those are divided again between
         * submit() (start) and complete() (wait), and this composes the two.
         *
         * The persistent scheduler is "re-armed" for each execution round via
         * arm_for_execution() instead of being destroyed and recreated, and
         * completion flows through one reusable CompletionLatch on the engine
         * rather than a promise/future allocated per step.
         */
        void ExecutionEngine::execute(CompiledGraph& graph)
        {
            // execute() IS submit() + complete(). Keeping it as their composition
            // rather than a third copy of the sequence means the synchronous and
            // asynchronous paths can never drift apart — the ordering rules below
            // (reset before submit, quiesce before returning) hold for both.
            submit(graph);
            complete();
        }

        /**
         * @brief Starts a run on the worker pool without waiting for it.
         *
         * Everything execute() used to do before blocking. The non-reentrancy flag
         * is taken here and released by complete(), not by an RAII guard scoped to
         * this call — the run outlives the function, which is the whole point. Any
         * failure after the flag is taken releases it before propagating, so a
         * throwing submit does not leave the engine permanently closed.
         */
        void ExecutionEngine::submit(CompiledGraph& graph)
        {
            SR_THROW_IF(impl_->is_shutdown.load(std::memory_order_acquire), "Execute called after shutdown.");
            SR_THROW_IF(graph.roots.empty(), "Received a compiled graph with no root nodes.");

            // Reject a concurrent run before touching any shared run state. If the
            // exchange returns true another run owns the engine, so we throw WITHOUT
            // releasing the flag — that run still owns it.
            SR_THROW_IF(impl_->run_in_progress.exchange(true, std::memory_order_acquire),
                        "ExecutionEngine::submit() is not reentrant: a graph run is "
                        "already in progress on this engine.");

            // Releases the flag only if this function exits early (an exception
            // while wiring or submitting). On the success path it is disarmed and
            // ownership passes to complete().
            struct SubmitGuard
            {
                std::atomic<bool>* flag;
                ~SubmitGuard() { if (flag) flag->store(false, std::memory_order_release); }
            } submit_guard{&impl_->run_in_progress};

            // DEBUG, not INFO: a run happens once per replayed step, so under a
            // million-step run(n) an INFO here would emit millions of log records
            // (ostringstream + timestamp + global mutex + condvar notify each),
            // defeating the allocation-free CompletionLatch. Compiled out in
            // release (ACTIVE_LOG_LEVEL = INFO there).
            SR_LOG_DEBUG("Executing graph ({} root nodes).", graph.roots.size());

            // Wire the sentinel's host_work once. It captures only stable pointers
            // — the engine's latch and scheduler — so the closure is built the first
            // time this plan runs and reused for every later step, instead of being
            // reassigned each call. A recompile produces a fresh sentinel (empty
            // host_work), which re-wires.
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
                        // The sentinel is the final node: the DAG only dispatches it
                        // once every predecessor's event has completed. Signal
                        // completion directly in C++ rather than through a SYCL host
                        // operation — AdaptiveCpp does not run a custom-operation
                        // callback until its event is waited on, so a host-task-based
                        // sentinel would never fire and execute() would block forever.
                        // Wait on the predecessor events first so any device work is
                        // genuinely flushed (this also drives AdaptiveCpp's queue to
                        // completion) before we report done. Copy each event:
                        // AdaptiveCpp's event::wait() is non-const and deps is const.
                        // The comparison target is built once, not once per
                        // dependency. sycl::event{} allocates on oneAPI's libsycl,
                        // so constructing it inside the loop cost one allocation
                        // per predecessor per run -- the same defect this file's
                        // execute() had, on a worker thread where the steady-state
                        // counter could not see it.
                        const sycl::event no_event{};
                        for (auto e : deps)
                            if (e != no_event)
                                e.wait();

                        // Read the run outcome before re-arming: arm_for_execution()
                        // clears run_failed_ for the next round, so capture it first.
                        // A failed run still drains every node to this sentinel, so
                        // reaching here means the run has quiesced either way.
                        const bool failed = sched_ptr->run_failed();
                        std::exception_ptr err = failed ? sched_ptr->run_error() : nullptr;

                        // Keeps the pool armed through the drain tail — peers are
                        // still finishing poll bookkeeping for this run's last
                        // nodes and must not park mid-drain. complete() disarms
                        // once that has quiesced, and the next submit() arms
                        // again, so this is not the next round's arming.
                        sched_ptr->arm_for_execution(profiling);

                        latch->signal(failed, std::move(err));
                        return {};
                    };
            }

            // Reset the latch, arm the scheduler, then submit roots. The reset must
            // precede submission so a waiter cannot observe the previous step's
            // completion. The caller's wait() returned only after the previous
            // sentinel signalled, i.e. after that step fully drained, so resetting
            // here races nothing.
            impl_->completion.reset();
            // Wired before arming; null when this plan does not request profiling.
            impl_->scheduler.set_profiling_ledger(graph.profiling ? &graph.profiling_ledger : nullptr);
            impl_->scheduler.arm_for_execution(graph.profiling);

            // One batched injector pass instead of one submit() (lock + notify) per
            // root — the per-step root-injection cost no longer scales with fan-out.
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

            // The sentinel signals the latch from its host_work, which runs on a
            // worker thread before that worker (and its peers) finish the poll
            // bookkeeping for the run's last nodes. Wait for the pool to fully drain
            // before returning: the caller may destroy this CompiledGraph the instant
            // this returns, and a worker still inside trigger_successors/release
            // would then touch freed nodes.
            impl_->scheduler.wait_quiescent();

            // The run is over and every task has been reaped, so the pool can go
            // back to sleep. This is the only point where that is safe — the idle
            // wait predicate does not look at the per-worker deques, and quiescence
            // is what guarantees they are empty. Doing it here rather than leaving
            // the pool armed is what makes an idle runtime free: the sentinel
            // re-arms for a next round that may never come, and until this call
            // existed that left every worker spinning through the steal backoff for
            // the rest of the context's life.
            //
            // The submission guard above is still held, so no other thread can
            // start a run between this call and the guard's release; the next
            // submit() arms again before it injects anything. That exclusion is
            // why the disarm lives here rather than once per user-visible run:
            // parking from anywhere the engine does not hold the guard could
            // disarm a pool that another thread had just armed, and a worker
            // holding queued tasks with no active events would then park on them.
            //
            // A multi-step run disarms and re-arms per step, which costs almost
            // nothing in practice: the next arm follows within microseconds, and a
            // worker only parks if it reaches the top of its loop inside that
            // window. Workers that do park are woken by the next arm's notify. The
            // effect is self-tuning — a tight batch keeps running, an idle runtime
            // goes to sleep — with no timer to choose a threshold for.
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
         */
        void IExecutionEngine::execute(CompiledGraph&& graph)
        {
            CompiledGraph local = std::move(graph);
            execute(local);
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
            for (std::size_t i = 0; i < all_nodes.size(); ++i)
            {
                auto& n = all_nodes[i];
                // Stamp the node's stable plan identity here, the one pass that
                // already walks every node once after compile.
                n->plan_index = i;
                base_in_degree.push_back(n->in_degree.load(std::memory_order_relaxed));
            }
            sentinel_in_degree = sentinel
                ? sentinel->in_degree.load(std::memory_order_relaxed) : 0;

            // Computed once here (F5), not rebuilt by reset_profiling()/collect_profiling() each run.
            std::uint32_t max_index = 0;
            for (auto& n : all_nodes)
                if (!n->is_sentinel) max_index = std::max(max_index, n->registration_index);
            const std::size_t index_count = all_nodes.empty() ? 0 : static_cast<std::size_t>(max_index) + 1;

            profiling_multiplicities.assign(index_count, 0);
            profiling_host_native.assign(index_count, false);
            for (auto& n : all_nodes)
            {
                if (n->is_sentinel) continue;
                ++profiling_multiplicities[n->registration_index];
                if (n->get_execution_mode() == SushiRuntime::Scheduler::Task::ExecutionMode::HOST &&
                    !n->submits_device_work())
                    profiling_host_native[n->registration_index] = true;
            }
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
                if (auto* off = n->offload_state())
                    off->attempts.store(0, std::memory_order_relaxed);
#endif
        }

    } // namespace Execution
} // namespace SushiRuntime
