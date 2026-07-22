/**************************************************************************/
/* task_scheduler.hpp                                                     */
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

#pragma once


#include <array>
#include <cstdint>
#include <deque>
#include <mutex>
#include <atomic>
#include <thread>
#include <vector>
#include <queue>
#include <exception>
#include <sycl/sycl.hpp>
#include <condition_variable>
#include <SushiRuntime/core/common.hpp>
#include <SushiRuntime/core/export.hpp>
#include <SushiRuntime/scheduler/rebalancer_interfaces.hpp>
#include <SushiRuntime/scheduler/event_polling.hpp>
#include <SushiRuntime/execution/runtime_context.hpp>

namespace SushiRuntime
{
    /** @brief Forward declaration to allow friend access without full include. */
    namespace Graph
    {
        class TaskGraph;
        struct Node;
    }

#ifdef SR_DISTRIBUTED
    namespace Distributed { class DistributedCoordinator; }
#endif

    namespace Scheduler 
    {
        class Deque;
        class Task;

        /**
         * @brief Min-heap comparator for the global priority queue.
         *
         * Returns true when @p a should yield to @p b, i.e. when a's priority is
         * lower. Null pointers sort to the bottom (lowest priority) to handle any
         * sentinel entries without a null-check in the dispatch loop.
         */
        struct TaskPriorityCompare
        {
            bool operator()(const Task* a, const Task* b) const
            {
                if (!a) return true;
                if (!b) return false;
                return a->get_priority() < b->get_priority();
            }
        };

        /**
         * @brief NUMA-aware work-stealing scheduler.
         *
         * Owns a fixed-size pool of worker threads started once (start()) and kept
         * alive across graph runs. Each run is gated by arm_for_execution() +
         * submit(root) pairs; the sentinel node's completion promise unblocks the
         * caller. Workers steal from node-local victims first; cross-node steals are
         * tracked separately so the RunReport can report intra/inter-NUMA balance.
         */
        class SUSHIRUNTIME_API TaskScheduler : public ITelemetryProvider, public ITaskMigrator
        {
            friend class SushiRuntime::Graph::TaskGraph;

            public:
                /** @brief Actual number of active workers. Read-only externally. */
                size_t num_workers = 0;

            private:
                /** @brief List of queues for each worker thread. */
                std::array<sushi_ptr<Deque>, Core::MAX_WORKER_THREADS> worker_queues;
                /** @brief Global injector queue for thread-safe external submissions. */
                std::deque<Task*> pending_submissions;
                /** @brief Global priority queue for high-priority task bypass. */
                std::priority_queue<Task*, std::vector<Task*>, TaskPriorityCompare> priority_submissions;
                /** @brief Guards the injector queues. */
                std::mutex pending_mutex;
                std::condition_variable pending_cv;
                /**
                 * @brief Lock-free shadow sizes of the two injector queues.
                 *
                 * Every worker checks the injectors on every dispatch iteration, but
                 * the common steady state is "both empty" — priority bypass is rare
                 * and external submissions arrive in bursts. Taking pending_mutex
                 * unconditionally turned that check into a single global cache line
                 * ping-ponging across all cores, throttling submit throughput.
                 * These counters shadow the container sizes and are mutated only
                 * under pending_mutex, so the hot path can read them relaxed and skip
                 * the lock entirely when they are zero. A stale zero is harmless: the
                 * enqueuer always notify()s and the loop re-checks; a stale non-zero
                 * just takes the lock and finds nothing (rare).
                 */
                std::atomic<size_t> pending_count_{0};
                std::atomic<size_t> priority_count_{0};
                /**
                 * @brief Controls the worker loop.
                 *
                 * Workers exit their loop when this is false AND no inflight events
                 * or pending submissions remain. Use arm_for_execution() to reset it
                 * for a new execution round without recreating threads.
                 */
                std::atomic<bool> running{false};
                /** @brief Signals that the pool should drain and shut down. */
                std::atomic<bool> shutdown_requested{false};
                /** @brief Guards start() so worker threads are spawned exactly once;
                 *  a second call would overwrite live std::thread handles (terminate)
                 *  and break the Chase-Lev single-owner-per-deque invariant. */
                std::atomic<bool> threads_started_{false};
                /**
                 * @brief Set when a task throws during the current run.
                 *
                 * Aborts only the current run — remaining tasks drain without
                 * executing so the DAG still reaches the sentinel — while leaving
                 * the worker pool alive and re-armable. Distinct from
                 * shutdown_requested, which permanently tears the pool down.
                 * Cleared by arm_for_execution() at the start of each round.
                 */
                std::atomic<bool> run_failed_{false};
                /** @brief Guards run_error_ on the cold failure path. */
                std::mutex run_error_mutex_;
                /** @brief First exception captured this run; delivered to the caller. */
                std::exception_ptr run_error_;
                /**
                 * @brief Set when the active run is cancelled from another thread.
                 *
                 * Like run_failed_ it makes remaining tasks drain without executing
                 * (the sentinel still runs), but the run completes normally rather
                 * than throwing — the drive loop sees it and stops. Unlike
                 * run_failed_ it is NOT cleared per step by arm_for_execution(): a
                 * cancellation aborts the whole multi-step run, so it is cleared only
                 * by reset_cancellation() at the start of a fresh run.
                 */
                std::atomic<bool> run_cancelled_{false};
                /** @brief Poller to track SYCL events. */
                EventPolling event_poller;
                /** @brief Reference to the runtime context. */
                SushiRuntime::Execution::RuntimeContext& context;
                /** @brief Worker threads. */
                std::array<std::thread, Core::MAX_WORKER_THREADS> workers;
                /**
                 * @brief Per-worker completed-task counter, each on its own cache line.
                 *
                 * The previous `alignas(CACHE_LINE_SIZE) atomic<size_t>[]` aligned only
                 * the array's start, so eight 8-byte counters shared one cache line and
                 * false-shared under contention; the elements were also default-
                 * constructed with an indeterminate value in C++17, feeding the thermal
                 * strategy garbage rates. Wrapping each counter in a cache-line-aligned,
                 * zero-initialised struct (mirroring WorkerTimeBuckets) fixes both.
                 */
                struct alignas(Core::CACHE_LINE_SIZE) WorkerCompletedCounter
                {
                    std::atomic<size_t> value{0};
                };
                WorkerCompletedCounter worker_completed_tasks[Core::MAX_WORKER_THREADS];

