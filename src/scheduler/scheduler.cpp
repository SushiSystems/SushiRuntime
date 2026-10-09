/**************************************************************************/
/* scheduler.cpp                                                          */
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
 * @file scheduler.cpp
 * @brief Runs the worker pool that dispatches, steals and completes graph tasks.
 * @author Mustafa Garip
 */

#include <new>
#include <thread>
#include <optional>
#include <mutex>
#include <deque>
#include <atomic>
#include <random>
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <exception>
#include <stdexcept>
#include <string>
#include <SushiRuntime/core/common.hpp>
#include <SushiRuntime/core/error.hpp>
#include <SushiRuntime/core/logger.hpp>
#include <SushiRuntime/core/sycl_compat.hpp>
#include <SushiRuntime/scheduler/task.hpp>
#include <SushiRuntime/topology/affinity.hpp>
#include <SushiRuntime/topology/topology_view.hpp>
#include <SushiRuntime/device/device_queue_set.hpp>
#include <SushiRuntime/device/queue_flavour.hpp>
#include <SushiRuntime/memory/usm_allocator.hpp>
#include <SushiRuntime/memory/epoch_reclamation.hpp>
#include <SushiRuntime/scheduler/task_scheduler.hpp>
#include <SushiRuntime/scheduler/work_stealing_deque.hpp>
#include <SushiRuntime/scheduler/worker_pool_settings.hpp>
#ifdef SR_DISTRIBUTED
#include <SushiRuntime/graph/node.hpp>
#include <SushiRuntime/distributed/distributed_coordinator.hpp>
#endif

namespace SushiRuntime
{
    namespace Scheduler
    {
        using namespace SushiRuntime::Core;

        namespace
        {
            /** @brief Pairs a failure's one-line description with the exception that carries it. */
            struct NamedFailure
            {
                std::string message;      /**< The failing task, then the original message. */
                std::exception_ptr error; /**< An exception of the original's family carrying message. */
            };

            /**
             * @brief Names @p task for a failure report.
             * @return The task's label, with the registration index when it is a graph node.
             */
            std::string describe_task(const Task& task)
            {
                if (!task.is_graph_node())
                    return Core::Detail::sr_format("task '{}'", task.get_name());

                const auto& node = static_cast<const SushiRuntime::Graph::Node&>(task);
                return Core::Detail::sr_format("node '{}' (registration index {})",
                                               node.get_name(), node.registration_index);
            }

            /**
             * @brief Builds the failure of @p task from a SushiException, keeping its code.
             * @param task The task that threw.
             * @param cause The exception it threw.
             */
            NamedFailure name_failure(const Task& task, const Core::SushiException& cause)
            {
                NamedFailure failure;
                failure.message = Core::Detail::sr_format("{} failed: {}", describe_task(task),
                                                          cause.what());
                failure.error = std::make_exception_ptr(
                    Core::SushiException(Core::Error{cause.error().code, failure.message}));
                return failure;
            }

            /**
             * @brief Builds the failure of @p task from a sycl::exception, keeping its code.
             * @param task The task that threw.
             * @param cause The exception it threw.
             */
            NamedFailure name_failure(const Task& task, const sycl::exception& cause)
            {
                NamedFailure failure;
                failure.message = Core::Detail::sr_format("{} failed: {}", describe_task(task),
                                                          cause.what());
                failure.error = std::make_exception_ptr(
                    sycl::exception(cause.code(), failure.message));
                return failure;
            }

            /**
             * @brief Builds the failure of @p task from any other standard exception.
             * @param task The task that threw.
             * @param cause The exception it threw.
             */
            NamedFailure name_failure(const Task& task, const std::exception& cause)
            {
                NamedFailure failure;
                failure.message = Core::Detail::sr_format("{} failed: {}", describe_task(task),
                                                          cause.what());
                failure.error = std::make_exception_ptr(std::runtime_error(failure.message));
                return failure;
            }

            /**
             * @brief Builds the failure of @p task from a throw that is not a std::exception.
             * @param task The task that threw.
             */
            NamedFailure name_failure(const Task& task)
            {
                NamedFailure failure;
                failure.message = Core::Detail::sr_format(
                    "{} failed with an exception that is not a std::exception",
                    describe_task(task));
                failure.error = std::make_exception_ptr(std::runtime_error(failure.message));
                return failure;
            }

