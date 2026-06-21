/**************************************************************************/
/* scheduler.cpp                                                          */
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

#include <new>
#include <thread>
#include <optional>
#include <mutex>
#include <deque>
#include <atomic>
#include <random>
#include <algorithm>
#include <SushiRuntime/core/common.hpp>
#include <SushiRuntime/core/logger.hpp>
#include <SushiRuntime/core/sycl_compat.hpp>
#include <SushiRuntime/scheduler/task.hpp>
#include <SushiRuntime/topology/affinity.hpp>
#include <SushiRuntime/memory/epoch_reclamation.hpp>
#include <SushiRuntime/scheduler/task_scheduler.hpp>
#include <SushiRuntime/execution/runtime_context.hpp>
#ifdef SR_DISTRIBUTED
#include <SushiRuntime/graph/node.hpp>
#include <SushiRuntime/distributed/distributed_coordinator.hpp>
#endif

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
         * Each slot is a std::atomic<Task*> so the array is accessed atomically.
         * * The owner thread pushes and pops from the bottom (LIFO).
         * Stealer threads steal from the top (FIFO).
         */
        class alignas(SushiRuntime::Core::CACHE_LINE_SIZE) Deque : public Core::RefCounted 
        {
        public:
            /** @brief Fixed capacity to avoid runtime resizing. 256K is huge for DAG workloads. */
            static constexpr int64_t CAPACITY = 262144;
            static constexpr int64_t MASK = CAPACITY - 1;

            /** @brief Ring buffer of atomic slots (C++17 replacement for atomic_ref). */
            std::atomic<Task*>* items;

            alignas(SushiRuntime::Core::CACHE_LINE_SIZE) std::atomic<int64_t> top;
            alignas(SushiRuntime::Core::CACHE_LINE_SIZE) std::atomic<int64_t> bottom;
            
        public:
            Deque() : top(0), bottom(0) 
            {
                items = new std::atomic<Task*>[CAPACITY]();
            }

            ~Deque() override
            {
                delete[] items;
            }

            inline size_t size() const 
            {
                int64_t b = bottom.load(std::memory_order_relaxed);
                int64_t t = top.load(std::memory_order_relaxed);
                
                return (b >= t) ? static_cast<size_t>(b - t) : 0;
            }


            /** 
             * @brief Pushes a task into the deque. 
             * @param task Pointer to the task to push.
             */
            void push(Task* task) 
            {
                int64_t b = bottom.load(std::memory_order_relaxed);
                int64_t t = top.load(std::memory_order_acquire);
                
                if (b - t >= CAPACITY) 
                {
                    SR_LOG_ERROR("Chase-Lev Deque overflow! Capacity {} exceeded.", CAPACITY);
                    return; 
                }

                items[b & MASK].store(task, std::memory_order_relaxed);
                
                std::atomic_thread_fence(std::memory_order_release);
                bottom.store(b + 1, std::memory_order_relaxed);
            }

            /** 
             * @brief Removes and returns a task from the bottom (owner thread). 
             * @return Pointer to the popped task, or nullptr if empty.
             */
            Task* pop() 
            {
                int64_t b = bottom.load(std::memory_order_relaxed) - 1;
                bottom.store(b, std::memory_order_relaxed);

                std::atomic_thread_fence(std::memory_order_seq_cst);
                int64_t t = top.load(std::memory_order_relaxed);

                if (t <= b) 
                {
                    Task* task = items[b & MASK].load(std::memory_order_relaxed);

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

            /** 
             * @brief Steals a task from the top (stealer threads). 
             * @return Pointer to the stolen task, or nullptr if empty.
             */
            Task* steal() 
            {
                int64_t t = top.load(std::memory_order_acquire);
                std::atomic_thread_fence(std::memory_order_seq_cst);
                int64_t b = bottom.load(std::memory_order_acquire);

                if (t < b) 
                {
                    Task* task = items[t & MASK].load(std::memory_order_relaxed);

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

        /**
         * @brief Constructs a new TaskScheduler.
         * Initializes worker threads and queues based on hardware concurrency.
         * @param ctx The RuntimeContext to associate with this scheduler.
         */
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

            build_steal_topology();

            SR_LOG_INFO("Task scheduler initialized with {} worker queues and threads.", num_workers);
        }

        /**
         * @brief Builds the NUMA-aware steal victim table once, at construction.
         *
         * For each worker, all other workers are ordered by ascending NUMA
         * distance from that worker's node (ties broken randomly to spread
         * contention). Node-local victims therefore lead every list, and
         * local_tier_len_ records how many of them there are. find_task_to_steal()
         * then walks the list in order, exhausting the local tier before touching
         * any remote node. If the provider exposes no CPU topology, every worker
         * maps to node 0 → a single flat tier == the original NUMA-blind behaviour.
         */
        void TaskScheduler::build_steal_topology()
        {
            const std::vector<int>& core_to_numa = context.cpu_core_to_numa();

            worker_numa_.assign(num_workers, 0);
            for (size_t i = 0; i < num_workers; ++i)
            {
                // Worker i is pinned to core i (see dispatch_loop), so its node is
                // the node of core i. Unknown/out-of-range cores fall back to 0.
                if (i < core_to_numa.size())
                    worker_numa_[i] = core_to_numa[i];
            }

            victim_order_.assign(num_workers, {});
            local_tier_len_.assign(num_workers, 0);

            std::mt19937 rng(0x51515151u);
            for (size_t th = 0; th < num_workers; ++th)
            {
                const int my_node = worker_numa_[th];

                struct Victim { int id; int dist; uint64_t key; };
                std::vector<Victim> victims;
                victims.reserve(num_workers > 0 ? num_workers - 1 : 0);

                int local = 0;
                for (size_t j = 0; j < num_workers; ++j)
                {
                    if (j == th) continue;
                    const int dist = context.numa_distance(my_node, worker_numa_[j]);
                    victims.push_back({ static_cast<int>(j), dist, rng() });
                    if (worker_numa_[j] == my_node) ++local;
                }

                std::sort(victims.begin(), victims.end(),
                          [](const Victim& a, const Victim& b)
                          {
                              if (a.dist != b.dist) return a.dist < b.dist;
                              return a.key < b.key; // random, stable tie-break
                          });

                auto& order = victim_order_[th];
                order.reserve(victims.size());
                for (const auto& v : victims) order.push_back(v.id);

                local_tier_len_[th] = local;
            }
        }

        /**
         * @brief Starts the task scheduler.
         * Spawns worker threads that wait for tasks to be submitted.
         */
        void TaskScheduler::start() 
        {
            // Workers begin in a waiting state. arm_for_execution() must be called
            // before any work is submitted to signal the pool to begin processing.
            for(size_t i=0; i<num_workers; ++i)
            {
                workers[i] = std::thread([this, i] { dispatch_loop(static_cast<int>(i)); });
            }
        }

        /**
         * @brief Arms the scheduler for a new execution round.
         * Signals worker threads to wake up and begin processing tasks.
         */
        void TaskScheduler::arm_for_execution()
        {
            // Signal workers that a new execution round has started.
            // This is called by ExecutionEngine before submitting root nodes,
            // and again by the sentinel's host_task to re-arm for the next round.
            running.store(true, std::memory_order_release);
            pending_cv.notify_all();
        }

        /**
         * @brief Stops the task scheduler.
         * Signals workers to drain remaining tasks and exit, then joins threads.
         */
        void TaskScheduler::stop()
        {
            // Signal workers to drain and exit.
            shutdown_requested.store(true, std::memory_order_release);
            running.store(false, std::memory_order_release);
            pending_cv.notify_all();
            for (size_t i = 0; i < num_workers; ++i)
                if (workers[i].joinable()) workers[i].join();   


            for (size_t i = 0; i < num_workers; ++i)
            {
                if (worker_queues[i])
                {
                    while (Task* task = worker_queues[i]->pop())
                        task->release();
                }
            }

            // Release any submissions that never made it into execution.
            std::lock_guard<std::mutex> lock(pending_mutex);
            while (!pending_submissions.empty())
            {
                Task* task = pending_submissions.front();
                pending_submissions.pop_front();
                if (task)
                    task->release();
            }

            while (!priority_submissions.empty())
            {
                Task* task = priority_submissions.top();
                priority_submissions.pop();
                if (task)
                    task->release();
            }
        }

        /**
         * @brief Destroys the TaskScheduler.
         * Ensures that the scheduler is stopped before destruction.
         */
        TaskScheduler::~TaskScheduler()
        {
            // stop() is idempotent; calling it here ensures cleanup even if
            // the owner (ExecutionEngine) failed to call shutdown() explicitly.
            stop();
        }

        /**
         * @brief Checks if there are any pending submissions in the global queue.
         * @return true if there are pending tasks, false otherwise.
         */
        bool TaskScheduler::has_pending_submissions()
        {
            std::lock_guard<std::mutex> lock(pending_mutex);
            return !pending_submissions.empty() || !priority_submissions.empty();
        }

        /**
         * @brief Drains a batch of pending submissions into a worker's local queue.
         * Prevents global queue contention by pulling tasks in batches.
         * @param thread_id The ID of the worker thread pulling the tasks.
         */
        void TaskScheduler::drain_pending_submissions(int thread_id)
        {
            if (thread_id < 0 || static_cast<size_t>(thread_id) >= num_workers)
                return;

            auto& my_queue = worker_queues[thread_id];
            
            // Prevent Chase-Lev Deque overflow by refusing to pull more tasks 
            // from the global queue if the local queue is already heavily loaded.
            if (my_queue->size() >= Deque::CAPACITY / 2)
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

            for (Task* task : batch)
                if (task) my_queue->push(task);
        }

        /**
         * @brief Main dispatch loop for worker threads.
         *
         * Lifecycle:
         * 1. IDLE phase   - running == false, no work. Yield until armed.
         * 2. ACTIVE phase - running == true. Process tasks and poll events.
         * 3. DRAIN phase  - running == false again (stop() called).
         * Continue until inflight events and pending
         * submissions are fully drained, then exit.
         *
         * This design allows the pool to be reused across multiple graph
         * execution rounds without recreating threads. 
         * @param thread_id The ID of the worker executing this loop.
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

            SushiRuntime::Topology::pin_current_thread_to_core(thread_id);

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
                Task* task = nullptr;

                {
                    SushiRuntime::Memory::EpochGuard epoch_guard;

                    event_poller.poll_events(active_events, *my_queue);
                    
                    {
                        std::lock_guard<std::mutex> lock(pending_mutex);
                        if (!priority_submissions.empty())
                        {
                            task = priority_submissions.top();
                            priority_submissions.pop();
                        }
                    }

                    if (!task)
                        task = my_queue->pop();

                    if (!task) 
                        task = find_task_to_steal(thread_id);

                    if (task)
                    {
                        spin_count = 0;
                        if (task->is_splittable())
                        {
                            if (auto split_chunk = task->split())
                                my_queue->push(split_chunk.value().leak());
                        }

#ifdef SR_DISTRIBUTED
                        // Offload branch: a single bool load for the overwhelming
                        // majority of (local) tasks. Only nodes that compile()
                        // stamped offloadable, in Master mode, ever reach the
                        // coordinator. On a successful offload the scheduling ref
                        // is transferred to the coordinator (released when the
                        // remote result fires complete_offloaded_node), so we must
                        // neither execute nor release it here.
                        bool offloaded = false;
                        if (coordinator_ && task->is_graph_node())
                        {
                            auto* gnode = static_cast<SushiRuntime::Graph::Node*>(task);
                            if (gnode->offloadable_)
                                offloaded = coordinator_->try_offload(gnode);
                        }
                        if (!offloaded)
                            execute_task(task, active_events);
#else
                        execute_task(task, active_events);
#endif
                    }
                }

                if (!task) 
                {
                    if (is_shutdown && active_events.is_empty() && !has_pending_submissions())
                        break;

                    // It is extremely important for the thread to be outside the Epoch while in the Backoff (Yield/Sleep) phase,
                    // so that a sleeping thread does not stall the global memory reclamation process.
                    spin_count++;
                    if (spin_count < 100) 
                        std::this_thread::yield();
                    else 
                        std::this_thread::sleep_for(std::chrono::microseconds(100));
                }
            }
        }

        /** 
         * @brief Executes a single task and tracks its events. 
         * @param task Pointer to the task to execute.
         * @param events The list of active events to track completion.
         */
        void TaskScheduler::execute_task(Task* task, EventPolling::ActiveEventList& events) 
        {
            if (!task) return;

            auto& tls_state = SushiRuntime::Memory::Detail::get_tls_state(); 
            if (tls_state.id >= 0 && static_cast<size_t>(tls_state.id) < Core::MAX_WORKER_THREADS) 
                worker_completed_tasks[tls_state.id].fetch_add(1, std::memory_order_relaxed);

            try 
            {
                SR_LOG_DEBUG("Executing task: {}", task->get_name());
                
                int target_numa_id = 0;
                std::vector<sycl::event> deps;
                SushiRuntime::Graph::Node* node = nullptr;

                if ((node = dynamic_cast<SushiRuntime::Graph::Node*>(task)))
                {
                    if (node->metadata.numa_affinity >= 0)
                        target_numa_id = node->metadata.numa_affinity;
                    
                    deps = node->sycl_deps.to_vector();
                }

                // Both paths must run on the queue whose context owns the node's
                // USM. A HOST task is just as dangerous as a KERNEL one here: its
                // host_work lambda commonly does its own queue.submit() of a device
                // kernel using exactly the queue we hand it, so giving it a sibling
                // device's queue (round-robin) causes a cross-context illegal
                // access (e.g. CPU-allocated USM dispatched onto a CUDA queue).
                // Route by the data pointer; fall back to NUMA-local round-robin
                // only when the node carries no USM to anchor on.
                void* anchor = nullptr;
                if (node)
                {
                    if (!node->io_writes_.empty())     anchor = node->io_writes_.front();
                    else if (!node->io_reads_.empty()) anchor = node->io_reads_.front();
                }
                auto& q = anchor ? context.get_queue_for_ptr(anchor, target_numa_id)
                                 : context.get_numa_local_queue(target_numa_id);

                sycl::event e;
                if (task->get_execution_mode() == Task::ExecutionMode::HOST)
                {
                    e = task->execute_host(q, deps);
                }
                else
                {
                    e = q.submit([&](sycl::handler& h) 
                    {
                        if (!deps.empty()) h.depends_on(deps);
                        task->execute(h);
                    });
                }
                
                // An empty event means the host work already ran inline (synchronous
                // execute_host) — there is nothing to wait on. Route it through the
                // poller flagged ready, so successors fire and the task is released
                // uniformly, without fabricating a barrier event. The old barrier
                // relied on host-op status polling, which AdaptiveCpp does not report.
                const bool ready = (e == sycl::event{});
                if (!events.push({e, task, ready}))
                    SR_LOG_WARN("ActiveEventList capacity is full, events cannot be polled!");
            } 
            catch (...) 
            {
                SR_LOG_ERROR("Critical exception caught during task execution: {}", task->get_name());
                
                if (error_callback) 
                    error_callback(std::current_exception());
                
                // Diğer worker'ların durması için acil durum sinyali
                shutdown_requested.store(true, std::memory_order_release);
                running.store(false, std::memory_order_release);
            }
        }

        /**
         * @brief Attempts to steal a task, preferring NUMA-local victims.
         *
         * Walks the precomputed, distance-ordered victim list for this worker:
         *   1. Node-local tier first, with a rotating start so idle workers don't
         *      all hammer the same victim (intra-tier load spreading).
         *   2. Remote tiers next, in ascending NUMA-distance order, only reached
         *      once the local tier is exhausted (strict locality policy).
         * This keeps a task on the node where its data lives, avoiding the remote
         * memory hop that NUMA-blind stealing would incur.
         * @param th_id The ID of the worker attempting to steal.
         * @return Pointer to a stolen task, or nullptr if none found.
         */
        Task* TaskScheduler::find_task_to_steal(int th_id)
        {
            if (th_id < 0 || static_cast<size_t>(th_id) >= num_workers)
                return nullptr;

            const std::vector<int>& order = victim_order_[th_id];
            const int total = static_cast<int>(order.size());
            const int local = local_tier_len_[th_id];

            // 1. Local tier with rotating start (only meaningful when local > 0).
            if (local > 0)
            {
                static thread_local size_t local_cursor = 0;
                const int start = static_cast<int>(local_cursor++ % static_cast<size_t>(local));
                for (int k = 0; k < local; ++k)
                {
                    const int victim_id = order[(start + k) % local];
                    if (Task* stolen = worker_queues[victim_id]->steal())
                    {
                        local_steals_.fetch_add(1, std::memory_order_relaxed);
                        return stolen;
                    }
                }
            }

            // 2. Remote tiers, already sorted by ascending distance — sequential.
            for (int k = local; k < total; ++k)
            {
                if (Task* stolen = worker_queues[order[k]]->steal())
                {
                    remote_steals_.fetch_add(1, std::memory_order_relaxed);
                    return stolen;
                }
            }

            return nullptr;
        }

        /**
         * @brief Retrieves the ordered list of victim workers to steal from.
         * @param worker_id The ID of the worker.
         * @return A vector of worker IDs ordered by NUMA distance.
         */
        std::vector<int> TaskScheduler::steal_victim_order(int worker_id) const
        {
            if (worker_id < 0 || static_cast<size_t>(worker_id) >= victim_order_.size())
                return {};
            return victim_order_[worker_id];
        }

        /** 
         * @brief Submits a new task to the main thread's queue. 
         * @param task Pointer to the task to submit.
         */
        void TaskScheduler::submit(Task* task) 
        {
            if (!task) return;

            task->add_ref();

            // External submissions go into a dedicated injector queue so we do not
            // violate the single-producer constraint of the per-worker Chase-Lev deques.
            {
                std::lock_guard<std::mutex> lock(pending_mutex);
                if (task->get_priority() > 0)
                    priority_submissions.push(task);
                else
                    pending_submissions.push_back(task);
            }
            
            // Wake a worker up if they are heavily sleeping
            pending_cv.notify_one();
        }

        /**
         * @brief Gets the number of worker threads.
         * @return The number of workers.
         */
        size_t TaskScheduler::get_worker_count() const 
        {
            return num_workers;
        }

        /**
         * @brief Gets the current size of a worker's local queue.
         * @param worker_id The ID of the worker.
         * @return The number of tasks in the worker's queue.
         */
        size_t TaskScheduler::get_queue_size(int worker_id) const 
        {
            if (worker_id < 0 || worker_id >= static_cast<int>(num_workers)) return 0;
            if (worker_queues[worker_id]) return worker_queues[worker_id]->size();
            return 0;
        }

        /**
         * @brief Gets the total number of tasks completed by a worker.
         * @param worker_id The ID of the worker.
         * @return The number of completed tasks.
         */
        size_t TaskScheduler::get_completion_rate(int worker_id) const 
        {
            if (worker_id < 0 || worker_id >= static_cast<int>(num_workers)) return 0;
            return worker_completed_tasks[worker_id].load(std::memory_order_relaxed);
        }

        /**
         * @brief Migrates tasks from a congested worker to other workers.
         * @param from_worker_id The ID of the heavily loaded worker.
         * @param to_worker_id Unused in this implementation (relies on global priority queue).
         * @param count The number of tasks to migrate.
         */
        void TaskScheduler::migrate_tasks(int from_worker_id, int /*to_worker_id*/, size_t count) 
        {
            if (from_worker_id < 0 || from_worker_id >= static_cast<int>(num_workers)) return;
            if (!worker_queues[from_worker_id]) return;

            // 1. Darboğazdaki (ısınmış) worker'dan lock-free olarak görevleri çal
            std::vector<Task*> rescued_tasks;
            rescued_tasks.reserve(count);

            for (size_t i = 0; i < count; ++i) 
            {
                Task* t = worker_queues[from_worker_id]->steal();
                if (t) rescued_tasks.push_back(t);
                else break; // Kuyruk boşalmışsa dur
            }

            // 2. Çalınan görevleri soğuk worker'ların kapması için Global Öncelikli Kuyruğa at
            if (!rescued_tasks.empty()) 
            {
                {
                    std::lock_guard<std::mutex> lock(pending_mutex);
                    for (Task* t : rescued_tasks) 
                    {
                        priority_submissions.push(t);
                    }
                }
                // Uyuyan / boşta bekleyen tüm soğuk worker'ları uyandır
                pending_cv.notify_all(); 
            }
        }

#ifdef SR_DISTRIBUTED
        /**
         * @brief Sets the distributed coordinator for offloading tasks.
         * @param c Pointer to the DistributedCoordinator.
         */
        void TaskScheduler::set_coordinator(Distributed::DistributedCoordinator* c)
        {
            coordinator_ = c;

            // In Master mode, bump the version of every registered buffer a
            // locally-completed node wrote. Combined with the DAG ordering, this
            // means any dependent task (notably an offloaded reader) is dispatched
            // only after the version reflects the write — so a stale worker cache
            // is forced to re-pull. Offloaded writers are handled symmetrically in
            // complete_offloaded_node(). This is the automatic counterpart to the
            // manual bump_distributed_buffer() API.
            if (c)
            {
                event_poller.set_completion_observer(
                    [this](Task* t)
                    {
                        if (!t || !t->is_graph_node()) return;
                        auto* n = static_cast<SushiRuntime::Graph::Node*>(t);
                        for (void* p : n->dist_writes_)
                            context.bump_distributed_buffer(p);
                    });

                // Lets the coordinator re-inject a node orphaned by a dead worker
                // (or a failed remote result). submit() is the thread-safe injector,
                // so this is safe from the coordinator's transport/monitor thread.
                //
                // Ref accounting: the orphaned node still carries the scheduling ref
                // that was transferred to the coordinator at offload time. submit()
                // takes its own ref (add_ref), so we drop the transferred one right
                // after — mirroring complete_offloaded_node()'s release. Net effect:
                // exactly one scheduling ref lives on, owned by the ready queue, and
                // event polling releases it when the node finally completes.
                c->set_resubmit_hook([this](Graph::Node* n) { submit(n); n->release(); });
            }
        }

        /**
         * @brief Completes a task that was offloaded to a remote node.
         * @param node The graph node that was offloaded.
         * @param success Whether the execution was successful.
         */
        void TaskScheduler::complete_offloaded_node(Graph::Node* node, bool success)
        {
            if (!node) return;

            // A failed remote result is treated like a lost worker: re-inject the
            // node so it runs on a survivor or, once the retry cap is hit, locally.
            // We must NOT bump versions or release successors on failure — the
            // authoritative buffers were not validly produced.
            if (!success)
            {
                SR_LOG_WARN("Offloaded node '{}' failed remotely; re-dispatching.", node->metadata.name);
                // submit() takes its own scheduling ref; drop the transferred one
                // (same release the success path performs at the end of this fn).
                submit(node);
                node->release();
                return;
            }

            // The offloaded write landed back in the master's authoritative buffer
            // (eager writeback) before this fired; advance its version so later
            // dependents re-pull, mirroring the local-completion observer.
            for (void* p : node->dist_writes_)
                context.bump_distributed_buffer(p);

            // Mirror EventPolling::trigger_successors, but route ready successors
            // through submit() (thread-safe injector) since this runs on the
            // coordinator's transport thread, not a worker thread.
            int32_t count   = node->inline_successor_count.load(std::memory_order_acquire);
            int32_t max_idx = std::min(count, SushiRuntime::Graph::Node::MAX_INLINE_SUCCESSORS);
            for (int32_t i = 0; i < max_idx; ++i)
            {
                auto* succ = node->inline_successors[i];
                if (succ && succ->in_degree.fetch_sub(1, std::memory_order_acq_rel) == 1)
                    submit(succ);
            }

            auto* curr = node->successor_list.load(std::memory_order_acquire);
            while (curr)
            {
                auto* succ = curr->target;
                if (succ && succ->in_degree.fetch_sub(1, std::memory_order_acq_rel) == 1)
                    submit(succ);
                curr = curr->next;
            }

            // Drop the scheduling reference that was transferred to the
            // coordinator when the node was offloaded.
            node->release();
        }
#endif

    } // namespace Scheduler
} // namespace SushiRuntime