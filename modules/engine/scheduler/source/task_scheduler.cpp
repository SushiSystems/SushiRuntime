/**************************************************************************/
/* task_scheduler.cpp                                                     */
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
 * @file task_scheduler.cpp
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
#include <SushiRuntime/core/result.hpp>
#include <SushiRuntime/log/logger.hpp>
#include <SushiRuntime/profile/profile.hpp>
#include <SushiRuntime/report/node_event_record.hpp>
#include <SushiRuntime/scheduler/scheduler_log_category.hpp>
#include <SushiRuntime/sycl/error_translation.hpp>
#include <SushiRuntime/sycl/sycl_compat.hpp>
#include <SushiRuntime/task/dispatch_events.hpp>
#include <SushiRuntime/task/task.hpp>
#include <SushiRuntime/topology/affinity.hpp>
#include <SushiRuntime/topology/topology_view.hpp>
#include <SushiRuntime/device/device_queue_set.hpp>
#include <SushiRuntime/device/queue_flavour.hpp>
#include <SushiRuntime/memory/usm_allocator.hpp>
#include <SushiRuntime/memory/epoch_reclamation.hpp>
#include <SushiRuntime/scheduler/task_scheduler.hpp>
#include <SushiRuntime/scheduler/failure_naming.hpp>
#include "dispatch_placement.hpp"
#include <SushiRuntime/work_stealing/work_stealing_deque.hpp>
#include <SushiRuntime/scheduler/worker_pool_settings.hpp>
#include <SushiRuntime/task/node.hpp>
#include <SushiRuntime/task/node_placement.hpp>
#include <SushiRuntime/task/node_widths.hpp>
#include <SushiRuntime/task/offload_coordinator.hpp>
#include <SushiRuntime/task/offload_state.hpp>

namespace SushiRuntime
{
    namespace Internal::Scheduler
    {
        using namespace SushiRuntime;

        namespace
        {
            /** @brief NUMA node whose devices serve a task that is not a graph node. */
            constexpr int K_TASK_NUMA_NODE = 0;

            /**
             * @brief Builds the ready event of a task whose work did not run to its end.
             * @return An event the poller reaps at once and reports to no offload coordinator.
             */
            EventPolling::PolledEvent skipped_event(Task* task)
            {
                EventPolling::PolledEvent skipped;
                skipped.task = task;
                skipped.ready = true;
                skipped.executed = false;
                return skipped;
            }
        } // namespace

        /**
         * @brief Sizes the worker pool from the requested settings (modules/engine/scheduler/README.md).
         */
        TaskScheduler::TaskScheduler(const WorkerPoolSettings& pool,
                                     const Internal::Topology::TopologyView& topology,
                                     Internal::Device::DeviceQueueSet& devices,
                                     const Internal::Memory::USMAllocator& memory)
            : topology_(topology), devices_(devices), memory_(memory)
        {
            size_t machine_cores = std::thread::hardware_concurrency();
            if (machine_cores == 0) machine_cores = 1;

            size_t threads = pool.worker_count != 0 ? pool.worker_count : machine_cores;
            if (threads > K_MAX_WORKER_THREADS)
            {
                SR_LOG_WARN(K_SCHEDULER_LOG_CATEGORY, "Worker count {} exceeds the maximum of {}; the pool runs {} workers.",
                            threads, K_MAX_WORKER_THREADS, K_MAX_WORKER_THREADS);
                threads = K_MAX_WORKER_THREADS;
            }
            if (threads == 0)
            {
                SR_LOG_WARN(K_SCHEDULER_LOG_CATEGORY, "Worker count resolved to 0; the pool runs 1 worker.");
                threads = 1;
            }

            num_workers_ = threads;
            pinning_ = pool.pinning;
            first_core_ = pool.first_core;

            // A core range past the last core is clamped and logged (modules/engine/scheduler/README.md).
            if (pinning_ == PinPolicy::Pin && first_core_ + num_workers_ > machine_cores)
            {
                const size_t last = first_core_ + num_workers_;
                first_core_ = first_core_ < machine_cores ? first_core_ : 0;
                if (first_core_ + num_workers_ > machine_cores)
                    num_workers_ = machine_cores > first_core_ ? machine_cores - first_core_ : 1;

                SR_LOG_WARN(K_SCHEDULER_LOG_CATEGORY, "Worker core range [{}, {}) exceeds the machine's {} cores; "
                            "clamped to {} workers starting at core {}.",
                            pool.first_core, last, machine_cores, num_workers_, first_core_);
            }

            for(size_t i=0; i<num_workers_; ++i)
            {
                worker_queues[i] = make_sushi<Deque>();
            }

            build_steal_topology();

            // The poller owns the run-quiescence counter's updates (modules/engine/scheduler/README.md).
            event_poller.set_inflight_tracker(&quiescence_);

            if (pinning_ == PinPolicy::Pin)
            {
                SR_LOG_INFO(K_SCHEDULER_LOG_CATEGORY, "Task scheduler initialized with {} workers pinned to cores [{}, {}).",
                            num_workers_, first_core_, first_core_ + num_workers_);
            }
            else
            {
                SR_LOG_INFO(K_SCHEDULER_LOG_CATEGORY, "Task scheduler initialized with {} unpinned workers.", num_workers_);
            }
        }