                /**
                 * @brief Per-worker accumulated time buckets (nanoseconds).
                 *
                 * Each element is its own cache line, so the worker that owns an index
                 * is the only writer and there is no false sharing. Relaxed atomics:
                 * a worker accumulates only its own bucket and the engine reads them at
                 * a run boundary, with no cross-worker ordering requirement.
                 */
                struct alignas(Core::CACHE_LINE_SIZE) WorkerTimeBuckets
                {
                    std::atomic<std::uint64_t> busy_ns{0};
                    std::atomic<std::uint64_t> stealing_ns{0};
                    std::atomic<std::uint64_t> polling_ns{0};
                    std::atomic<std::uint64_t> idle_ns{0};
                };
                WorkerTimeBuckets worker_time_[Core::MAX_WORKER_THREADS];

                /** @brief Whether the active run accumulates per-worker time buckets. */
                bool profiling_active_ = false;

                // --- NUMA-aware hierarchical stealing (built once at construction) ---
                /** @brief NUMA node of each worker (= node of the core it is pinned to). */
                std::vector<int> worker_numa_;
                /** @brief Per-worker steal victim ids ordered by ascending NUMA distance. */
                std::vector<std::vector<int>> victim_order_;
                /** @brief Number of leading entries in victim_order_[i] that are node-local. */
                std::vector<int> local_tier_len_;

                /** @brief Precomputes worker_numa_, victim_order_ and local_tier_len_. */
                void build_steal_topology();

                /** @brief Successful steals from a node-local victim. */
                std::atomic<size_t> local_steals_{0};
                /** @brief Successful steals from a remote-node victim. */
                std::atomic<size_t> remote_steals_{0};

                /** @brief Executes a single task and tracks its events.
                 *  @param worker_id The id of the executing worker (for per-worker
                 *         completion accounting — must be the scheduler's worker id,
                 *         not the EBR slot id). */
                void execute_task(Task* task, int worker_id, EventPolling::ActiveEventList& events);
                /** @brief Records the first exception of the run and flips run_failed_. */
                void record_run_error(std::exception_ptr ep);
                /** @brief Looks for a task to steal from another thread's queue. */
                Task* find_task_to_steal(int th_id);
                /** @brief Moves pending submissions into the local worker queue. */
                void drain_pending_submissions(int thread_id);
                /** @brief Checks whether there are pending submissions left in the injector queue. */
                bool has_pending_submissions();

            public:
                /** @brief Creates a new task scheduler. */
                TaskScheduler(SushiRuntime::Execution::RuntimeContext& ctx);
                /** @brief Cleans up the scheduler and worker queues. */
                ~TaskScheduler();

                /** @brief Main loop for worker threads. */
                void dispatch_loop(int thread_id);

                /**
                 * @brief Starts the persistent background worker threads.
                 *
                 * Must be called exactly once after construction. Workers remain
                 * alive across multiple graph execution rounds.
                 */
                void start();

                /**
                 * @brief Arms the scheduler for a new execution round.
                 *
                 * Resets the running flag so workers continue their dispatch loops
                 * for the upcoming graph. Must be called before submitting the first
                 * root node of each round. Avoids thread creation/destruction between
                 * successive graph runs by reusing the persistent worker pool.
                 */
                void arm_for_execution();

