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
        class SUSHIRUNTIME_API TaskScheduler 
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
        };

    } // namespace Scheduler
} // namespace SushiRuntime