        /**
         * @brief Builds the NUMA-aware steal victim table once (modules/engine/scheduler/README.md).
         */
        void TaskScheduler::build_steal_topology()
        {
            const std::vector<int>& core_to_numa = topology_.core_to_numa();

            worker_numa_.assign(num_workers_, 0);
            if (pinning_ == PinPolicy::Pin)
            {
                for (size_t i = 0; i < num_workers_; ++i)
                {
                    // Worker i is pinned to core first_core_ + i; an unknown core maps to node 0.
                    const size_t core = first_core_ + i;
                    if (core < core_to_numa.size())
                        worker_numa_[i] = core_to_numa[core];
                    else
                        SR_LOG_DEBUG(K_SCHEDULER_LOG_CATEGORY, "Worker {} is pinned to core {}, which the topology "
                                     "does not list; it is assigned to NUMA node 0.", i, core);
                }
            }

            victim_order_.assign(num_workers_, {});
            local_tier_len_.assign(num_workers_, 0);

            std::mt19937 rng(0x51515151u);
            for (size_t th = 0; th < num_workers_; ++th)
            {
                const int my_node = worker_numa_[th];

                struct Victim { int id; int dist; uint64_t key; };
                std::vector<Victim> victims;
                victims.reserve(num_workers_ > 0 ? num_workers_ - 1 : 0);

                int local = 0;
                for (size_t j = 0; j < num_workers_; ++j)
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
            // Workers are spawned exactly once (modules/engine/scheduler/README.md).
            bool expected = false;
            if (!threads_started_.compare_exchange_strong(expected, true))
            {
                SR_LOG_WARN(K_SCHEDULER_LOG_CATEGORY, "TaskScheduler::start() called more than once; ignoring.");
                return;
            }

            // Workers wait; arm_for_execution() precedes submitted work (modules/engine/scheduler/README.md).
            for(size_t i=0; i<num_workers_; ++i)
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
                       ? devices_.queue(index, Internal::Device::QueueFlavour{false, true})
                       : devices_.queue(index);
        }

        /**
         * @brief Finds the device index behind an already-selected queue.
         */
        std::size_t TaskScheduler::queue_device_index(sycl::queue& queue) const
        {
            const std::size_t n = devices_.count();
            for (std::size_t i = 0; i < n; ++i)
                if (devices_.queue(i) == queue)
                    return i;
            return static_cast<std::size_t>(-1);
        }

        /**
         * @brief Wires the plan's event ledger for the round about to be armed.
         */
        void TaskScheduler::set_profiling_ledger(Internal::Observability::ProfilingLedger* ledger) noexcept
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
            outcome_.clear_failure();

            // The poller gets the profiling flag before any worker runs (modules/engine/scheduler/README.md).
            event_poller.set_profiling(profiling);
            profiling_active_.store(profiling, std::memory_order_release);

            // The flag is one term of the idle wait predicate, so it is set under pending_mutex.
            {
                SR_LOCK_WAIT("lock.pending", pending_mutex);
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
            log_run_tail();
            SR_LOCK_WAIT("lock.pending", pending_mutex);
            running.store(false, std::memory_order_release);
        }

        /**
         * @brief Logs the tasks the run skipped and the polls that waited on their oldest event.
         */
        void TaskScheduler::log_run_tail() noexcept
        {
            const std::uint64_t aborted = skipped_aborted_.exchange(0, std::memory_order_relaxed);
            const std::uint64_t disabled = skipped_disabled_.exchange(0, std::memory_order_relaxed);
            const std::uint64_t stall_drives = event_poller.take_stall_drives();

            if (aborted != 0 && outcome_.is_cancelled())
                SR_LOG_INFO(K_SCHEDULER_LOG_CATEGORY, "The run was cancelled; {} tasks were skipped without running.",
                            aborted);
            else if (aborted != 0)
                SR_LOG_DEBUG(K_SCHEDULER_LOG_CATEGORY, "The run failed; {} tasks were skipped without running.",
                             aborted);
            if (disabled != 0)
                SR_LOG_DEBUG(K_SCHEDULER_LOG_CATEGORY, "{} tasks were skipped because their control was off.",
                             disabled);
            if (stall_drives != 0)
                SR_LOG_DEBUG(K_SCHEDULER_LOG_CATEGORY, "{} polls waited on their oldest event after stalling.",
                             stall_drives);
        }

        /**
         * @brief Stops the task scheduler.
         * Signals workers to drain remaining tasks and exit, then joins threads.
         */
        void TaskScheduler::stop()
        {
            // The flags change under pending_mutex (modules/engine/scheduler/README.md).
            {
                SR_LOCK_WAIT("lock.pending", pending_mutex);
                shutdown_requested.store(true, std::memory_order_release);
                running.store(false, std::memory_order_release);
            }
            pending_cv.notify_all();
            for (size_t i = 0; i < num_workers_; ++i)
                if (workers[i].joinable()) workers[i].join();   


            for (size_t i = 0; i < num_workers_; ++i)
            {
                if (worker_queues[i])
                {
                    while (Task* task = worker_queues[i]->pop())
                        task->release();
                }
            }

            // Release any submissions that never made it into execution.
            SR_LOCK_WAIT("lock.pending", pending_mutex);
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
            // stop() is idempotent, so the destructor may call it too (modules/engine/scheduler/README.md).
            stop();
        }

        /**
         * @brief Checks if there are any pending submissions in the global queue.
         * @return true if there are pending tasks, false otherwise.
         */
        bool TaskScheduler::has_pending_submissions()
        {
            // The shadow counters are read without the lock (modules/engine/scheduler/README.md).
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
            if (thread_id < 0 || static_cast<size_t>(thread_id) >= num_workers_)
                return;

            auto& my_queue = worker_queues[thread_id];
            
            // A half-full local deque pulls nothing from the injector (modules/engine/scheduler/README.md).
            if (my_queue->size() >= Deque::K_CAPACITY / 2)
                return;

            // An empty injector skips the mutex; a stale read is safe (modules/engine/scheduler/README.md).
            if (pending_count_.load(std::memory_order_relaxed) == 0)
                return;

            // The batch buffer is per worker and keeps its capacity (modules/engine/scheduler/README.md).
            constexpr size_t BATCH_SIZE = 4096;
            std::vector<Task*>& batch = drain_scratch_[thread_id];
            batch.clear();

            {
                SR_LOCK_WAIT("lock.pending", pending_mutex);
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
         * @brief Runs a worker's idle, active and drain phases (modules/engine/scheduler/README.md).
         * @param thread_id The ID of the worker executing this loop.
         */
        void TaskScheduler::dispatch_loop(int thread_id) 
        {
            if (static_cast<size_t>(thread_id) >= num_workers_) 
            {
                SR_LOG_ERROR(K_SCHEDULER_LOG_CATEGORY, "Dispatch loop thread_id {} out of bounds!", thread_id);
                return;
            }

            // The thread is registered first, so its first zone allocates nothing.
            Internal::Profile::name_this_thread("sr-worker-" + std::to_string(thread_id));

            auto& my_queue = worker_queues[thread_id];
            EventPolling::ActiveEventList active_events;

            // A pinned worker stays on core first_core_ + id (modules/engine/scheduler/README.md).
            if (pinning_ == PinPolicy::Pin)
                SushiRuntime::Internal::Topology::pin_current_thread_to_core(
                    static_cast<int>(first_core_) + thread_id);

            int spin_count = 0;

            // Clock reads are gated on profiling_active_ (modules/engine/scheduler/README.md).
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

                // A worker parks only with an empty deque of its own (modules/engine/scheduler/README.md).
                if (!is_running && !is_shutdown && active_events.is_empty()
                    && my_queue->size() == 0 && my_queue->overflow_size() == 0
                    && !has_pending_submissions())
                {
                    // The park has no timeout (modules/engine/scheduler/README.md).
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
                    SushiRuntime::Internal::Memory::EpochGuard epoch_guard;

                    // A failed poll fails the run, not the thread (scheduler/README.md).
                    std::optional<Error> polled;
                    Result<void> poll_ran = SYCL::translate_exceptions([&]()
                    {
                        if (prof)
                        {
                            const auto p0 = sclock::now();
                            polled = event_poller.poll_events(active_events, *my_queue);
                            add_ns(wt.polling_ns, p0, sclock::now());
                        }
                        else
                            polled = event_poller.poll_events(active_events, *my_queue);
                    });
                    if (!poll_ran)
                    {
                        SR_LOG_ERROR(K_SCHEDULER_LOG_CATEGORY, "poll_events threw {}: {}; the current run is aborted.",
                                     to_string(poll_ran.error().code), poll_ran.error().message);
                        outcome_.record(std::move(poll_ran.error()));
                    }
                    else if (polled)
                    {
                        SR_LOG_ERROR(K_SCHEDULER_LOG_CATEGORY, "An event of the current run failed with {}: {}; the run is aborted.",
                                     to_string(polled->code), polled->message);
                        outcome_.record(std::move(*polled));
                    }

                    // Only a priority submission takes the global mutex (modules/engine/scheduler/README.md).
                    if (priority_count_.load(std::memory_order_relaxed) > 0)
                    {
                        SR_LOCK_WAIT("lock.pending", pending_mutex);
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

                        // An offloaded task is owned by the coordinator (modules/engine/scheduler/README.md).
                        const bool offloaded = offload_gate_.offer(*task, outcome_);
                        if (!offloaded)
                        {
                            const auto b0 = prof ? sclock::now() : sclock::time_point{};
                            execute_task(task, thread_id, active_events);
                            if (prof) add_ns(wt.busy_ns, b0, sclock::now());
                        }
                        else
                        {
                            // Its quiescence count retires here (modules/engine/scheduler/README.md).
                            quiescence_.retire();
                        }
                    }
                }

                if (!task) 
                {
                    if (is_shutdown && active_events.is_empty() && !has_pending_submissions())
                        break;

                    // Backoff runs outside the epoch (modules/engine/scheduler/README.md).
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

            SR_ZONE_FINE("dispatch.task");

            // An aborting run executes no more kernels (modules/engine/scheduler/README.md).
            if (outcome_.is_aborting())
            {
                bool is_sentinel = task->is_graph_node() &&
                                   static_cast<SushiRuntime::Internal::Graph::Node*>(task)->is_sentinel;
                if (!is_sentinel)
                {
                    skipped_aborted_.fetch_add(1, std::memory_order_relaxed);
                    events.push(skipped_event(task));
                    return;
                }
            }

            // A disabled late-bound node runs no kernel (modules/engine/scheduler/README.md).
            if (task->is_graph_node())
            {
                auto* gnode = static_cast<SushiRuntime::Internal::Graph::Node*>(task);
                if (gnode->bindings_ &&
                    !gnode->bindings_->enabled.load(std::memory_order_relaxed))
                {
                    skipped_disabled_.fetch_add(1, std::memory_order_relaxed);
                    events.push(skipped_event(task));
                    return;
                }
            }

            // Completions are keyed by worker id, not EBR slot id (modules/engine/scheduler/README.md).
            if (worker_id >= 0 && static_cast<size_t>(worker_id) < K_MAX_WORKER_THREADS)
                worker_completed_tasks[worker_id].value.fetch_add(1, std::memory_order_relaxed);

            const auto abort_run = [this, task, &events](Error failure)
            {
                SR_LOG_ERROR(K_SCHEDULER_LOG_CATEGORY, "{}; the run is aborted and its remaining tasks are skipped.",
                             failure.message);

                // Only this run fails; the pool stays re-armable (modules/engine/scheduler/README.md).
                outcome_.record(std::move(failure));

                // The failed task is pushed as ready so successors fire (modules/engine/scheduler/README.md).
                events.push(skipped_event(task));
            };

            Result<void> ran = SYCL::translate_exceptions([&]()
            {
                SR_LOG_TRACE(K_SCHEDULER_LOG_CATEGORY, "Executing task: {}", task->name());
                
                SushiRuntime::Internal::Graph::Node* node = nullptr;

                // The downcast uses the is_graph_node() tag, not RTTI (modules/engine/scheduler/README.md).
                if (task->is_graph_node())
                    node = static_cast<SushiRuntime::Internal::Graph::Node*>(task);

                // A pure-CPU host task needs no queue and runs inline (see design spec §0).
                if (node && node->execution_mode() == Task::ExecutionMode::Host &&
                    !node->submits_device_work())
                {
                    // Timing follows this run's plan flag, not the context-wide setting.
                    if (profiling_active_.load(std::memory_order_acquire))
                    {
                        // Captured now, not read at reap, so a later round's step cannot land on this record.
                        const std::uint32_t step = current_dispatch_step_.load(std::memory_order_acquire);
                        const auto h0 = std::chrono::steady_clock::now();
                        {
                            SR_ZONE_FINE("dispatch.host");
                            node->execute_native();
                        }
                        const auto h1 = std::chrono::steady_clock::now();
                        const std::uint64_t h0_ns = static_cast<std::uint64_t>(
                            std::chrono::duration_cast<std::chrono::nanoseconds>(
                                h0.time_since_epoch()).count());
                        const std::uint64_t h1_ns = static_cast<std::uint64_t>(
                            std::chrono::duration_cast<std::chrono::nanoseconds>(
                                h1.time_since_epoch()).count());

                        // Raw steady times, on the host device; the observation carries them as they are.
                        if (auto* ledger = active_ledger_.load(std::memory_order_acquire))
                        {
                            // A host node submits no dispatch: the widths stay K_WIDTH_UNKNOWN and the device is the host.
                            Internal::Observability::DispatchFacts facts;
                            facts.node_index = node->registration_index;
                            facts.invocation = step;
                            facts.submit_thread = Internal::Profile::current_thread_index();
                            ledger->record(facts, Internal::Observability::DispatchTimes{h0_ns, h0_ns, h1_ns});
                        }
                    }
                    else
                    {
                        node->execute_native();
                    }
                    EventPolling::PolledEvent host_done;
                    host_done.task = task;
                    host_done.ready = true;
                    events.push(host_done);
                    return;
                }

                // A node is placed once, at its first dispatch (modules/engine/scheduler/README.md).
                if (node && !node->locality_checked_)
                {
                    const Result<void> placed =
                        place_for_dispatch(*node, memory_, devices_.count());
                    if (!placed)
                    {
                        abort_run(name_failure(*task, placed.error()));
                        return;
                    }
                }

                // Every branch resolves an index and funnels through dispatch_queue().
                std::uint32_t dispatch_device = K_HOST_DEVICE_INDEX;
                sycl::queue& queue = [&]() -> sycl::queue&
                {
                    // The first dispatch of a node cached its placement in the block above.
                    if (node)
                    {
                        dispatch_device = static_cast<std::uint32_t>(node->cached_queue_index_);
                        return dispatch_queue(static_cast<std::size_t>(node->cached_queue_index_));
                    }

                    sycl::queue& picked =
                        devices_.queue_or_numa_local(std::nullopt, K_TASK_NUMA_NODE);

                    // Skips the index lookup entirely on the default (non-profiled) path.
                    if (!profiling_active_.load(std::memory_order_acquire))
                        return picked;

                    const std::size_t idx = queue_device_index(picked);
                    if (idx != static_cast<std::size_t>(-1))
                    {
                        dispatch_device = static_cast<std::uint32_t>(idx);
                        return dispatch_queue(idx);
                    }

                    // The queue is not in the device set, so the dispatch has no profiling queue to time on.
                    if (auto* ledger = active_ledger_.load(std::memory_order_acquire))
                        ledger->note_drop(DropCause::NoQueue,
                                          DropCounts::K_NO_NODE);
                    return picked;
                }();

                sycl::event e;
                std::vector<sycl::event> handed_over;
                if (task->execution_mode() == Task::ExecutionMode::Host)
                {
                    // execute_host() needs a vector; this path allocates (modules/engine/scheduler/README.md).
                    std::vector<sycl::event> dependencies;
                    if (node)
                    {
                        dependencies.reserve(node->sycl_deps.size());
                        for (const auto& ev : node->sycl_deps)
                            dependencies.push_back(ev);
                    }
                    SR_ZONE_FINE("dispatch.submit");
                    if (profiling_active_.load(std::memory_order_acquire))
                    {
                        // A body that submits several kernels hands each event over (api/graph/dispatch_events.hpp).
                        Internal::DispatchEvents::DispatchEventScope handing(handed_over);
                        e = task->execute_host(queue, dependencies);
                    }
                    else
                    {
                        e = task->execute_host(queue, dependencies);
                    }
                }
                else
                {
                    // The first device submission to reach here claims the run's clock pairing.
                    Internal::Observability::ProfilingLedger* pairing_ledger = nullptr;
                    std::chrono::steady_clock::time_point pairing_t0;
                    if (profiling_active_.load(std::memory_order_acquire))
                    {
                        if (auto* ledger = active_ledger_.load(std::memory_order_acquire))
                            if (ledger->claim_pairing_owner(dispatch_device, task))
                            {
                                pairing_ledger = ledger;
                                pairing_t0 = std::chrono::steady_clock::now();
                            }
                    }

                    {
                        SR_ZONE_FINE("dispatch.submit");
                        e = queue.submit([&](sycl::handler& h)
                        {
                            // depends_on() per event builds no heap vector (modules/engine/scheduler/README.md).
                            if (node)
                                for (const auto& ev : node->sycl_deps)
                                    h.depends_on(ev);
                            task->execute(h);
                        });
                    }

                    if (pairing_ledger)
                    {
                        const auto pairing_t1 = std::chrono::steady_clock::now();
                        const std::uint64_t steady_ns = static_cast<std::uint64_t>(
                            std::chrono::duration_cast<std::chrono::nanoseconds>(
                                pairing_t0.time_since_epoch()).count());
                        const std::uint64_t uncertainty_ns = static_cast<std::uint64_t>(
                            std::chrono::duration_cast<std::chrono::nanoseconds>(
                                pairing_t1 - pairing_t0).count());
                        pairing_ledger->set_pairing_submit_window(dispatch_device, steady_ns, uncertainty_ns);
                    }
                }
                
                // An empty event is pushed as ready, never dropped (modules/engine/scheduler/README.md).
                const bool ready = (e == sycl::event{});
                const SushiRuntime::Internal::Graph::NodeWidths widths =
                    (node && profiling_active_.load(std::memory_order_acquire))
                        ? SushiRuntime::Internal::Graph::snapshot_widths(*node)
                        : SushiRuntime::Internal::Graph::NodeWidths{};
                EventPolling::PolledEvent dispatched;
                dispatched.event = e;
                dispatched.task = task;
                dispatched.ready = ready;
                dispatched.has_event = !ready;
                dispatched.step = current_dispatch_step_.load(std::memory_order_acquire);
                dispatched.widths = widths;
                dispatched.handed_over = std::move(handed_over);
                dispatched.device_index = dispatch_device;
                if (profiling_active_.load(std::memory_order_acquire))
                    dispatched.submit_thread = Internal::Profile::current_thread_index();
                events.push(dispatched);
            });
            if (!ran)
                abort_run(name_failure(*task, ran.error()));
        }

        /**
         * @brief Returns the accumulated time split for a worker.
         */
        TaskScheduler::WorkerTimeSplit
        TaskScheduler::worker_times(int worker_id) const noexcept
        {
            WorkerTimeSplit s;
            if (worker_id < 0 || static_cast<size_t>(worker_id) >= num_workers_)
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
            for (size_t i = 0; i < K_MAX_WORKER_THREADS; ++i)
            {
                worker_time_[i].busy_ns.store(0, std::memory_order_relaxed);
                worker_time_[i].stealing_ns.store(0, std::memory_order_relaxed);
                worker_time_[i].polling_ns.store(0, std::memory_order_relaxed);
                worker_time_[i].idle_ns.store(0, std::memory_order_relaxed);
            }
        }

        /**
         * @brief Steals a task, node-local victims first (modules/engine/scheduler/README.md).
         * @param th_id The ID of the worker attempting to steal.
         * @return Pointer to a stolen task, or nullptr if none found.
         */
        Task* TaskScheduler::find_task_to_steal(int th_id)
        {
            if (th_id < 0 || static_cast<size_t>(th_id) >= num_workers_)
                return nullptr;

            SR_ZONE_DETAIL("worker.steal");

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

            // The task is counted before workers can reach it (modules/engine/scheduler/README.md).
            quiescence_.add();

            // External submissions use the injector, not a worker deque (modules/engine/scheduler/README.md).
            {
                SR_LOCK_WAIT("lock.pending", pending_mutex);
                if (task->priority() > 0)
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
         * @brief Submits a batch of root nodes under one injector pass (modules/engine/scheduler/README.md).
         */
        void TaskScheduler::submit_batch(const std::vector<Internal::Graph::Node*>& roots)
        {
            if (roots.empty()) return;

            size_t normal_added = 0;
            size_t priority_added = 0;
            {
                SR_LOCK_WAIT("lock.pending", pending_mutex);
                for (Internal::Graph::Node* node : roots)
                {
                    if (!node) continue;
                    Task* task = node; // Node derives from Task
                    task->add_ref();
                    if (task->priority() > 0)
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
                quiescence_.add(normal_added + priority_added);
                if (normal_added)
                    pending_count_.store(pending_submissions.size(), std::memory_order_relaxed);
                if (priority_added)
                    priority_count_.store(priority_submissions.size(), std::memory_order_relaxed);
            }

            // One notify_all wakes the pool for the whole batch (modules/engine/scheduler/README.md).
            pending_cv.notify_all();
        }

        /**
         * @brief Blocks until every task counted into the run has been reaped or offloaded.
         *
         * @see modules/engine/scheduler/README.md
         */
        void TaskScheduler::wait_quiescent()
        {
            quiescence_.wait();
        }

        /**
         * @brief Gets the number of worker threads.
         * @return The number of workers.
         */
        size_t TaskScheduler::worker_count() const 
        {
            return num_workers_;
        }

        /**
         * @brief Gets the current size of a worker's local queue.
         * @param worker_id The ID of the worker.
         * @return The number of tasks in the worker's queue.
         */
        size_t TaskScheduler::queue_size(int worker_id) const 
        {
            if (worker_id < 0 || worker_id >= static_cast<int>(num_workers_)) return 0;
            if (worker_queues[worker_id]) return worker_queues[worker_id]->size();
            return 0;
        }

        /**
         * @brief Gets the total number of tasks completed by a worker.
         * @param worker_id The ID of the worker.
         * @return The number of completed tasks.
         */
        size_t TaskScheduler::completed_task_count(int worker_id) const 
        {
            if (worker_id < 0 || worker_id >= static_cast<int>(num_workers_)) return 0;
            return worker_completed_tasks[worker_id].value.load(std::memory_order_relaxed);
        }

        /**
         * @brief Moves tasks from a congested worker onto the global priority queue.
         * @param from_worker_id The ID of the heavily loaded worker.
         * @param count The number of tasks to migrate.
         */
        void TaskScheduler::migrate_tasks(int from_worker_id, size_t count) 
        {
            if (from_worker_id < 0 || from_worker_id >= static_cast<int>(num_workers_)) return;
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
                    SR_LOCK_WAIT("lock.pending", pending_mutex);
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

        /**
         * @brief Installs the offload coordinator in the scheduler and its poller, and binds it.
         * @param offload Not owned; null removes the installed one.
         */
        void TaskScheduler::set_offload_coordinator(Internal::Graph::OffloadCoordinator* offload)
        {
            // The coordinator holds its sink before a worker can offer it a node.
            if (offload != nullptr)
                offload->bind(*this);

            offload_gate_.install(offload);
            event_poller.set_offload_coordinator(offload);
        }

        void TaskScheduler::prepare_offload(span<const sushi_ptr<Internal::Graph::Node>> nodes,
                                            const std::shared_ptr<const PlacementRules>& rules)
        {
            offload_gate_.prepare(nodes, rules);
        }

    } // namespace Scheduler
} // namespace SushiRuntime