                /**
                 * @brief Signals workers to stop and waits for all threads to join.
                 *
                 * Called once when the RuntimeContext (and thus the ExecutionEngine)
                 * is being destroyed. After this returns, no worker thread holds a
                 * reference to the context, so queues and allocators can be freed safely.
                 */
                void stop();

                /** @brief Submits a new task to the injector queue. */
                void submit(Task* task);

                /**
                 * @brief Reports whether a task threw during the current run.
                 *
                 * Read by the completion sentinel to deliver the error to the caller
                 * instead of success. Cleared each round by arm_for_execution().
                 * @return True if the current run has failed.
                 */
                bool run_failed() const { return run_failed_.load(std::memory_order_acquire); }

                /**
                 * @brief Returns the first exception captured during the current run.
                 *
                 * Synchronizes with the failing worker via run_error_mutex_, so the
                 * value is visible once run_failed() reads true.
                 * @return The captured exception, or null if the run did not fail.
                 */
                std::exception_ptr run_error();

                /**
                 * @brief Requests cancellation of the active run. Thread-safe.
                 *
                 * Remaining tasks drain without executing and the run completes
                 * (the sentinel still fires); the driver stops launching new steps.
                 */
                void request_cancellation() { run_cancelled_.store(true, std::memory_order_release); }

                /**
                 * @brief Reports whether the active run has been cancelled.
                 * @return True if request_cancellation() was called and not yet reset.
                 */
                bool run_cancelled() const { return run_cancelled_.load(std::memory_order_acquire); }

                /** @brief Clears the cancellation flag at the start of a fresh run. */
                void reset_cancellation() { run_cancelled_.store(false, std::memory_order_release); }

                size_t get_worker_count() const override;
                size_t get_queue_size(int worker_id) const override;
                size_t get_completion_rate(int worker_id) const override;

                void migrate_tasks(int from_worker_id, int to_worker_id, size_t count) override;

                /**
                 * @brief Introspection hook (tests): the distance-ordered steal
                 * victim list for a worker. Leading entries are node-local.
                 * @return Victim worker ids in the order find_task_to_steal() walks.
                 */
                std::vector<int> steal_victim_order(int worker_id) const;

                /** @brief Count of successful steals from a node-local victim. */
                size_t get_local_steal_count() const  { return local_steals_.load(std::memory_order_relaxed); }
                /** @brief Count of successful steals that had to cross to a remote node. */
                size_t get_remote_steal_count() const { return remote_steals_.load(std::memory_order_relaxed); }

                /**
                 * @brief One worker's accumulated time split, in nanoseconds.
                 *
                 * The stall-attribution signal: how a worker spent a run across real
                 * work, stealing, completion polling, and idle backoff. Only populated
                 * when the run was armed for profiling.
                 */
                struct WorkerTimeSplit
                {
                    std::uint64_t busy_ns = 0;     /**< Inside task execution. */
                    std::uint64_t stealing_ns = 0; /**< Searching victims for work. */
                    std::uint64_t polling_ns = 0;  /**< Polling in-flight events. */
                    std::uint64_t idle_ns = 0;     /**< Backing off or waiting to be armed. */
                };

                /**
                 * @brief Returns the accumulated time split for worker @p worker_id.
                 * @param worker_id Worker index in [0, get_worker_count()).
                 * @return The worker's time split; zeroed when out of range.
                 */
                WorkerTimeSplit get_worker_times(int worker_id) const noexcept;

                /**
                 * @brief Zeroes every worker's time split.
                 *
                 * Called by the engine before a profiled run so the report covers only
                 * that run, mirroring the before/after handling of the steal counters.
                 */
                void reset_worker_times() noexcept;

#ifdef SR_DISTRIBUTED
                /**
                 * @brief Attaches the master-side coordinator (Master mode only).
                 *
                 * Null in Standalone/Worker mode → the dispatch loop never takes the
                 * offload branch, so the local hot path is unchanged.
                 */
                void set_coordinator(Distributed::DistributedCoordinator* c);

                /**
                 * @brief Releases an offloaded node's successors once its remote
                 * result arrives — the network equivalent of EventPolling firing.
                 *
                 * Installed as the coordinator's completion hook. Uses submit() so
                 * it is safe to invoke from the transport thread.
                 */
                void complete_offloaded_node(Graph::Node* node, bool success);
#endif

            private:
#ifdef SR_DISTRIBUTED
                /** @brief Master-side offload brain; null unless in Master mode. */
                Distributed::DistributedCoordinator* coordinator_ = nullptr;
#endif
        };

    } // namespace Scheduler
} // namespace SushiRuntime
