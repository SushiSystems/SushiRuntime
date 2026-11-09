/**************************************************************************/
/* task_scheduler.hpp                                                     */
/**************************************************************************/
/*                          This file is part of:                         */
/*                              SushiRuntime                              */
/*              https://github.com/SushiSystems/SushiRuntime              */
/*                         https://sushisystems.io                        */
/**************************************************************************/
/* Copyright (c) 2026-present Mustafa Garip & Sushi Systems               */
/*                                                                        */
/* Licensed under the Apache License, Version 2.0 (the "License"); you    */
/* may not use this file except in compliance with the License. You may   */
/* obtain a copy of the License at                                        */
/*                                                                        */
/*     https://www.apache.org/licenses/LICENSE-2.0                        */
/*                                                                        */
/* Unless required by applicable law or agreed to in writing, software    */
/* distributed under the License is distributed on an "AS IS" BASIS,      */
/* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or        */
/* implied. See the License for the specific language governing           */
/* permissions and limitations under the License.                         */
/**************************************************************************/

/**
 * @file task_scheduler.hpp
 * @brief Declares TaskScheduler, the NUMA-aware work-stealing scheduler.
 * @author Mustafa Garip
 * @see modules/engine/scheduler/README.md
 */

#pragma once


#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <atomic>
#include <thread>
#include <vector>
#include <queue>
#include <exception>
#include <sycl/sycl.hpp>
#include <condition_variable>
#include <SushiRuntime/core/common.hpp>
#include <SushiRuntime/core/executor.hpp>
#include <SushiRuntime/core/export.hpp>
#include <SushiRuntime/scheduler/rebalancer_interfaces.hpp>
#include <SushiRuntime/scheduler/event_polling.hpp>
#include <SushiRuntime/scheduler/offload_gate.hpp>
#include <SushiRuntime/machine/pin_policy.hpp>
#include <SushiRuntime/scheduler/quiescence_latch.hpp>
#include <SushiRuntime/profile/profile.hpp>
#include <SushiRuntime/scheduler/run_outcome.hpp>
#include <SushiRuntime/scheduler/worker_pool_settings.hpp>
#include <SushiRuntime/task/offload_sink.hpp>

namespace SushiRuntime
{
    /** @brief Declares the graph types this header names only by pointer or reference. */
    namespace Internal::Graph
    {
        class TaskGraph;
        struct Node;
        class OffloadCoordinator;
    }

    namespace Internal::Topology { class TopologyView; }
    namespace Internal::Device { class DeviceQueueSet; }
    namespace Internal::Memory { class USMAllocator; }

    namespace Internal::Scheduler 
    {
        class Deque;
        class Task;