            /** @brief Returns the device an address is recorded on, or -1 when it has no record. */
            int recorded_device_index(const Memory::UsmAllocator& memory, void* address)
            {
                const std::optional<std::size_t> device = memory.device_of(address);
                return device ? static_cast<int>(*device) : -1;
            }
        } // namespace

        /**
         * @brief Sizes the worker pool from the requested settings (src/scheduler/README.md).
         */
        TaskScheduler::TaskScheduler(const WorkerPoolSettings& pool,
                                     const Topology::TopologyView& topology,
                                     Device::DeviceQueueSet& devices,
                                     const Memory::UsmAllocator& memory)
            : topology_(topology), devices_(devices), memory_(memory)
        {
            size_t machine_cores = std::thread::hardware_concurrency();
            if (machine_cores == 0) machine_cores = 1;

            size_t threads = pool.worker_count != 0 ? pool.worker_count : machine_cores;
            if (threads > Core::MAX_WORKER_THREADS) threads = Core::MAX_WORKER_THREADS;
            if (threads == 0) threads = 1;

            num_workers = threads;
            pinning_ = pool.pinning;
            first_core_ = pool.first_core;

            // A core range past the last core is clamped and logged (src/scheduler/README.md).
            if (pinning_ == PinPolicy::Pin && first_core_ + num_workers > machine_cores)
            {
                const size_t last = first_core_ + num_workers;
                first_core_ = first_core_ < machine_cores ? first_core_ : 0;
                if (first_core_ + num_workers > machine_cores)
                    num_workers = machine_cores > first_core_ ? machine_cores - first_core_ : 1;

                SR_LOG_WARN("Worker core range [{}, {}) exceeds the machine's {} cores; "
                            "clamped to {} workers starting at core {}.",
                            pool.first_core, last, machine_cores, num_workers, first_core_);
            }

            for(size_t i=0; i<num_workers; ++i)
            {
                worker_queues[i] = make_sushi<Deque>();
            }

            build_steal_topology();

            // The poller owns the run-quiescence counter's updates (src/scheduler/README.md).
            event_poller.set_inflight_tracker(&inflight_, &quiesce_mutex_, &quiesce_cv_);

            if (pinning_ == PinPolicy::Pin)
            {
                SR_LOG_INFO("Task scheduler initialized with {} workers pinned to cores [{}, {}).",
                            num_workers, first_core_, first_core_ + num_workers);
            }
            else
            {
                SR_LOG_INFO("Task scheduler initialized with {} unpinned workers.", num_workers);
            }
        }

