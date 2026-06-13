/**************************************************************************/
/* task_scheduler.hpp                                                     */
/**************************************************************************/
/*                          This file is part of:                         */
/*                              SushiRuntime                              */
/*              https://github.com/SushiSystems/SushiRuntime              */
/*                        https://sushisystems.io                         */
/**************************************************************************/
/* Copyright (c) 2026-present Mustafa Garip & Sushi Systems               */
/* All Rights Reserved.                                                   */
/*                                                                        */
/* CONFIDENTIAL: This software is the proprietary information of          */
/* Mustafa Garip & Sushi Systems. Unauthorized copying of this file,      */
/* via any medium is strictly prohibited.                                 */
/*                                                                        */
/* This source code and the intellectual property contained herein        */
/* is confidential and may not be disclosed, copied, or used without      */
/* explicit written permission from the copyright holders.                */
/**************************************************************************/

#pragma once


#include <array>
#include <deque>
#include <mutex>
#include <atomic>
#include <thread>
#include <vector>
#include <queue>
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
    }

    namespace Scheduler 
    {
        class Deque;
        class Task;

        /** @brief Comparator for global priority queue. */
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
         * @brief Work-stealing scheduler loop.
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
                 * @brief Controls the worker loop.
                 *
                 * Workers exit their loop when this is false AND no inflight events
                 * or pending submissions remain. Use arm_for_execution() to reset it
                 * for a new execution round without recreating threads.
                 */
                std::atomic<bool> running{false};
                /** @brief Signals that the pool should drain and shut down. */
                std::atomic<bool> shutdown_requested{false};
                /** @brief Poller to track SYCL events. */
                EventPolling event_poller;
                /** @brief Reference to the runtime context. */
                SushiRuntime::Execution::RuntimeContext& context;
                /** @brief Worker threads. */
                std::array<std::thread, Core::MAX_WORKER_THREADS> workers;
                /** @brief Callback for error handling. */
                std::function<void(std::exception_ptr)> error_callback;
                /** @brief Cache-line aligned atomic counters for tracking completed tasks per worker. */
                alignas(Core::CACHE_LINE_SIZE) std::atomic<size_t> worker_completed_tasks[Core::MAX_WORKER_THREADS];

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

                /** @brief Executes a single task and tracks its events. */
                void execute_task(Task* task, EventPolling::ActiveEventList& events);
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
                 * Resets the running flag to true so that workers continue their
                 * dispatch loops for the upcoming graph. Must be called before
                 * submitting the first root node of each execution round.
                 *
                 * This is the Madde 7 fix: no thread creation/destruction between runs.
                 */
                void arm_for_execution();

                /**
                 * @brief Signals workers to stop and waits for all threads to join.
                 *
                 * Called once when the RuntimeContext (and thus the ExecutionEngine)
                 * is being destroyed. Ensures no dangling context references exist
                 * after this call returns. [Madde 18 fix]
                 */
                void stop();

                /** @brief Submits a new task to the injector queue. */
                void submit(Task* task);

                /** @brief Sets the error callback. */
                void set_error_callback(std::function<void(std::exception_ptr)> cb) 
                { error_callback = cb; }

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
        };

    } // namespace Scheduler
} // namespace SushiRuntime
