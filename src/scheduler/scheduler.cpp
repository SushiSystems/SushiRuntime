/**************************************************************************/
/* scheduler.cpp                                                          */
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

#include <new>
#include <thread>
#include <optional>
#include <SushiRuntime/core/common.hpp>
#include <SushiRuntime/core/logger.hpp>
#include <SushiRuntime/scheduler/task.hpp>
#include <SushiRuntime/scheduler/task_scheduler.hpp>
#include <SushiRuntime/execution/runtime_context.hpp>

namespace SushiRuntime
{
    namespace Scheduler
    {
        using namespace SushiRuntime::Execution;
        using namespace SushiRuntime::Core;

        /**
         * @brief Lock-free Chase-Lev deque for task scheduling.
         * 
         * The owner thread pushes and pops from the bottom (LIFO).
         * Stealer threads steal from the top (FIFO).
         */
        class alignas(SushiRuntime::Core::CACHE_LINE_SIZE) Deque : public Core::RefCounted 
        {
        private:
            static constexpr int64_t INITIAL_CAPACITY = 65536;
            static constexpr int64_t MASK = INITIAL_CAPACITY - 1;

            alignas(64) std::atomic<void**> array;
            alignas(64) std::atomic<int64_t> top;
            alignas(64) std::atomic<int64_t> bottom;

        public:
            Deque() : top(0), bottom(0) 
            {
                array.store(new void*[INITIAL_CAPACITY], std::memory_order_relaxed);
            }

            ~Deque() 
            {
                delete[] array.load(std::memory_order_relaxed);
            }

            /** @brief Pushes a task into the deque. */
            void push(void* task) 
            {
                int64_t b = bottom.load(std::memory_order_relaxed);
                int64_t t = top.load(std::memory_order_acquire);
                
                if (b - t >= INITIAL_CAPACITY) 
                {
                    SR_THROW_IF(true, "HPC deque overflow. Capacity: {}", INITIAL_CAPACITY);
                }

                array.load(std::memory_order_relaxed)[b & MASK] = task;
                std::atomic_thread_fence(std::memory_order_release);
                bottom.store(b + 1, std::memory_order_relaxed);
            }

            /** @brief Removes and returns a task from the bottom. */
            void* pop() 
            {
                int64_t b = bottom.load(std::memory_order_relaxed) - 1;
                bottom.store(b, std::memory_order_relaxed);
                std::atomic_thread_fence(std::memory_order_seq_cst);
                int64_t t = top.load(std::memory_order_relaxed);

                if (t <= b) {
                    void* task = array.load(std::memory_order_relaxed)[b & MASK];
                    if (t == b) {
                        if (!top.compare_exchange_strong(t, t + 1, std::memory_order_seq_cst, std::memory_order_relaxed)) {
                            task = nullptr;
                        }
                        bottom.store(b + 1, std::memory_order_relaxed);
                    }
                    return task;
                }
                bottom.store(b + 1, std::memory_order_relaxed);
                return nullptr;
            }

            /** @brief Steals a task from the top. */
            void* steal() 
            {
                int64_t t = top.load(std::memory_order_acquire);
                std::atomic_thread_fence(std::memory_order_seq_cst);
                int64_t b = bottom.load(std::memory_order_acquire);

                if (t < b) {
                    void* task = array.load(std::memory_order_relaxed)[t & MASK];
                    if (!top.compare_exchange_strong(t, t + 1, std::memory_order_seq_cst, std::memory_order_relaxed)) {
                        return nullptr;
                    }
                    return task;
                }
                return nullptr;
            }
        };

        TaskScheduler::TaskScheduler(RuntimeContext& ctx) : context(ctx) 
        {
            size_t threads = std::thread::hardware_concurrency();
            if (threads == 0) threads = 1;
            if (threads > Core::MAX_WORKER_THREADS) threads = Core::MAX_WORKER_THREADS;

            num_workers = threads;

            for(size_t i=0; i<num_workers; ++i)
            {
                worker_queues[i] = make_sushi<Deque>();
            }
            
            SR_LOG_INFO("Task scheduler initialized with {} worker queues and threads.", num_workers);
        }

        void TaskScheduler::start() 
        {
            for(size_t i=0; i<num_workers; ++i)
            {
                workers[i] = std::thread([this, i] { dispatch_loop(static_cast<int>(i)); });
            }
        }

        TaskScheduler::~TaskScheduler()
        {
            running.store(false, std::memory_order_release);
            for (size_t i = 0; i < num_workers; ++i)
            {
                if (workers[i].joinable()) workers[i].join();
            }
        }

        /**
         * @brief Main dispatch loop for worker threads.
         * 
         * Workers poll for events, try to pop tasks from their own queue, 
         * or steal tasks from others.
         */
        void TaskScheduler::dispatch_loop(int thread_id) 
        {
            if (static_cast<size_t>(thread_id) >= num_workers) 
            {
                SR_LOG_ERROR("Dispatch loop thread_id {} out of bounds!", thread_id);
                return;
            }

            auto& my_queue = worker_queues[thread_id];
            
            // TODO: Use a thread-local fixed-size array or a small-vector for event polling.
            std::vector<EventPolling::PolledEvent> active_events;

            while (running.load(std::memory_order_relaxed)) 
            {
                event_poller.poll_events(active_events, *my_queue);

                void* task = my_queue->pop();

                if (!task) {
                    task = find_task_to_steal(thread_id);
                }

                if (task) {
                    execute_task(task, active_events);
                } else {
                     // TODO: implement adaptive backoff here to save energy.
                    std::this_thread::yield();
                }
            }
        }

        /** @brief Submits a generic task to the hardware queue. */
        void TaskScheduler::execute_task(void* task_ptr, std::vector<EventPolling::PolledEvent>& events) 
        {
            auto* task = static_cast<Task*>(task_ptr);
            SR_LOG_DEBUG("Executing task: {}", task->get_name());
            
            // TODO: select the best queue based on task locality or load.
            auto& q = context.get_queue(0); 
            sycl::event e;

            if (task->get_execution_mode() == Task::ExecutionMode::HOST)
            {
                // Execute directly on the queue (library calls)
                // TODO: Pass dependencies correctly from the task graph
                // For now, passing empty dependencies as the scheduler doesn't track fine-grained event deps here yet
                e = task->execute_host(q, {});
            }
            else
            {
                // Execute as kernel lambda
                // TODO: support direct sycl event chaining to reduce host overhead.
                e = q.submit([&](sycl::handler& h) 
                {
                    task->execute(h);
                });
            }
            
            // Prevent dummy sycl events from crashing the polling loop
            if (e == sycl::event{})
            {
                e = q.submit([&](sycl::handler& h) {
                    h.host_task([]() {});
                });
            }

            events.push_back({e, task_ptr});
        }

        /** @brief Attempts to find a task to steal from a different thread. */
        void* TaskScheduler::find_task_to_steal(int th_id) 
        {
            // TODO: use a random victim selection strategy to reduce contention.
            // TODO: prioritize stealing from workers on the same numa node.
            for (size_t i = 0; i < num_workers; ++i) 
            {
                size_t victim_id = (th_id + i + 1) % num_workers;
                if (static_cast<int>(victim_id) == th_id) continue;
                
                void* stolen = worker_queues[victim_id]->steal();
                if (stolen) return stolen;
            }
            return nullptr;
        }

        /** @brief Submits a new task to the main thread's queue. */
        void TaskScheduler::submit(void* task) 
        {
            if (num_workers > 0) 
            {
                worker_queues[0]->push(task);
            }
        }

    } // namespace Scheduler
} // namespace SushiRuntime