        /**
         * @brief Builds the NUMA-aware steal victim table once (src/scheduler/README.md).
         */
        void TaskScheduler::build_steal_topology()
        {
            const std::vector<int>& core_to_numa = topology_.core_to_numa();

            worker_numa_.assign(num_workers, 0);
            if (pinning_ == PinPolicy::Pin)
            {
                for (size_t i = 0; i < num_workers; ++i)
                {
                    // Worker i is pinned to core first_core_ + i; an unknown core maps to node 0.
                    const size_t core = first_core_ + i;
                    if (core < core_to_numa.size())
                        worker_numa_[i] = core_to_numa[core];
                }
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
                    const int dist = topology_.numa_distance(my_node, worker_numa_[j]);
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
            // Workers are spawned exactly once (src/scheduler/README.md).
            bool expected = false;
            if (!threads_started_.compare_exchange_strong(expected, true))
            {
                SR_LOG_WARN("TaskScheduler::start() called more than once; ignoring.");
                return;
            }

            // Workers wait; arm_for_execution() precedes submitted work (src/scheduler/README.md).
            for(size_t i=0; i<num_workers; ++i)
            {
                workers[i] = std::thread([this, i] { dispatch_loop(static_cast<int>(i)); });
            }
        }

        /**
         * @brief Picks which of a device's two queues a dispatch uses.
         */
        sycl::queue& TaskScheduler::dispatch_queue(std::size_t index)
        {
            return profiling_active_.load(std::memory_order_acquire)
                       ? devices_.queue(index, Device::QueueFlavour{false, true})
                       : devices_.queue(index);
        }

        /**
         * @brief Finds the device index behind an already-selected queue.
         */
        std::size_t TaskScheduler::queue_device_index(sycl::queue& q) const
        {
            const std::size_t n = devices_.count();
            for (std::size_t i = 0; i < n; ++i)
                if (devices_.queue(i) == q)
                    return i;
            return static_cast<std::size_t>(-1);
        }

        /**
         * @brief Wires the plan's event ledger for the round about to be armed.
         */
        void TaskScheduler::set_profiling_ledger(Core::ProfilingLedger* ledger) noexcept
        {
            active_ledger_.store(ledger, std::memory_order_release);
            event_poller.set_ledger(ledger);
        }

        /**
         * @brief Arms the scheduler for a new execution round.
         * Signals worker threads to wake up and begin processing tasks.
         */
        void TaskScheduler::arm_for_execution(bool profiling)
        {
            // Wakes workers for a new round; clears the previous round's failure.
            {
                std::lock_guard<std::mutex> lock(run_error_mutex_);
                run_error_ = nullptr;
            }
            run_failed_.store(false, std::memory_order_release);

            // The poller gets the profiling flag before any worker runs (src/scheduler/README.md).
            event_poller.set_profiling(profiling);
            profiling_active_.store(profiling, std::memory_order_release);

            // The flag is one term of the idle wait predicate, so it is set under pending_mutex.
            {
                std::lock_guard<std::mutex> lock(pending_mutex);
                running.store(true, std::memory_order_release);
            }
            pending_cv.notify_all();
        }

        /**
         * @brief Returns the pool to its parked state after a run quiesces.
         *
         * Under pending_mutex for the same reason arm_for_execution() is: the flag
         * is one term of the idle wait predicate. No notify — nothing waits for the
         * pool to become idle, and a worker cannot already be parked while the flag
         * is set. See the header for why this may only be called after quiescence.
         */
        void TaskScheduler::disarm() noexcept
        {
            // The ledger pointer must not dangle past its owning CompiledGraph's lifetime.
            set_profiling_ledger(nullptr);
            std::lock_guard<std::mutex> lock(pending_mutex);
            running.store(false, std::memory_order_release);
        }

        /**
         * @brief Stops the task scheduler.
         * Signals workers to drain remaining tasks and exit, then joins threads.
         */
        void TaskScheduler::stop()
        {
            // The flags change under pending_mutex (src/scheduler/README.md).
            {
                std::lock_guard<std::mutex> lock(pending_mutex);
                shutdown_requested.store(true, std::memory_order_release);
                running.store(false, std::memory_order_release);
            }
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
            pending_count_.store(0, std::memory_order_relaxed);
            priority_count_.store(0, std::memory_order_relaxed);
        }

        /**
         * @brief Destroys the TaskScheduler.
         * Ensures that the scheduler is stopped before destruction.
         */
        TaskScheduler::~TaskScheduler()
        {
            // stop() is idempotent, so the destructor may call it too (src/scheduler/README.md).
            stop();
        }

        /**
         * @brief Checks if there are any pending submissions in the global queue.
         * @return true if there are pending tasks, false otherwise.
         */
        bool TaskScheduler::has_pending_submissions()
        {
            // The shadow counters are read without the lock (src/scheduler/README.md).
            return pending_count_.load(std::memory_order_relaxed) != 0
                || priority_count_.load(std::memory_order_relaxed) != 0;
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
            
            // A half-full local deque pulls nothing from the injector (src/scheduler/README.md).
            if (my_queue->size() >= Deque::CAPACITY / 2)
                return;

            // An empty injector skips the mutex; a stale read is safe (src/scheduler/README.md).
            if (pending_count_.load(std::memory_order_relaxed) == 0)
                return;

            // The batch buffer is per worker and keeps its capacity (src/scheduler/README.md).
            constexpr size_t BATCH_SIZE = 4096;
            std::vector<Task*>& batch = drain_scratch_[thread_id];
            batch.clear();

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
                pending_count_.store(pending_submissions.size(), std::memory_order_relaxed);

                // If there's still work left in the global injector, wake up another worker
                if (!pending_submissions.empty())
                    pending_cv.notify_one();
            }

            for (Task* task : batch)
                if (task) my_queue->push(task);
        }

