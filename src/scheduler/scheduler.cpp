/**************************************************************************/
/* scheduler.cpp                                                          */
/**************************************************************************/
/* This file is part of:                         */
/* SushiRuntime                              */
/* https://github.com/SushiSystems/SushiRuntime              */
/* https://sushisystems.io                         */
/**************************************************************************/
/* Copyright (c) 2026-present Mustafa Garip & Sushi Systems               */
/* All Rights Reserved.                                                   */
/* */
/* CONFIDENTIAL: This software is the proprietary information of          */
/* Mustafa Garip & Sushi Systems. Unauthorized copying of this file,      */
/* via any medium is strictly prohibited.                                 */
/* */
/* This source code and the intellectual property contained herein        */
/* is confidential and may not be disclosed, copied, or used without      */
/* explicit written permission from the copyright holders.                */
/**************************************************************************/

#include <new>
#include <thread>
#include <optional>
#include <mutex>
#include <deque>
#include <atomic> // Required for std::atomic_ref
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
         * @brief Highly optimized lock-free Chase-Lev deque for task scheduling.
         * * Utilizes a fixed-capacity power-of-2 ring buffer to eliminate dynamic 
         * resizing overhead and memory reclamation (garbage collection) issues. 
         * Accesses raw array memory atomically via C++20 std::atomic_ref.
         * * The owner thread pushes and pops from the bottom (LIFO).
         * Stealer threads steal from the top (FIFO).
         */
        class alignas(SushiRuntime::Core::CACHE_LINE_SIZE) Deque : public Core::RefCounted 
        {
        private:
            /** @brief Fixed capacity to avoid runtime resizing. 256K is huge for DAG workloads. */
            static constexpr int64_t CAPACITY = 262144; 
            static constexpr int64_t MASK = CAPACITY - 1;

            /** @brief Raw array managed atomically via std::atomic_ref. */
            Task** items;

            alignas(SushiRuntime::Core::CACHE_LINE_SIZE) std::atomic<int64_t> top;
            alignas(SushiRuntime::Core::CACHE_LINE_SIZE) std::atomic<int64_t> bottom;
            
        public:
            Deque() : top(0), bottom(0) 
            {
                items = new Task*[CAPACITY]();
            }

            ~Deque() override
            {
                delete[] items;
            }

            /** @brief Pushes a task into the deque. */
            void push(Task* task) 
            {
                int64_t b = bottom.load(std::memory_order_relaxed);
                int64_t t = top.load(std::memory_order_acquire);
                
                if (b - t >= CAPACITY) 
                {
                    SR_LOG_ERROR("Chase-Lev Deque overflow! Capacity {} exceeded.", CAPACITY);
                    return; 
                }

                // C++20 atomic_ref enables atomic access directly on the raw pointer array
                std::atomic_ref<Task*>(items[b & MASK]).store(task, std::memory_order_relaxed);
                
                std::atomic_thread_fence(std::memory_order_release);
                bottom.store(b + 1, std::memory_order_relaxed);
            }

            /** @brief Removes and returns a task from the bottom (owner thread). */
            Task* pop() 
            {
                int64_t b = bottom.load(std::memory_order_relaxed) - 1;
                bottom.store(b, std::memory_order_relaxed);

                std::atomic_thread_fence(std::memory_order_seq_cst);
                int64_t t = top.load(std::memory_order_relaxed);

                if (t <= b) 
                {
                    Task* task = std::atomic_ref<Task*>(items[b & MASK]).load(std::memory_order_relaxed);

                    if (t == b) 
                    {
                        if (!top.compare_exchange_strong(t, 
                                                         t + 1, 
                                                         std::memory_order_seq_cst, 
                                                         std::memory_order_relaxed))
                        {
                            task = nullptr;
                        }
                        bottom.store(b + 1, std::memory_order_relaxed);
                    }
                    return task;
                }
                
                bottom.store(b + 1, std::memory_order_relaxed);
                return nullptr;
            }

            /** @brief Steals a task from the top (stealer threads). */
            Task* steal() 
            {
                int64_t t = top.load(std::memory_order_acquire);
                std::atomic_thread_fence(std::memory_order_seq_cst);
                int64_t b = bottom.load(std::memory_order_acquire);

                if (t < b) 
                {
                    Task* task = std::atomic_ref<Task*>(items[t & MASK]).load(std::memory_order_relaxed);

                    if (!top.compare_exchange_strong(t, 
                                                     t + 1, 
                                                     std::memory_order_seq_cst, 
                                                     std::memory_order_relaxed))
                    {
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
            // Workers begin in a waiting state. arm_for_execution() must be called
            // before any work is submitted to signal the pool to begin processing.
            for(size_t i=0; i<num_workers; ++i)
            {
                workers[i] = std::thread([this, i] { dispatch_loop(static_cast<int>(i)); });
            }
        }

        void TaskScheduler::arm_for_execution()
        {
            // Signal workers that a new execution round has started.
            // This is called by ExecutionEngine before submitting root nodes,
            // and again by the sentinel's host_task to re-arm for the next round.
            running.store(true, std::memory_order_release);
            pending_cv.notify_all();
        }

        void TaskScheduler::stop()
        {
            // Signal workers to drain and exit.
            shutdown_requested.store(true, std::memory_order_release);
            running.store(false, std::memory_order_release);
            pending_cv.notify_all();
            for (size_t i = 0; i < num_workers; ++i)
            {
                if (workers[i].joinable()) workers[i].join();
            }

            // Release any submissions that never made it into execution.
            std::lock_guard<std::mutex> lock(pending_mutex);
            while (!pending_submissions.empty())
            {
                Task* task = pending_submissions.front();
                pending_submissions.pop_front();
                if (task)
                {
                    task->release();
                }
            }

            while (!priority_submissions.empty())
            {
                Task* task = priority_submissions.top();
                priority_submissions.pop();
                if (task)
                {
                    task->release();
                }
            }
        }

        TaskScheduler::~TaskScheduler()
        {
            // stop() is idempotent; calling it here ensures cleanup even if
            // the owner (ExecutionEngine) failed to call shutdown() explicitly.
            stop();
        }

        bool TaskScheduler::has_pending_submissions()
        {
            std::lock_guard<std::mutex> lock(pending_mutex);
            return !pending_submissions.empty() || !priority_submissions.empty();
        }

        void TaskScheduler::drain_pending_submissions(int thread_id)
        {
            if (thread_id < 0 || static_cast<size_t>(thread_id) >= num_workers)
                return;

            // Grab a reasonable batch to prevent local deque overflow 
            // and ensure fair load distribution among all workers.
            constexpr size_t BATCH_SIZE = 4096; 
            std::vector<Task*> batch;
            
            {
                std::lock_guard<std::mutex> lock(pending_mutex);
                if (pending_submissions.empty()) return;

                size_t grab_count = std::min(BATCH_SIZE, pending_submissions.size());
                batch.reserve(grab_count);

                for (size_t i = 0; i < grab_count; ++i)
                {
                    batch.push_back(pending_submissions.front());
                    pending_submissions.pop_front();
                }

                // If there's still work left in the global injector, wake up another worker
                if (!pending_submissions.empty())
                    pending_cv.notify_one();
            }

            auto& my_queue = worker_queues[thread_id];
            
            for (Task* task : batch)
                if (task) my_queue->push(task);
        }

        /**
         * @brief Main dispatch loop for worker threads.
         *
         * Lifecycle:
         * 1. IDLE phase   — running == false, no work. Yield until armed.
         * 2. ACTIVE phase — running == true. Process tasks and poll events.
         * 3. DRAIN phase  — running == false again (stop() called).
         * Continue until inflight events and pending
         * submissions are fully drained, then exit.
         *
         * This design allows the pool to be reused across multiple graph
         * execution rounds without recreating threads. 
         */
        void TaskScheduler::dispatch_loop(int thread_id) 
        {
            if (static_cast<size_t>(thread_id) >= num_workers) 
            {
                SR_LOG_ERROR("Dispatch loop thread_id {} out of bounds!", thread_id);
                return;
            }

            auto& my_queue = worker_queues[thread_id];
            
            EventPolling::ActiveEventList active_events;

            int spin_count = 0;

            while (true)
            {
                const bool is_running = running.load(std::memory_order_acquire);
                const bool is_shutdown = shutdown_requested.load(std::memory_order_acquire);

                // IDLE / waiting-for-arm phase: no work, not yet signalled.
                if (!is_running && !is_shutdown && active_events.is_empty() && !has_pending_submissions())
                {
                    // Full idle, sleep until armed or stopped
                    std::unique_lock<std::mutex> lock(pending_mutex);
                    pending_cv.wait_for(lock, std::chrono::milliseconds(10), [this]() 
                    {
                        return running.load(std::memory_order_acquire) || 
                               shutdown_requested.load(std::memory_order_acquire) || 
                               !pending_submissions.empty() || 
                               !priority_submissions.empty();
                    });
                    continue;
                }

                // ACTIVE / DRAIN phase: process whatever work is available.
                drain_pending_submissions(thread_id);
                event_poller.poll_events(active_events, *my_queue);

                Task* task = nullptr;
                
                {
                    std::lock_guard<std::mutex> lock(pending_mutex);
                    if (!priority_submissions.empty())
                    {
                        task = priority_submissions.top();
                        priority_submissions.pop();
                    }
                }

                if (!task)
                {
                    task = my_queue->pop();
                }

                if (!task) 
                {
                    task = find_task_to_steal(thread_id);
                }

                if (task) 
                {
                    spin_count = 0; // Reset backoff

                    // If the task is heavily parallelizable, split a chunk off and throw it
                    // back into our deque so idle worker threads can steal and help us execute it.
                    if (task->is_splittable())
                    {
                        if (auto split_chunk = task->split())
                            my_queue->push(split_chunk.value().leak());
                    }

                    execute_task(task, active_events);
                } 
                else if (is_shutdown && active_events.is_empty() && !has_pending_submissions())
                {
                    // DRAIN complete: shutdown requested, nothing left to process.
                    // This is the exit condition for the stop() path.
                    break;
                }
                else
                {
                    // Adaptive backoff to reduce 100% CPU usage
                    spin_count++;
                    if (spin_count < 100) 
                    {
                        // Active spinning (pause instructions via yield)
                        std::this_thread::yield();
                    } 
                    else 
                    {
                        // Heavy backoff when completely starved of work
                        std::this_thread::sleep_for(std::chrono::microseconds(100));
                    }
                }
            }
        }

        /** @brief Executes a single task and tracks its events. */
        void TaskScheduler::execute_task(Task* task, EventPolling::ActiveEventList& events) 
        {
            if (!task)
            {
                return;
            }

            SR_LOG_DEBUG("Executing task: {}", task->get_name());
            
            int target_numa_id = 0;
            std::vector<sycl::event> deps;
            
            if (auto* node = dynamic_cast<SushiRuntime::Graph::Node*>(task))
            {
                if (node->metadata.numa_affinity >= 0)
                    target_numa_id = node->metadata.numa_affinity;
                
                deps = node->sycl_deps.to_vector();
            }

            auto& q = context.get_numa_local_queue(target_numa_id);
            sycl::event e;

            if (task->get_execution_mode() == Task::ExecutionMode::HOST)
            {
                // Execute directly on the queue (library calls)
                e = task->execute_host(q, deps);
            }
            else
            {
                // Execute as kernel lambda
                e = q.submit([&](sycl::handler& h) 
                {
                    if (!deps.empty())
                    {
                        h.depends_on(deps);
                    }
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

            if (!events.push({e, task})) 
            {
                SR_LOG_WARN("ActiveEventList capacity is full, events cannot be polled!");
            }
        }

        /** @brief Attempts to find a task to steal from a different thread. */
        Task* TaskScheduler::find_task_to_steal(int th_id) 
        {
            // Stealing strategy: Start at a random victim and wrap around.
            // This spreads the load and reduces contention compared to always starting at 0.
            static thread_local size_t steal_cursor = static_cast<size_t>(th_id);
            steal_cursor = (steal_cursor + 1) % num_workers;
            
            for (size_t i = 0; i < num_workers; ++i) 
            {
                size_t victim_id = (steal_cursor + i) % num_workers;
                if (static_cast<int>(victim_id) == th_id) continue;
                
                Task* stolen = worker_queues[victim_id]->steal();
                if (stolen) return stolen;
            }
            return nullptr;
        }

        /** @brief Submits a new task to the main thread's queue. */
        void TaskScheduler::submit(Task* task) 
        {
            if (!task)
            {
                return;
            }

            task->add_ref();

            // External submissions go into a dedicated injector queue so we do not
            // violate the single-producer constraint of the per-worker Chase-Lev deques.
            {
                std::lock_guard<std::mutex> lock(pending_mutex);
                if (task->get_priority() > 0)
                {
                    priority_submissions.push(task);
                }
                else
                {
                    pending_submissions.push_back(task);
                }
            }
            
            // Wake a worker up if they are heavily sleeping
            pending_cv.notify_one();
        }

    } // namespace Scheduler
} // namespace SushiRuntime