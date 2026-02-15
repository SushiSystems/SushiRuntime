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
#include <vector>
#include <memory>
#include <atomic>
#include <sycl/sycl.hpp>
#include "event_polling.hpp"
#include "../execution/runtime_context.hpp"

namespace SushiRuntime 
{
    namespace Scheduler 
    {
        class Deque;

        /**
         * @brief work-stealing scheduler loop.
         */
        class TaskScheduler 
        {
            friend class SushiRuntime::Graph::TaskGraph;

            private:
                /** @brief list of queues for each worker thread. */
                std::vector<sushi_ptr<Deque>> worker_queues;
                /** @brief flag to control the execution of the loop. */
                std::atomic<bool> running{true};
                /** @brief poller to track sycl events. */
                EventPolling event_poller;
                /** @brief reference to the runtime context. */
                SushiRuntime::Execution::RuntimeContext& context;

                /** @brief executes a single task and tracks its events. */
                void execute_task(void* task_ptr, std::vector<EventPolling::PolledEvent>& events);
                /** @brief looks for a task to steal from another thread's queue. */
                void* find_task_to_steal(int th_id);

            public:
                /** @brief creates a new task scheduler. */
                TaskScheduler(SushiRuntime::Execution::RuntimeContext& ctx);
                /** @brief cleans up the scheduler and worker queues. */
                ~TaskScheduler();

                /** @brief main loop for worker threads. */
                void dispatch_loop(int thread_id);
                /** @brief submits a new task to the scheduler. */
                void submit(void* task);
        };

    } // namespace Scheduler
} // namespace SushiRuntime