        /**
         * @brief Runs a worker's idle, active and drain phases (src/scheduler/README.md).
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

            // A pinned worker stays on core first_core_ + id (src/scheduler/README.md).
            if (pinning_ == PinPolicy::Pin)
                SushiRuntime::Topology::pin_current_thread_to_core(
                    static_cast<int>(first_core_) + thread_id);

            int spin_count = 0;

            // Clock reads are gated on profiling_active_ (src/scheduler/README.md).
            using sclock = std::chrono::steady_clock;
            auto& wt = worker_time_[thread_id];
            auto add_ns = [](std::atomic<std::uint64_t>& bucket,
                             sclock::time_point a, sclock::time_point b) noexcept
            {
                bucket.fetch_add(static_cast<std::uint64_t>(
                    std::chrono::duration_cast<std::chrono::nanoseconds>(b - a).count()),
                    std::memory_order_relaxed);
            };

            while (true)
            {
                const bool is_running = running.load(std::memory_order_acquire);
                const bool is_shutdown = shutdown_requested.load(std::memory_order_acquire);
                const bool prof = profiling_active_.load(std::memory_order_acquire);

                // A worker parks only with an empty deque of its own (src/scheduler/README.md).
                if (!is_running && !is_shutdown && active_events.is_empty()
                    && my_queue->size() == 0 && my_queue->overflow_size() == 0
                    && !has_pending_submissions())
                {
                    // The park has no timeout (src/scheduler/README.md).
                    const auto idle0 = prof ? sclock::now() : sclock::time_point{};
                    std::unique_lock<std::mutex> lock(pending_mutex);
                    pending_cv.wait(lock, [this]()
                    {
                        return running.load(std::memory_order_acquire) ||
                               shutdown_requested.load(std::memory_order_acquire) ||
                               !pending_submissions.empty() ||
                               !priority_submissions.empty();
                    });
                    if (prof) add_ns(wt.idle_ns, idle0, sclock::now());
                    continue;
                }

                // ACTIVE / DRAIN phase: process whatever work is available.
                drain_pending_submissions(thread_id);
                Task* task = nullptr;

                {
                    SushiRuntime::Memory::EpochGuard epoch_guard;

                    // A poll_events throw fails the run, not the thread (src/scheduler/README.md).
                    try
                    {
                        if (prof)
                        {
                            const auto p0 = sclock::now();
                            event_poller.poll_events(active_events, *my_queue);
                            add_ns(wt.polling_ns, p0, sclock::now());
                        }
                        else
                            event_poller.poll_events(active_events, *my_queue);
                    }
                    catch (...)
                    {
                        SR_LOG_ERROR("poll_events threw; aborting the current run.");
                        record_run_error(std::current_exception());
                    }

                    // Only a priority submission takes the global mutex (src/scheduler/README.md).
                    if (priority_count_.load(std::memory_order_relaxed) > 0)
                    {
                        std::lock_guard<std::mutex> lock(pending_mutex);
                        if (!priority_submissions.empty())
                        {
                            task = priority_submissions.top();
                            priority_submissions.pop();
                            priority_count_.store(priority_submissions.size(), std::memory_order_relaxed);
                        }
                    }

                    if (!task)
                        task = my_queue->pop();

                    if (!task)
                    {
                        if (prof)
                        {
                            const auto s0 = sclock::now();
                            task = find_task_to_steal(thread_id);
                            add_ns(wt.stealing_ns, s0, sclock::now());
                        }
                        else
                            task = find_task_to_steal(thread_id);
                    }

                    if (task)
                    {
                        spin_count = 0;
                        if (task->is_splittable())
                        {
                            if (auto split_chunk = task->split())
                                my_queue->push(split_chunk.value().leak());
                        }

#ifdef SR_DISTRIBUTED
                        // An offloaded task is owned by the coordinator (src/scheduler/README.md).
                        bool offloaded = false;
                        if (coordinator_ && task->is_graph_node())
                        {
                            auto* gnode = static_cast<SushiRuntime::Graph::Node*>(task);
                            auto* off = gnode->offload_state();
                            if (off && off->offloadable)
                                offloaded = coordinator_->try_offload(gnode);
                        }
                        if (!offloaded)
                        {
                            const auto b0 = prof ? sclock::now() : sclock::time_point{};
                            execute_task(task, thread_id, active_events);
                            if (prof) add_ns(wt.busy_ns, b0, sclock::now());
                        }
                        else
                        {
                            // Its quiescence count retires here (src/scheduler/README.md).
                            if (inflight_.fetch_sub(1, std::memory_order_acq_rel) == 1)
                            {
                                std::lock_guard<std::mutex> lock(quiesce_mutex_);
                                quiesce_cv_.notify_all();
                            }
                        }
#else
                        const auto b0 = prof ? sclock::now() : sclock::time_point{};
                        execute_task(task, thread_id, active_events);
                        if (prof) add_ns(wt.busy_ns, b0, sclock::now());
#endif
                    }
                }

                if (!task) 
                {
                    if (is_shutdown && active_events.is_empty() && !has_pending_submissions())
                        break;

                    // Backoff runs outside the epoch (src/scheduler/README.md).
                    spin_count++;
                    const auto bk0 = prof ? sclock::now() : sclock::time_point{};
                    // A worker holding in-flight events never sleeps: its successors wait on its poll.
                    if (spin_count < 100 || !active_events.is_empty())
                        std::this_thread::yield();
                    else
                        std::this_thread::sleep_for(std::chrono::microseconds(100));
                    if (prof) add_ns(wt.idle_ns, bk0, sclock::now());
                }
            }
        }

        /** 
         * @brief Executes a single task and tracks its events.
         * @param task Pointer to the task to execute.
         * @param worker_id The executing worker's id (scheduler worker id, not the
         *        EBR slot id), used for per-worker completion accounting.
         * @param events The list of active events to track completion.
         */
        void TaskScheduler::execute_task(Task* task, int worker_id, EventPolling::ActiveEventList& events)
        {
            if (!task) return;

            // An aborting run executes no more kernels (src/scheduler/README.md).
            if (run_failed_.load(std::memory_order_acquire) ||
                run_cancelled_.load(std::memory_order_acquire))
            {
                bool is_sentinel = task->is_graph_node() &&
                                   static_cast<SushiRuntime::Graph::Node*>(task)->is_sentinel;
                if (!is_sentinel)
                {
                    events.push({sycl::event{}, task, true});
                    return;
                }
            }

            // A disabled late-bound node runs no kernel (src/scheduler/README.md).
            if (task->is_graph_node())
            {
                auto* gnode = static_cast<SushiRuntime::Graph::Node*>(task);
                if (gnode->bindings_ &&
                    !gnode->bindings_->enabled.load(std::memory_order_relaxed))
                {
                    events.push({sycl::event{}, task, true});
                    return;
                }
            }

            // Completions are keyed by worker id, not EBR slot id (src/scheduler/README.md).
            if (worker_id >= 0 && static_cast<size_t>(worker_id) < Core::MAX_WORKER_THREADS)
                worker_completed_tasks[worker_id].value.fetch_add(1, std::memory_order_relaxed);

            const auto abort_run = [this, task, &events](const NamedFailure& failure)
            {
                SR_LOG_ERROR("{}; the run is aborted and its remaining tasks are skipped.",
                             failure.message);

                // Only this run fails; the pool stays re-armable (src/scheduler/README.md).
                record_run_error(failure.error);

                // The failed task is pushed as ready so successors fire (src/scheduler/README.md).
                events.push({sycl::event{}, task, true});
            };

            try 
            {
                SR_LOG_DEBUG("Executing task: {}", task->get_name());
                
                int target_numa_id = 0;
                SushiRuntime::Graph::Node* node = nullptr;

                // The downcast uses the is_graph_node() tag, not RTTI (src/scheduler/README.md).
                if (task->is_graph_node())
                {
                    node = static_cast<SushiRuntime::Graph::Node*>(task);

                    if (node->metadata.numa_affinity >= 0)
                        target_numa_id = node->metadata.numa_affinity;
                }

                // A pure-CPU host task needs no queue and runs inline (see design spec §0).
                if (node && node->get_execution_mode() == Task::ExecutionMode::HOST &&
                    !node->submits_device_work())
                {
                    // Timing follows this run's plan flag, not the context-wide setting.
                    if (profiling_active_.load(std::memory_order_acquire))
                    {
                        // Captured now, not read at reap, so a later round's step cannot land on this record.
                        const std::uint32_t step = current_dispatch_step_.load(std::memory_order_acquire);
                        const auto h0 = std::chrono::steady_clock::now();
                        node->execute_native();
                        const auto h1 = std::chrono::steady_clock::now();
                        const std::uint64_t h0_ns = static_cast<std::uint64_t>(
                            std::chrono::duration_cast<std::chrono::nanoseconds>(
                                h0.time_since_epoch()).count());
                        const std::uint64_t h1_ns = static_cast<std::uint64_t>(
                            std::chrono::duration_cast<std::chrono::nanoseconds>(
                                h1.time_since_epoch()).count());

                        // Raw steady times; converted onto the run's event clock at collect().
                        if (auto* ledger = active_ledger_.load(std::memory_order_acquire))
                        {
                            const std::uint32_t slot = ledger->claim_slot(node->registration_index);
                            if (slot != Core::ProfilingLedger::npos)
                            {
                                Core::NodeEventRecord rec{};
                                rec.node_index = node->registration_index;
                                rec.invocation = step;
                                // A host node submits no dispatch: launched_width and live_width stay WIDTH_UNKNOWN.
                                rec.submit_ns  = h0_ns;
                                rec.start_ns   = h0_ns;
                                rec.end_ns     = h1_ns;
                                ledger->write(node->registration_index, slot, rec);
                            }
                        }
                    }
                    else
                    {
                        node->execute_native();
                    }
                    events.push({sycl::event{}, task, true});
                    return;
                }

                // The queue is the one whose context owns the node's USM (src/scheduler/README.md).
                void* anchor = nullptr;
                if (node)
                {
                    if (!node->io_writes_.empty())     anchor = node->io_writes_.front().base;
                    else if (!node->io_reads_.empty()) anchor = node->io_reads_.front().base;
                }

                // A node's regions must share one device context (src/scheduler/README.md).
                if (node && !node->locality_checked_)
                {
                    const int anchor_ctx = recorded_device_index(memory_, anchor);
                    bool spans = false;
                    if (anchor_ctx >= 0)
                    {
                        auto off_anchor = [&](const std::vector<Core::ResourceRegion>& regs)
                        {
                            for (const auto& r : regs)
                            {
                                if (!r.base) continue;
                                const int c = recorded_device_index(memory_, r.base);
                                if (c >= 0 && c != anchor_ctx) return true;
                            }
                            return false;
                        };
                        spans = off_anchor(node->io_reads_) || off_anchor(node->io_writes_);
                    }
                    if (spans)
                        // A failure is not cached, so it recurs each run (src/scheduler/README.md).
                        throw Core::SushiException(Core::Error{
                            Core::Errc::invalid_graph,
                            "its buffers span multiple device contexts; co-locate them on "
                            "one device (cross-device execution is not yet supported)."});

                    // Only a registry hit is cached as the queue index (src/scheduler/README.md).
                    node->cached_queue_index_ = anchor_ctx;
                    node->locality_checked_ = true; // co-located; never re-check
                }

                // Every branch resolves an index and funnels through dispatch_queue().
                sycl::queue& q = [&]() -> sycl::queue&
                {
                    if (node && node->cached_queue_index_ >= 0)
                        return dispatch_queue(static_cast<std::size_t>(node->cached_queue_index_));

                    // The device that owns the anchor's USM is routed by index.
                    if (anchor)
                        if (const std::optional<std::size_t> owner = memory_.owning_device(anchor))
                            return dispatch_queue(*owner);

                    sycl::queue& picked =
                        devices_.queue_or_numa_local(std::nullopt, target_numa_id);

                    // Skips the index lookup entirely on the default (non-profiled) path.
                    if (!profiling_active_.load(std::memory_order_acquire))
                        return picked;

                    const std::size_t idx = queue_device_index(picked);
                    return idx != static_cast<std::size_t>(-1) ? dispatch_queue(idx) : picked;
                }();

                sycl::event e;
                if (task->get_execution_mode() == Task::ExecutionMode::HOST)
                {
                    // execute_host() needs a vector; this path allocates (src/scheduler/README.md).
                    std::vector<sycl::event> deps;
                    if (node)
                    {
                        deps.reserve(node->sycl_deps.size());
                        for (const auto& ev : node->sycl_deps)
                            deps.push_back(ev);
                    }
                    e = task->execute_host(q, deps);
                }
                else
                {
                    // The first device submission to reach here claims the run's clock pairing.
                    Core::ProfilingLedger* pairing_ledger = nullptr;
                    std::chrono::steady_clock::time_point pairing_t0;
                    if (profiling_active_.load(std::memory_order_acquire))
                    {
                        if (auto* ledger = active_ledger_.load(std::memory_order_acquire))
                            if (ledger->claim_pairing_owner(task))
                            {
                                pairing_ledger = ledger;
                                pairing_t0 = std::chrono::steady_clock::now();
                            }
                    }

                    e = q.submit([&](sycl::handler& h)
                    {
                        // depends_on() per event builds no heap vector (src/scheduler/README.md).
                        if (node)
                            for (const auto& ev : node->sycl_deps)
                                h.depends_on(ev);
                        task->execute(h);
                    });

                    if (pairing_ledger)
                    {
                        const auto pairing_t1 = std::chrono::steady_clock::now();
                        const std::uint64_t steady_ns = static_cast<std::uint64_t>(
                            std::chrono::duration_cast<std::chrono::nanoseconds>(
                                pairing_t0.time_since_epoch()).count());
                        const std::uint64_t uncertainty_ns = static_cast<std::uint64_t>(
                            std::chrono::duration_cast<std::chrono::nanoseconds>(
                                pairing_t1 - pairing_t0).count());
                        pairing_ledger->set_pairing_submit_window(steady_ns, uncertainty_ns);
                    }
                }
                
                // An empty event is pushed as ready, never dropped (src/scheduler/README.md).
                const bool ready = (e == sycl::event{});
                const SushiRuntime::Graph::NodeWidths widths =
                    (node && profiling_active_.load(std::memory_order_acquire))
                        ? SushiRuntime::Graph::snapshot_widths(*node)
                        : SushiRuntime::Graph::NodeWidths{};
                events.push({e, task, ready, /*has_event=*/!ready,
                            current_dispatch_step_.load(std::memory_order_acquire), widths});
            } 
            catch (const Core::SushiException& cause)
            {
                abort_run(name_failure(*task, cause));
            }
            catch (const sycl::exception& cause)
            {
                abort_run(name_failure(*task, cause));
            }
            catch (const std::exception& cause)
            {
                abort_run(name_failure(*task, cause));
            }
            catch (...)
            {
                abort_run(name_failure(*task));
            }
        }

