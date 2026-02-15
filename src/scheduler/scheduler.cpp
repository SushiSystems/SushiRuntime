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

#include "../../include/SushiRuntime/scheduler/task_scheduler.hpp"
#include "../../include/SushiRuntime/scheduler/splittable_task.hpp"
#include "../../include/SushiRuntime/execution/runtime_context.hpp"
#include "../../include/SushiRuntime/core/logger.hpp"
#include "../../include/SushiRuntime/core/common.hpp"
#include <thread>
#include <optional>
#include <new>

namespace SushiRuntime
{
    namespace Scheduler
    {
        using namespace SushiRuntime::Execution;
        using namespace SushiRuntime::Core;

        /**
         * @brief lock-free chase-lev deque for task scheduling.
         * 
         * the owner thread pushes and pops from the bottom (lifo).
         * stealer threads steal from the top (fifo).
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

            /** @brief pushes a task into the deque. */
            void push(void* task) 
            {
                int64_t b = bottom.load(std::memory_order_relaxed);
                int64_t t = top.load(std::memory_order_acquire);
                
                if (b - t >= INITIAL_CAPACITY) 
                {
                    SR_THROW_IF(true, "hpc deque overflow. capacity: {}", INITIAL_CAPACITY);
                }

                array.load(std::memory_order_relaxed)[b & MASK] = task;
                std::atomic_thread_fence(std::memory_order_release);
                bottom.store(b + 1, std::memory_order_relaxed);
            }

            /** @brief removes and returns a task from the bottom. */
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

            /** @brief steals a task from the top. */
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

            for(size_t i=0; i<threads; ++i)
            {
                worker_queues.push_back(make_sushi<Deque>());
            }
            
            SR_LOG_INFO("task scheduler initialized with {} worker queues.", threads);
        }

        TaskScheduler::~TaskScheduler() = default;

        /**
         * @brief main dispatch loop for worker threads.
         * 
         * workers poll for events, try to pop tasks from their own queue, 
         * or steal tasks from others.
         */
        void TaskScheduler::dispatch_loop(int thread_id) 
        {
            if (static_cast<size_t>(thread_id) >= worker_queues.size()) 
            {
                SR_LOG_ERROR("dispatch loop thread_id {} out of bounds!", thread_id);
                return;
            }

            auto& my_queue = worker_queues[thread_id];
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

        /** @brief submits a generic splittable task to the hardware queue. */
        void TaskScheduler::execute_task(void* task_ptr, std::vector<EventPolling::PolledEvent>& events) 
        {
            auto* task = static_cast<SplittableTask*>(task_ptr);
            
            // TODO: select the best queue based on task locality or load.
            auto& q = context.get_queue(0); 
            
            // TODO: support direct sycl event chaining to reduce host overhead.
            sycl::event e = q.submit([&](sycl::handler& h) 
            {
                task->execute(h);
            });
            
            events.push_back({e, task_ptr});
        }

        /** @brief attempts to find a task to steal from a different thread. */
        void* TaskScheduler::find_task_to_steal(int th_id) 
        {
            // TODO: use a random victim selection strategy to reduce contention.
            // TODO: prioritize stealing from workers on the same numa node.
            for (size_t i = 0; i < worker_queues.size(); ++i) 
            {
                size_t victim_id = (th_id + i + 1) % worker_queues.size();
                if (static_cast<int>(victim_id) == th_id) continue;
                
                void* stolen = worker_queues[victim_id]->steal();
                if (stolen) return stolen;
            }
            return nullptr;
        }

        /** @brief submits a new task to the main thread's queue. */
        void TaskScheduler::submit(void* task) 
        {
            if (!worker_queues.empty()) 
            {
                worker_queues[0]->push(task);
            }
        }

    } // namespace Scheduler
} // namespace SushiRuntime