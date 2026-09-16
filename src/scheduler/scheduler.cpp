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
#include <chrono>
#include <cstdint>
#include <SushiRuntime/core/common.hpp>
#include <SushiRuntime/core/error.hpp>
#include <SushiRuntime/core/logger.hpp>
#include <SushiRuntime/core/sycl_compat.hpp>
#include <SushiRuntime/scheduler/task.hpp>
#include <SushiRuntime/topology/affinity.hpp>
#include <SushiRuntime/memory/epoch_reclamation.hpp>
#include <SushiRuntime/scheduler/task_scheduler.hpp>
#include <SushiRuntime/scheduler/work_stealing_deque.hpp>
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
         * @brief Constructs a new TaskScheduler.
         *
         * Sizes and places the worker pool from the context's RuntimeSettings: how
         * many workers to run, which core range to bind them into, and whether to
         * bind at all. The defaults reproduce the sole-tenant behaviour (one worker
         * per logical core, worker i on core i); a host that shares the machine
         * names a narrower range so its own threads keep their cores.
         *
         * @param ctx The RuntimeContext to associate with this scheduler.
         */
        TaskScheduler::TaskScheduler(RuntimeContext& ctx) : context(ctx)
        {
            const Execution::RuntimeSettings& settings = context.settings();

            size_t machine_cores = std::thread::hardware_concurrency();
            if (machine_cores == 0) machine_cores = 1;

            size_t threads = settings.worker_count != 0 ? settings.worker_count : machine_cores;
            if (threads > Core::MAX_WORKER_THREADS) threads = Core::MAX_WORKER_THREADS;
            if (threads == 0) threads = 1;

            num_workers = threads;
            pinning_ = settings.pinning;
            first_core_ = settings.first_core;

            // A range that runs past the last core is worth saying out loud. The OS
            // affinity calls accept an out-of-range mask and simply do not bind, so
            // the failure mode without this is a config that looks like it reserves
            // cores and silently does not — exactly the isolation the caller asked
            // for, quietly absent.
            if (pinning_ == Execution::PinPolicy::Pin && first_core_ + num_workers > machine_cores)
            {
                const size_t last = first_core_ + num_workers;
                first_core_ = first_core_ < machine_cores ? first_core_ : 0;
                if (first_core_ + num_workers > machine_cores)
                    num_workers = machine_cores > first_core_ ? machine_cores - first_core_ : 1;

                SR_LOG_WARN("Worker core range [{}, {}) exceeds the machine's {} cores; "
                            "clamped to {} workers starting at core {}.",
                            settings.first_core, last, machine_cores, num_workers, first_core_);
            }

            for(size_t i=0; i<num_workers; ++i)
            {
                worker_queues[i] = make_sushi<Deque>();
            }

            build_steal_topology();

            // The poller owns the increment/decrement of the run-quiescence counter
            // (it is where tasks are born as successors and reaped on completion).
            event_poller.set_inflight_tracker(&inflight_, &quiesce_mutex_, &quiesce_cv_);

            if (pinning_ == Execution::PinPolicy::Pin)
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
         * @brief Builds the NUMA-aware steal victim table once, at construction.
         *
         * For each worker, all other workers are ordered by ascending NUMA
         * distance from that worker's node (ties broken randomly to spread
         * contention). Node-local victims therefore lead every list, and
         * local_tier_len_ records how many of them there are. find_task_to_steal()
         * then walks the list in order, exhausting the local tier before touching
         * any remote node. If the provider exposes no CPU topology, every worker
         * maps to node 0 → a single flat tier == the original NUMA-blind behaviour.
         *
         * Under PinPolicy::Float a worker has no fixed core and therefore no fixed
         * node, so the same flat single tier is the only honest answer: ordering
         * victims by a distance that does not hold would make stealing worse, not
         * better, by sending a worker to a "local" victim it does not share a node
         * with.
         */
        void TaskScheduler::build_steal_topology()
        {
            const std::vector<int>& core_to_numa = context.cpu_core_to_numa();

            worker_numa_.assign(num_workers, 0);
            if (pinning_ == Execution::PinPolicy::Pin)
            {
                for (size_t i = 0; i < num_workers; ++i)
                {
                    // Worker i is pinned to core (first_core_ + i) — see
                    // dispatch_loop — so its node is that core's node. Unknown or
                    // out-of-range cores fall back to 0.
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
            // Spawn workers exactly once. A second start() would assign over live,
            // joinable std::thread handles (std::terminate) and spin up a second set
            // of owners for the per-worker Chase-Lev deques, silently violating their
            // single-producer invariant.
            bool expected = false;
            if (!threads_started_.compare_exchange_strong(expected, true))
            {
                SR_LOG_WARN("TaskScheduler::start() called more than once; ignoring.");
                return;
            }

            // Workers begin in a waiting state. arm_for_execution() must be called
            // before any work is submitted to signal the pool to begin processing.
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
                       ? context.profiling_queue(index)
                       : context.get_queue(index);
        }

        /**
         * @brief Finds the device index behind an already-selected queue.
         */
        std::size_t TaskScheduler::queue_device_index(sycl::queue& q) const
        {
            const std::size_t n = context.device_count();
            for (std::size_t i = 0; i < n; ++i)
                if (context.get_queue(i) == q)
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

            // Publish this round's plan flag to the poller before workers run, so
            // completion accumulates per-node timing under profiling and stays
            // free of timestamp reads otherwise.
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
            // Signal workers to drain and exit. Under pending_mutex for the same
            // reason arm_for_execution() is — and here a lost wakeup is worse than
            // a late start: with no timeout in the idle wait, a worker that missed
            // the shutdown flag would never be woken again and the join below would
            // never return.
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
            // Lock-free: the shadow counters are the source of truth for emptiness
            // and are only mutated under pending_mutex. This keeps the idle/shutdown
            // liveness checks off the global lock, which the armed-idle pool polls
            // frequently between runs.
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
            
            // Prevent Chase-Lev Deque overflow by refusing to pull more tasks 
            // from the global queue if the local queue is already heavily loaded.
            if (my_queue->size() >= Deque::CAPACITY / 2)
                return;

            // Lock-free fast path: skip the global mutex entirely when the injector
            // is empty (the steady-state case). See pending_count_ for why a stale
            // read is safe.
            if (pending_count_.load(std::memory_order_relaxed) == 0)
                return;

            // Grab a reasonable batch to prevent local deque overflow
            // and ensure fair load distribution among all workers.
            // The batch buffer belongs to this worker and keeps its capacity across
            // drains, so the steady state does not allocate. A local vector here
            // meant one heap allocation per worker per drain — invisible in a batch
            // benchmark, a per-tick allocation on a frame loop.
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

            // Placement is the co-tenancy guarantee: bound to (first_core_ + id), a
            // worker cannot drift onto a core the host reserved for its main, render
            // or audio thread. Float leaves that to the OS.
            if (pinning_ == Execution::PinPolicy::Pin)
                SushiRuntime::Topology::pin_current_thread_to_core(
                    static_cast<int>(first_core_) + thread_id);

            int spin_count = 0;

            // Stall-attribution timing. All reads are gated on profiling_active_, so
            // a non-profiled run takes no clock samples in this hot loop.
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

                // IDLE / waiting-for-arm phase: no work, not yet signalled.
                // The owner's own deque is part of the condition, not an
                // afterthought. Nothing in the wait predicate below looks at it —
                // it cannot, the deques are lock-free and no notify accompanies a
                // push — so a worker that parked holding queued tasks would strand
                // them until something unrelated woke it. This branch used to be
                // unreachable after the first run (the armed flag was never
                // cleared), which is why the omission never showed; disarm() makes
                // it reachable, so the check has to be here.
                if (!is_running && !is_shutdown && active_events.is_empty()
                    && my_queue->size() == 0 && my_queue->overflow_size() == 0
                    && !has_pending_submissions())
                {
                    // Full idle: park until armed, stopped, or given work. No
                    // timeout — every term of this predicate is mutated under
                    // pending_mutex (arm_for_execution, stop, submit,
                    // submit_batch, drain_pending_submissions, the rebalancer's
                    // rescue path), so a state change cannot be missed and a
                    // periodic re-check would find nothing.
                    //
                    // This is the co-tenancy guarantee for a pool that is not
                    // running: a 10 ms poll cost every worker a wakeup and a
                    // pending_mutex acquisition a hundred times a second while the
                    // runtime did nothing at all — cache and lock traffic against
                    // the host's own threads on a machine the runtime is only a
                    // guest on, and enough to keep a laptop out of its deep idle
                    // states. A parked worker now costs exactly nothing.
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

                    // poll_events touches SYCL events (get_info/wait), which can
                    // surface an async or device error. Uncaught, it would escape
                    // this std::thread body and call std::terminate. Route it
                    // through record_run_error like execute_task does: fail the
                    // current run, keep the pool re-armable.
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

                    // Lock-free fast path: only contend for the global mutex when a
                    // priority submission actually exists (the rare case). Priority
                    // bypass is uncommon, so this keeps the per-iteration cost at one
                    // relaxed atomic load instead of a global lock acquisition.
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
                            // The node left the local poll path — it will not be
                            // reaped here — so retire its quiescence count now. Its
                            // remote-completion re-triggers successors through the
                            // ordinary submit()/poll cycle, which counts them afresh.
                            // Any transient dip to zero this causes is harmless: the
                            // sentinel cannot fire (and so nobody waits on the counter)
                            // until every offloaded node's result has landed.
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

                    // It is extremely important for the thread to be outside the Epoch while in the Backoff (Yield/Sleep) phase,
                    // so that a sleeping thread does not stall the global memory reclamation process.
                    spin_count++;
                    const auto bk0 = prof ? sclock::now() : sclock::time_point{};
                    if (spin_count < 100)
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

            // The current run is aborting (a task threw, or it was cancelled): do
            // not execute more kernels. Route the task through the poller flagged
            // ready so its successors still fire and the DAG drains to the sentinel
            // instead of deadlocking on an un-triggered successor. The sentinel
            // itself is exempt — it must run to deliver the outcome and unblock
            // execute().
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

            // A late-bound node that is disabled this step (a sleeping island, an
            // LOD-off system) runs no kernel: route it through the poller flagged
            // ready so its successors fire and the DAG drains exactly as a completed
            // node would. Toggling a subsystem costs no recompile. The relaxed load
            // is safe — the step boundary's completion barrier and the acquire/release
            // submit path supply the happens-before. The sentinel carries no
            // bindings, so it is never skipped here.
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

            // Count the completion against the EXECUTING WORKER's id, not the EBR
            // slot id. EBR ids are handed out in order of first epoch touch across
            // all threads (a non-worker thread that compiles the graph takes slot 0
            // and shifts every worker), so keying by EBR id credited the wrong
            // worker and fed the thermal strategy bogus per-worker rates. The read
            // side (completed_tasks_for) already uses the worker id.
            if (worker_id >= 0 && static_cast<size_t>(worker_id) < Core::MAX_WORKER_THREADS)
                worker_completed_tasks[worker_id].value.fetch_add(1, std::memory_order_relaxed);

            try 
            {
                SR_LOG_DEBUG("Executing task: {}", task->get_name());
                
                int target_numa_id = 0;
                std::vector<sycl::event> deps;
                SushiRuntime::Graph::Node* node = nullptr;

                // Downcast through the virtual is_graph_node() type tag, not RTTI:
                // this runs for every dispatched task, and dynamic_cast here was the
                // one hot-path RTTI query left. EventPolling already uses the same
                // tag, so the two downcast sites now share one strategy.
                if (task->is_graph_node())
                {
                    node = static_cast<SushiRuntime::Graph::Node*>(task);

                    if (node->metadata.numa_affinity >= 0)
                        target_numa_id = node->metadata.numa_affinity;

                    deps = node->sycl_deps.to_vector();
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
                    if (!node->io_writes_.empty())     anchor = node->io_writes_.front().base;
                    else if (!node->io_reads_.empty()) anchor = node->io_reads_.front().base;
                }

                // One-time co-location check: a single kernel cannot dereference USM
                // from two non-peer device contexts, so a node whose regions straddle
                // a device boundary would hit an illegal cross-context access. Fail
                // the run with a precise error instead. The result is cached on the
                // node (dispatched serially across steps, so a plain flag is safe), so
                // steady-state replay pays nothing. Cross-device execution itself is a
                // WP-5 concern (region ownership + halo exchange); until then this
                // enforces the co-location contract the layer above must honour.
                if (node && !node->locality_checked_)
                {
                    const int anchor_ctx = context.context_index_for_ptr(anchor);
                    bool spans = false;
                    if (anchor_ctx >= 0)
                    {
                        auto off_anchor = [&](const std::vector<Core::ResourceRegion>& regs)
                        {
                            for (const auto& r : regs)
                            {
                                if (!r.base) continue;
                                const int c = context.context_index_for_ptr(r.base);
                                if (c >= 0 && c != anchor_ctx) return true;
                            }
                            return false;
                        };
                        spans = off_anchor(node->io_reads_) || off_anchor(node->io_writes_);
                    }
                    if (spans)
                        // Not cached: a still-misconfigured graph must fail again on a
                        // later run, not slip through once the flag is set.
                        throw Core::SushiException(Core::Error{
                            Core::Errc::invalid_graph,
                            "A task's buffers span multiple device contexts; co-locate "
                            "them on one device (cross-device execution is not yet "
                            "supported)."});

                    // Cache the owning-queue index resolved from the same lookup, so
                    // steady-state replay routes via get_queue(index) (a bare
                    // all_queues[] read) instead of get_queue_for_ptr(), which takes a
                    // process-global mutex on every device dispatch. Only the
                    // authoritative map hit (anchor_ctx >= 0) is cached; a map miss
                    // keeps the introspection / round-robin fallback below.
                    node->cached_queue_index_ = anchor_ctx;
                    node->locality_checked_ = true; // co-located; never re-check
                }

                // Every branch resolves an index and funnels through dispatch_queue().
                sycl::queue& q = [&]() -> sycl::queue&
                {
                    if (node && node->cached_queue_index_ >= 0)
                        return dispatch_queue(static_cast<std::size_t>(node->cached_queue_index_));

                    sycl::queue& picked = anchor
                        ? context.get_queue_for_ptr(anchor, target_numa_id)
                        : context.get_numa_local_queue(target_numa_id);

                    // Skips the index lookup entirely on the default (non-profiled) path.
                    if (!profiling_active_.load(std::memory_order_acquire))
                        return picked;

                    const std::size_t idx = queue_device_index(picked);
                    return idx != static_cast<std::size_t>(-1) ? dispatch_queue(idx) : picked;
                }();

                sycl::event e;
                if (task->get_execution_mode() == Task::ExecutionMode::HOST)
                {
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
                        if (!deps.empty()) h.depends_on(deps);
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
                
                // An empty event means the host work already ran inline (synchronous
                // execute_host) — there is nothing to wait on. Route it through the
                // poller flagged ready, so successors fire and the task is released
                // uniformly, without fabricating a barrier event. The old barrier
                // relied on host-op status polling, which AdaptiveCpp does not report.
                //
                // push() never drops: the list spills to the heap past its inline
                // capacity. Dropping an event here would leave the task's successors
                // permanently un-triggered and deadlock the run.
                // ready == "no real event" (inline host work); has_event is its
                // inverse, cached so the poll loop never reconstructs sycl::event{}.
                const bool ready = (e == sycl::event{});
                events.push({e, task, ready, /*has_event=*/!ready,
                            current_dispatch_step_.load(std::memory_order_acquire)});
            } 
            catch (...)
            {
                SR_LOG_ERROR("Task '{}' threw; aborting the current run.", task->get_name());

                // Fail only this run — record the first exception and flip
                // run_failed_ — without latching shutdown_requested, so the pool
                // survives and stays re-armable for the next graph.
                record_run_error(std::current_exception());

                // Route the failed task through the poller as ready so its
                // successors still fire; combined with the run_failed_ skip path
                // above, the whole DAG drains to the sentinel rather than hanging.
                events.push({sycl::event{}, task, true});
            }
        }

        /**
         * @brief Records the first exception of the current run and flips run_failed_.
         *
         * Only the first failing task wins; later exceptions are dropped. The mutex
         * makes the exception write and the flag publish atomic relative to other
         * workers, and its release pairs with the acquire in run_failed()/run_error()
         * so the sentinel sees a fully-written exception.
         * @param ep The exception thrown by the task.
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

            // Count the task into the run before it becomes reachable to workers, so
            // the quiescence counter can never miss it. Balanced by the poller's
            // release on completion (or the offload decrement in Master mode).
            inflight_.fetch_add(1, std::memory_order_relaxed);

            // External submissions go into a dedicated injector queue so we do not
            // violate the single-producer constraint of the per-worker Chase-Lev deques.
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
         * @brief Submits a batch of root nodes under one injector pass.
         *
         * Mirrors submit() exactly per node — add_ref, priority split, quiescence
         * count — but amortizes the fixed cost across the whole root set: one
         * pending_mutex acquisition, a single inflight_ increment, and one wake
         * burst instead of (roots) of each. The inflight_ increment stays inside the
         * locked region, so tasks are counted before any worker can drain them —
         * the same happens-before submit() relies on.
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

            // One wake for the whole batch: roots are independent and there are
            // num_workers threads, so a single notify_all replaces the per-root
            // notify_one storm. Idle workers that find no work simply re-sleep.
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
                    priority_count_.store(priority_submissions.size(), std::memory_order_relaxed);
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
                        if (auto* off = n->offload_state())
                            for (void* p : off->writes)
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
            if (auto* off = node->offload_state())
                for (void* p : off->writes)
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

            // Cross-region boundary successors (dynamic region graph), released the
            // same way; empty for ordinary offloaded nodes.
            for (auto* succ : node->region.boundary_successors)
            {
                if (succ && succ->in_degree.fetch_sub(1, std::memory_order_acq_rel) == 1)
                    submit(succ);
            }

            // Drop the scheduling reference that was transferred to the
            // coordinator when the node was offloaded.
            node->release();
        }
#endif

    } // namespace Scheduler
} // namespace SushiRuntime