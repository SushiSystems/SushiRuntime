/**************************************************************************/
/* task_scheduler.hpp                                                     */
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
 * @file task_scheduler.hpp
 * @brief Declares TaskScheduler, the NUMA-aware work-stealing scheduler.
 * @author Mustafa Garip
 * @see modules/engine/scheduler/README.md
 */

#pragma once


#include <array>
#include <cstddef>
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
#include <SushiRuntime/scheduler/pin_policy.hpp>
#include <SushiRuntime/scheduler/worker_pool_settings.hpp>
#include <SushiRuntime/task/offload_sink.hpp>

namespace SushiRuntime
{
    /** @brief Forward declaration to allow friend access without full include. */
    namespace Graph
    {
        class TaskGraph;
        struct Node;
        class OffloadCoordinator;
    }

    namespace Topology { class TopologyView; }
    namespace Device { class DeviceQueueSet; }
    namespace Memory { class UsmAllocator; }

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
         * @brief Schedules tasks on a persistent, NUMA-aware work-stealing worker pool.
         * @see modules/engine/scheduler/README.md
         */
        class SUSHIRUNTIME_API TaskScheduler final : public ITelemetryProvider,
                                                     public ITaskMigrator,
                                                     public Graph::OffloadSink
        {
            public:
                /**
                 * @brief Returns the first core of the range workers are bound into, as applied.
                 * @return The first bound core index; meaningless when pinning() is Float.
                 * @see modules/engine/scheduler/README.md
                 */
                std::size_t first_core() const noexcept { return first_core_; }

                /**
                 * @brief Whether workers are bound to cores or left to the OS.
                 * @return The pin policy in effect for this scheduler's workers.
                 */
                PinPolicy pinning() const noexcept { return pinning_; }

            private:
                /** @brief Number of workers in the pool, fixed at construction. */
                size_t num_workers_ = 0;

                /** @brief List of queues for each worker thread. */
                std::array<sushi_ptr<Deque>, Core::MAX_WORKER_THREADS> worker_queues;
                /**
                 * @brief Holds each worker's scratch for the drain_pending_submissions batch.
                 * @see modules/engine/scheduler/README.md
                 */
                std::array<std::vector<Task*>, Core::MAX_WORKER_THREADS> drain_scratch_;
                /**
                 * @brief Implements a task FIFO that does not allocate in the steady state.
                 * @see modules/engine/scheduler/README.md
                 */
                class SubmissionQueue
                {
                    public:
                        bool empty() const noexcept { return head_ >= items_.size(); }
                        std::size_t size() const noexcept { return items_.size() - head_; }
                        Task* front() const noexcept { return items_[head_]; }
                        void push_back(Task* task) { items_.push_back(task); }

                        void pop_front() noexcept
                        {
                            ++head_;
                            if (head_ >= items_.size())
                            {
                                // Drained: rewind into the same buffer; clear() keeps capacity.
                                items_.clear();
                                head_ = 0;
                            }
                            else if (head_ >= COMPACT_AFTER && head_ * 2 >= items_.size())
                            {
                                // The consumed prefix is dropped by a move inside the same buffer.
                                items_.erase(items_.begin(),
                                             items_.begin() + static_cast<std::ptrdiff_t>(head_));
                                head_ = 0;
                            }
                        }

                    private:
                        static constexpr std::size_t COMPACT_AFTER = 1024;
                        std::vector<Task*> items_;
                        std::size_t        head_ = 0;
                };

                /** @brief Global injector queue for thread-safe external submissions. */
                SubmissionQueue pending_submissions;
                /** @brief Global priority queue for high-priority task bypass. */
                std::priority_queue<Task*, std::vector<Task*>, TaskPriorityCompare> priority_submissions;
                /** @brief Guards the injector queues. */
                std::mutex pending_mutex;
                std::condition_variable pending_cv;
                /**
                 * @brief Shadow the sizes of the two injector queues for lock-free reads.
                 * @see modules/engine/scheduler/README.md
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
                 * @brief Tells whether a task threw during the current run.
                 * @see modules/engine/scheduler/README.md
                 */
                std::atomic<bool> run_failed_{false};
                /** @brief Guards run_error_ on the cold failure path. */
                std::mutex run_error_mutex_;
                /** @brief First exception captured this run; delivered to the caller. */
                std::exception_ptr run_error_;
                /**
                 * @brief Tells whether the active run was cancelled from another thread.
                 * @see modules/engine/scheduler/README.md
                 */
                std::atomic<bool> run_cancelled_{false};
                /** @brief Poller to track SYCL events. */
                EventPolling event_poller;
                /** @brief Core-to-NUMA map and NUMA distances the steal order is built from. */
                const Topology::TopologyView& topology_;
                /** @brief Device queues every dispatch is submitted to. */
                Device::DeviceQueueSet& devices_;
                /** @brief Allocator asked which device owns a node's USM. */
                const Memory::UsmAllocator& memory_;
                /**
                 * @brief Offered each offloadable node at dispatch; null when none is installed.
                 * @see modules/engine/scheduler/README.md
                 */
                Graph::OffloadCoordinator* offload_ = nullptr;

                /**
                 * @brief Holds whether workers bind to cores, copied from WorkerPoolSettings.
                 * @see modules/engine/scheduler/README.md
                 */
                PinPolicy pinning_ = PinPolicy::Pin;

                /**
                 * @brief First core of the range workers bind into, worker i on
                 *        (first_core_ + i).
                 *
                 * Copied from WorkerPoolSettings and then clamped into the machine's
                 * core count, so this is the range actually in effect rather than the
                 * one requested. Meaningless under PinPolicy::Float.
                 */
                std::size_t first_core_ = 0;

                /** @brief Worker threads. */
                std::array<std::thread, Core::MAX_WORKER_THREADS> workers;
                /**
                 * @brief Holds one worker's completed-task counter on its own cache line.
                 * @see modules/engine/scheduler/README.md
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
                std::atomic<bool> profiling_active_{false};

                /** @brief The armed round's event ledger, or null when it does not profile. */
                std::atomic<Core::ProfilingLedger*> active_ledger_{nullptr};

                /** @brief Counter arm_for_execution() draws the next round's step from. */
                std::atomic<std::uint32_t> next_dispatch_step_{0};
                /** @brief This round's step, captured into each event a worker dispatches this round. */
                std::atomic<std::uint32_t> current_dispatch_step_{0};

                /**
                 * @brief Counts tasks submitted for the run but not yet reaped.
                 * @see modules/engine/scheduler/README.md
                 */
                std::atomic<long long> inflight_{0};
                /** @brief Guards the wait_quiescent() predicate against lost wakeups. */
                std::mutex quiesce_mutex_;
                /** @brief Signalled by the poller when inflight_ reaches zero. */
                std::condition_variable quiesce_cv_;

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
                /** @brief Runs one worker thread until stop(); @p thread_id is its worker index. */
                void dispatch_loop(int thread_id);
                /** @brief Records the first exception of the run and flips run_failed_. */
                void record_run_error(std::exception_ptr ep);
                /** @brief Looks for a task to steal from another thread's queue. */
                Task* find_task_to_steal(int th_id);
                /** @brief Moves pending submissions into the local worker queue. */
                void drain_pending_submissions(int thread_id);
                /** @brief Checks whether there are pending submissions left in the injector queue. */
                bool has_pending_submissions();

                /**
                 * @brief Picks which of a device's two queues a dispatch uses.
                 * @param index Device index in the queue set.
                 * @return The profiling queue when the round's plan requested
                 *         profiling, the plain queue otherwise.
                 */
                sycl::queue& dispatch_queue(std::size_t index);

                /**
                 * @brief Finds the device index behind an already-selected queue.
                 * @param q A plain queue of this scheduler's queue set.
                 * @return Its device index, or SIZE_MAX if none matches.
                 * @see modules/engine/scheduler/README.md
                 */
                std::size_t queue_device_index(sycl::queue& q) const;

            public:
                /**
                 * @brief Sizes the worker pool and builds its steal order; starts no thread.
                 * @param pool Copied; the range it asks for is clamped to the machine's cores.
                 * @param topology Must outlive the scheduler.
                 * @param devices Must outlive the scheduler.
                 * @param memory Must outlive the scheduler.
                 */
                TaskScheduler(const WorkerPoolSettings& pool,
                              const Topology::TopologyView& topology,
                              Device::DeviceQueueSet& devices,
                              const Memory::UsmAllocator& memory);
                /** @brief Cleans up the scheduler and worker queues. */
                ~TaskScheduler();

                /**
                 * @brief Starts the persistent background worker threads.
                 *
                 * Must be called exactly once after construction. Workers remain
                 * alive across multiple graph execution rounds.
                 */
                void start();

                /**
                 * @brief Arms the scheduler for a new execution round.
                 * @pre Called before the first root node of each round is submitted.
                 * @param profiling The round's plan flag; device nodes go to the profiling queue
                 *        while set.
                 * @see modules/engine/scheduler/README.md
                 */
                void arm_for_execution(bool profiling = false);

                /**
                 * @brief Wires the plan's event ledger for the round about to be armed.
                 * @pre The CompiledGraph that owns the ledger outlives the window up to disarm().
                 * @param ledger The plan's ledger, or null when it does not profile.
                 * @see modules/engine/scheduler/README.md
                 */
                void set_profiling_ledger(Core::ProfilingLedger* ledger) noexcept;

                /** @brief Resets the per-run step counter, to 0. */
                void reset_dispatch_step() noexcept { next_dispatch_step_.store(0, std::memory_order_release); }

                /** @brief Draws this round's step; call exactly once per root-injecting submit(), not per arm. */
                void advance_dispatch_step() noexcept
                {
                    current_dispatch_step_.store(
                        next_dispatch_step_.fetch_add(1, std::memory_order_acq_rel),
                        std::memory_order_release);
                }

                /** @brief The step this round's dispatches must stamp into their event records. */
                std::uint32_t current_dispatch_step() const noexcept
                {
                    return current_dispatch_step_.load(std::memory_order_acquire);
                }

                /**
                 * @brief Returns the pool to its parked state after a run quiesces.
                 * @pre The run has quiesced; ExecutionEngine::complete() calls this after
                 *        wait_quiescent().
                 * @see modules/engine/scheduler/README.md
                 */
                void disarm() noexcept;

                /**
                 * @brief Signals workers to stop and waits for all threads to join.
                 *
                 * Called once when the owning engine is being destroyed. After this
                 * returns, no worker thread touches the queue set or the allocator,
                 * so both can be freed safely.
                 */
                void stop();

                /** @brief Submits a new task to the injector queue. */
                void submit(Task* task);

                /**
                 * @brief Submits a batch of root nodes in a single injector pass.
                 * @see modules/engine/scheduler/README.md
                 */
                void submit_batch(const std::vector<SushiRuntime::Graph::Node*>& roots);

                /**
                 * @brief Blocks until every task submitted for the current run has been reaped.
                 * @see modules/engine/scheduler/README.md
                 */
                void wait_quiescent();

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
                void request_cancellation() noexcept { run_cancelled_.store(true, std::memory_order_release); }

                /**
                 * @brief Reports whether the active run has been cancelled.
                 * @return True if request_cancellation() was called and not yet reset.
                 */
                bool run_cancelled() const { return run_cancelled_.load(std::memory_order_acquire); }

                /** @brief Clears the cancellation flag at the start of a fresh run. */
                void reset_cancellation() { run_cancelled_.store(false, std::memory_order_release); }

                size_t get_worker_count() const override;
                size_t get_queue_size(int worker_id) const override;
                size_t completed_task_count(int worker_id) const override;

                void migrate_tasks(int from_worker_id, size_t count) override;
                using ITaskMigrator::migrate_tasks;

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

                /**
                 * @brief Installs the coordinator offloadable nodes are offered to, and binds it
                 *        to this scheduler.
                 * @pre The pool runs no node.
                 * @param offload Not owned; null removes the installed one.
                 * @see modules/engine/scheduler/README.md
                 */
                void set_offload_coordinator(Graph::OffloadCoordinator* offload);

                /**
                 * @brief Queues a node no worker finished and drops the coordinator's reference.
                 * @param node Accepted by the coordinator's try_offload().
                 */
                void resubmit(Graph::Node& node) override;

                /**
                 * @brief Releases a remotely finished node's successors, then drops the
                 *        coordinator's reference.
                 * @param node Accepted by the coordinator's try_offload().
                 * @see modules/engine/scheduler/README.md
                 */
                void complete_remote(Graph::Node& node) override;
        };

    } // namespace Scheduler
} // namespace SushiRuntime