        /**
         * @brief Records the run's first exception and flips run_failed_ (src/scheduler/README.md).
         * @param ep The exception thrown by the task; later ones are dropped.
         */
        void TaskScheduler::record_run_error(std::exception_ptr ep)
        {
            std::lock_guard<std::mutex> lock(run_error_mutex_);
            if (!run_failed_.load(std::memory_order_relaxed))
            {
                run_error_ = ep;
                run_failed_.store(true, std::memory_order_release);
            }
        }

        /**
         * @brief Returns the first exception captured during the current run.
         * @return The captured exception, or null if the run did not fail.
         */
        std::exception_ptr TaskScheduler::run_error()
        {
            std::lock_guard<std::mutex> lock(run_error_mutex_);
            return run_error_;
        }

        /**
         * @brief Returns the accumulated time split for a worker.
         */
        TaskScheduler::WorkerTimeSplit
        TaskScheduler::get_worker_times(int worker_id) const noexcept
        {
            WorkerTimeSplit s;
            if (worker_id < 0 || static_cast<size_t>(worker_id) >= num_workers)
                return s;
            const auto& b = worker_time_[worker_id];
            s.busy_ns     = b.busy_ns.load(std::memory_order_relaxed);
            s.stealing_ns = b.stealing_ns.load(std::memory_order_relaxed);
            s.polling_ns  = b.polling_ns.load(std::memory_order_relaxed);
            s.idle_ns     = b.idle_ns.load(std::memory_order_relaxed);
            return s;
        }

