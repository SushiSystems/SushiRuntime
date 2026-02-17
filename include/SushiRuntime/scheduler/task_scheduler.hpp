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

#include <atomic>
#include <memory>
#include <array>
#include <thread>
#include <vector>
#include <sycl/sycl.hpp>
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

        /**
         * @brief Work-stealing scheduler loop.
         */
        class SUSHIRUNTIME_API TaskScheduler 
        {
            friend class SushiRuntime::Graph::TaskGraph;

            private:
                /** @brief Actual number of active workers. */
                size_t num_workers = 0;
                /** @brief List of queues for each worker thread. */
                std::array<sushi_ptr<Deque>, Core::MAX_WORKER_THREADS> worker_queues;
                /** @brief Flag to control the execution of the loop. */
                std::atomic<bool> running{true};
                /** @brief Poller to track SYCL events. */
                EventPolling event_poller;
                /** @brief Reference to the runtime context. */
                SushiRuntime::Execution::RuntimeContext& context;
                /** @brief Worker threads. */
                std::array<std::thread, Core::MAX_WORKER_THREADS> workers;

                /** @brief Executes a single task and tracks its events. */
                void execute_task(void* task_ptr, std::vector<EventPolling::PolledEvent>& events);
                /** @brief Looks for a task to steal from another thread's queue. */
                void* find_task_to_steal(int th_id);

            public:
                /** @brief Creates a new task scheduler. */
                TaskScheduler(SushiRuntime::Execution::RuntimeContext& ctx);
                /** @brief Cleans up the scheduler and worker queues. */
                ~TaskScheduler();

                /** @brief Main loop for worker threads. */
                void dispatch_loop(int thread_id);
                /** @brief Submits a new task to the scheduler. */
                void submit(void* task);
        };

    } // namespace Scheduler
} // namespace SushiRuntime