        /**
         * @brief Schedules tasks on a persistent, NUMA-aware work-stealing worker pool.
         * @see modules/engine/scheduler/README.md
         */
        class SUSHIRUNTIME_API TaskScheduler final : public TelemetryProvider,
                                                     public TaskMigrator,
                                                     public Internal::Graph::OffloadSink
        {
            public:
                /**
                 * @brief Returns the first core of the range workers are bound into, as applied.
                 * @return The first bound core index; meaningless when pinning() is Float.
                 * @see modules/engine/scheduler/README.md
                 */
                std::size_t first_core() const noexcept;

                /**
                 * @brief Whether workers are bound to cores or left to the OS.
                 * @return The pin policy in effect for this scheduler's workers.
                 */
                PinPolicy pinning() const noexcept;

                /**
                 * @brief Sizes the worker pool and builds its steal order; starts no thread.
                 * @param pool Copied; the range it asks for is clamped to the machine's cores.
                 * @param topology Must outlive the scheduler.
                 * @param devices Must outlive the scheduler.
                 * @param memory Must outlive the scheduler.
                 * @param collector Takes the dispatch and steal zones; must outlive the scheduler.
                 */
                TaskScheduler(const WorkerPoolSettings& pool,
                              const Internal::Topology::TopologyView& topology,
                              Internal::Device::DeviceQueueSet& devices,
                              const Internal::Memory::USMAllocator& memory,
                              Profile::Collector& collector = Profile::process_collector());
                /** @brief Cleans up the scheduler and worker queues. */
                ~TaskScheduler();

                /**
                 * @brief Runs the worker rounds as jobs of @p executor instead of on threads.
                 *
                 * Called before start(). Each job runs one round of one worker and posts the next
                 * while that worker has work, so a SeededExecutor steps the scheduler on the test's
                 * thread. stop() runs the rounds left on the calling thread.
                 * @param executor Must outlive the scheduler; null keeps the threads.
                 */
                void set_executor(Executor* executor) noexcept;

                /**
                 * @brief Starts the persistent background worker threads.
                 *
                 * Must be called exactly once after construction. Workers remain
                 * alive across multiple graph execution rounds.
                 */
                void start();

                /**
                 * @brief Arms the scheduler for a new execution round.
                 * @pre Called before the first root node of each round is submitted.
                 * @param profiling The round's plan flag; device nodes go to the profiling queue
                 *        while set.
                 * @see modules/engine/scheduler/README.md
                 */
                void arm_for_execution(bool profiling = false);

                /**
                 * @brief Wires the plan's event ledger for the round about to be armed.
                 * @pre The CompiledGraph that owns the ledger outlives the window up to disarm().
                 * @param ledger The plan's ledger, or null when it does not profile.
                 * @see modules/engine/scheduler/README.md
                 */
                void set_profiling_ledger(Internal::Observability::ProfilingLedger* ledger) noexcept;

                /** @brief Resets the per-run step counter, to 0. */
                void reset_dispatch_step() noexcept;

                /** @brief Draws this round's step; call exactly once per root-injecting submit(), not per arm. */
                void advance_dispatch_step() noexcept;

                /** @brief The step this round's dispatches must stamp into their event records. */
                std::uint32_t current_dispatch_step() const noexcept;

                /**
                 * @brief Returns the pool to its parked state after a run quiesces.
                 * @pre The run has quiesced; SchedulerExecutionEngine::complete() calls this after
                 *        wait_quiescent().
                 * @see modules/engine/scheduler/README.md
                 */
                void disarm() noexcept;

                /**
                 * @brief Signals workers to stop and waits for all threads to join.
                 *
                 * Called once when the owning engine is being destroyed. After this
                 * returns, no worker thread touches the queue set or the allocator,
                 * so both can be freed safely.
                 */
                void stop();

                /** @brief Submits a new task to the injector queue. */
                void submit(Task* task);

                /**
                 * @brief Submits a batch of root nodes in a single injector pass.
                 * @see modules/engine/scheduler/README.md
                 */
                void submit_batch(const std::vector<SushiRuntime::Internal::Graph::Node*>& roots);

                /**
                 * @brief Blocks until every task counted into the current run has retired: reaped
                 *        by the poller, or handed to the offload coordinator.
                 * @see modules/engine/scheduler/README.md
                 */
                void wait_quiescent();

                /**
                 * @brief Reports whether a task failed during the current run.
                 *
                 * Read by the completion sentinel to deliver the error to the caller
                 * instead of success. Cleared each round by arm_for_execution().
                 * @return True if the current run has failed.
                 */
                bool run_failed() const;

                /**
                 * @brief Returns the first failure recorded during the current run.
                 *
                 * The value is visible once run_failed() reads true.
                 * @return The failure, or nothing when the run did not fail.
                 */
                [[nodiscard]] std::optional<Error> run_error() const;

                /**
                 * @brief Requests cancellation of the active run. Thread-safe.
                 *
                 * Remaining tasks drain without executing and the run completes
                 * (the sentinel still fires); the driver stops launching new steps.
                 */
                void request_cancellation() noexcept;

                /**
                 * @brief Reports whether the active run has been cancelled.
                 * @return True if request_cancellation() was called and not yet reset.
                 */
                bool run_cancelled() const;

                /** @brief Clears the cancellation flag at the start of a fresh run. */
                void reset_cancellation();

                size_t worker_count() const override;
                size_t queue_size(int worker_id) const override;
                size_t completed_task_count(int worker_id) const override;

                void migrate_tasks(int from_worker_id, size_t count) override;
                using TaskMigrator::migrate_tasks;

                /**
                 * @brief Introspection hook (tests): the distance-ordered steal
                 * victim list for a worker. Leading entries are node-local.
                 * @return Victim worker ids in the order find_task_to_steal() walks.
                 */
                std::vector<int> steal_victim_order(int worker_id) const;

                /** @brief Count of successful steals from a node-local victim. */
                size_t local_steal_count() const;
                /** @brief Count of successful steals that had to cross to a remote node. */
                size_t remote_steal_count() const;

                /**
                 * @brief One worker's accumulated time split, in nanoseconds.
                 *
                 * The stall-attribution signal: how a worker spent a run across real
                 * work, stealing, completion polling, and idle backoff. Only populated
                 * when the run was armed for profiling.
                 */
                struct WorkerTimeSplit
                {
                    std::uint64_t busy_ns = 0;     /**< Inside task execution. */
                    std::uint64_t stealing_ns = 0; /**< Searching victims for work. */
                    std::uint64_t polling_ns = 0;  /**< Polling in-flight events. */
                    std::uint64_t idle_ns = 0;     /**< Backing off or waiting to be armed. */
                };

                /**
                 * @brief Returns the accumulated time split for worker @p worker_id.
                 * @param worker_id Worker index in [0, worker_count()).
                 * @return The worker's time split; zeroed when out of range.
                 */
                WorkerTimeSplit worker_times(int worker_id) const noexcept;

                /**
                 * @brief Zeroes every worker's time split.
                 *
                 * Called by the engine before a profiled run so the report covers only
                 * that run, mirroring the before/after handling of the steal counters.
                 */
                void reset_worker_times() noexcept;

                /**
                 * @brief Installs the coordinator offloadable nodes are offered to, and binds it
                 *        to this scheduler.
                 * @pre The pool runs no node.
                 * @param offload Not owned; null removes the installed one.
                 * @see modules/engine/scheduler/README.md
                 */
                void set_offload_coordinator(Internal::Graph::OffloadCoordinator* offload);

                /**
                 * @brief Returns how many dispatched nodes a coordinator has taken so far.
                 * @return A lifetime total; diff two readings to measure one run.
                 */
                [[nodiscard]] std::uint64_t offloaded_task_count() const noexcept;

                /**
                 * @brief Lets the installed coordinator mark the nodes a worker may run.
                 * @pre None of the nodes has been submitted.
                 * @param nodes Borrowed for the call; with no coordinator nothing happens.
                 * @param rules The rules of the snapshot the nodes were compiled against.
                 */
                void prepare_offload(span<const sushi_ptr<Internal::Graph::Node>> nodes,
                                     const std::shared_ptr<const PlacementRules>& rules);

                /**
                 * @brief Queues a node no worker finished and drops the coordinator's reference.
                 * @param node Accepted by the coordinator's try_offload().
                 */
                void resubmit(Internal::Graph::Node& node) override;

                /**
                 * @brief Releases a remotely finished node's successors, then drops the
                 *        coordinator's reference.
                 * @param node Accepted by the coordinator's try_offload().
                 * @see modules/engine/scheduler/README.md
                 */
                void complete_remote(Internal::Graph::Node& node) override;

                /**
                 * @brief Records an error as the run's failure, then completes the node as
                 *        complete_remote() does, so the aborting run skips its successors.
                 * @param node Accepted by the coordinator's try_offload().
                 */
                void fail(Internal::Graph::Node& node, Error error) override;

            private:
                /** @brief The pieces the scheduler composes (modules/engine/scheduler/README.md). */
                struct Parts;

                std::unique_ptr<Parts> parts_;
        };

    } // namespace Scheduler
} // namespace SushiRuntime