        /**
         * @brief Zeroes every worker's accumulated time split.
         */
        void TaskScheduler::reset_worker_times() noexcept
        {
            for (size_t i = 0; i < Core::MAX_WORKER_THREADS; ++i)
            {
                worker_time_[i].busy_ns.store(0, std::memory_order_relaxed);
                worker_time_[i].stealing_ns.store(0, std::memory_order_relaxed);
                worker_time_[i].polling_ns.store(0, std::memory_order_relaxed);
                worker_time_[i].idle_ns.store(0, std::memory_order_relaxed);
            }
        }

        /**
         * @brief Steals a task, node-local victims first (src/scheduler/README.md).
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

            // The task is counted before workers can reach it (src/scheduler/README.md).
            inflight_.fetch_add(1, std::memory_order_relaxed);

            // External submissions use the injector, not a worker deque (src/scheduler/README.md).
            {
                std::lock_guard<std::mutex> lock(pending_mutex);
                if (task->get_priority() > 0)
                {
                    priority_submissions.push(task);
                    priority_count_.store(priority_submissions.size(), std::memory_order_relaxed);
                }
                else
                {
                    pending_submissions.push_back(task);
                    pending_count_.store(pending_submissions.size(), std::memory_order_relaxed);
                }
            }

            // Wake a worker up if they are heavily sleeping
            pending_cv.notify_one();
        }

        /**
         * @brief Submits a batch of root nodes under one injector pass (src/scheduler/README.md).
         */
        void TaskScheduler::submit_batch(const std::vector<Graph::Node*>& roots)
        {
            if (roots.empty()) return;

            size_t normal_added = 0;
            size_t priority_added = 0;
            {
                std::lock_guard<std::mutex> lock(pending_mutex);
                for (Graph::Node* node : roots)
                {
                    if (!node) continue;
                    Task* task = node; // Node derives from Task
                    task->add_ref();
                    if (task->get_priority() > 0)
                    {
                        priority_submissions.push(task);
                        ++priority_added;
                    }
                    else
                    {
                        pending_submissions.push_back(task);
                        ++normal_added;
                    }
                }
                inflight_.fetch_add(static_cast<long long>(normal_added + priority_added),
                                    std::memory_order_relaxed);
                if (normal_added)
                    pending_count_.store(pending_submissions.size(), std::memory_order_relaxed);
                if (priority_added)
                    priority_count_.store(priority_submissions.size(), std::memory_order_relaxed);
            }

            // One notify_all wakes the pool for the whole batch (src/scheduler/README.md).
            pending_cv.notify_all();
        }

        /**
         * @brief Blocks until every task submitted for the run has been reaped.
         *
         * See the header and inflight_. The predicate reads the counter under the
         * same mutex the poller locks before notifying, so a decrement to zero that
         * lands just before this thread parks is never missed.
         */
        void TaskScheduler::wait_quiescent()
        {
            std::unique_lock<std::mutex> lock(quiesce_mutex_);
            quiesce_cv_.wait(lock, [this]
            {
                return inflight_.load(std::memory_order_acquire) <= 0;
            });
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
            return worker_completed_tasks[worker_id].value.load(std::memory_order_relaxed);
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

            // 1. Steal tasks lock-free from the congested (hot) worker.
            std::vector<Task*> rescued_tasks;
            rescued_tasks.reserve(count);

            for (size_t i = 0; i < count; ++i) 
            {
                Task* t = worker_queues[from_worker_id]->steal();
                if (t) rescued_tasks.push_back(t);
                else break; // Stop once the queue is empty.
            }

            // 2. Put the stolen tasks on the global priority queue for cooler workers.
            if (!rescued_tasks.empty()) 
            {
                {
                    std::lock_guard<std::mutex> lock(pending_mutex);
                    for (Task* t : rescued_tasks)
                    {
                        priority_submissions.push(t);
                    }
                    priority_count_.store(priority_submissions.size(), std::memory_order_relaxed);
                }
                // Wake every sleeping or idle cool worker.
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

            // A locally completed node bumps the buffers it wrote (src/scheduler/README.md).
            if (c)
            {
                event_poller.set_completion_observer(
                    [c](Task* t)
                    {
                        if (!t || !t->is_graph_node()) return;
                        auto* n = static_cast<SushiRuntime::Graph::Node*>(t);
                        if (auto* off = n->offload_state())
                            for (void* p : off->writes)
                                c->bump_buffer_version(p);
                    });

                // The hook resubmits an orphan and drops one ref (src/scheduler/README.md).
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

            // A failed remote result re-injects the node (src/scheduler/README.md).
            if (!success)
            {
                SR_LOG_WARN("Offloaded node '{}' failed remotely; re-dispatching.", node->metadata.name);
                // The transferred ref is dropped; submit() took its own (src/scheduler/README.md).
                submit(node);
                node->release();
                return;
            }

            // The write is on the master already; its version advances (src/scheduler/README.md).
            if (coordinator_)
                if (auto* off = node->offload_state())
                    for (void* p : off->writes)
                        coordinator_->bump_buffer_version(p);

            // Ready successors use submit() on this transport thread (src/scheduler/README.md).
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

            // Cross-region boundary successors are released the same way (src/scheduler/README.md).
            for (auto* succ : node->region.boundary_successors)
            {
                if (succ && succ->in_degree.fetch_sub(1, std::memory_order_acq_rel) == 1)
                    submit(succ);
            }

            // The ref the coordinator took at offload is dropped (src/scheduler/README.md).
            node->release();
        }
#endif

    } // namespace Scheduler
} // namespace SushiRuntime