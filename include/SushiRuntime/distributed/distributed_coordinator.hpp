/**************************************************************************/
/* distributed_coordinator.hpp                                            */
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
 * @file distributed_coordinator.hpp
 * @brief Declares DistributedCoordinator, which offloads tasks from the master node.
 * @author Mustafa Garip
 */

#pragma once

#include <chrono>
#include <memory>
#include <functional>
#include <SushiRuntime/core/export.hpp>
#include <SushiRuntime/core/sushi_ptr.hpp>
#include <SushiRuntime/distributed/i_transport.hpp>
#include <SushiRuntime/distributed/i_offload_policy.hpp>
#include <SushiRuntime/distributed/kernel_registry.hpp>
#include <SushiRuntime/distributed/buffer_registry.hpp>

namespace SushiRuntime
{
    namespace Graph { struct Node; }

    namespace Distributed
    {
        /**
         * @brief Orchestrates distributed task offloading and remote execution on the master node.
         *
         * Hides the complexity of network transport, worker heartbeats, and cluster state from the local task scheduler.
         * Remote buffer coherence is managed transparently via version bumping on local writes.
         */
        class SUSHIRUNTIME_API DistributedCoordinator
        {
            public:
                /**
                 * @brief Callback invoked when a remotely executed task completes.
                 *
                 * Integrates remote completion with the local task graph's dependency resolution.
                 *
                 * @param node The task graph node that finished execution.
                 * @param success True if execution completed without error, false otherwise.
                 */
                using CompletionHook = std::function<void(Graph::Node* node, bool success)>;

                /**
                 * @brief Callback invoked to re-inject an orphaned node into the ready queue.
                 *
                 * Called when a worker dies or fails, allowing the scheduler to retry offloading or execute locally.
                 *
                 * @param node The task graph node to be rescheduled.
                 */
                using ResubmitHook = std::function<void(Graph::Node* node)>;

                /**
                 * @brief Constructs a coordinator from the kernel registry, buffers, transport and
                 *        policy.
                 */
                DistributedCoordinator(sushi_ptr<KernelRegistry> kernels,
                                       BufferRegistry&           buffers,
                                       std::shared_ptr<ITransport>     transport,
                                       std::shared_ptr<IOffloadPolicy> policy);
                ~DistributedCoordinator();

                DistributedCoordinator(const DistributedCoordinator&) = delete;
                DistributedCoordinator& operator=(const DistributedCoordinator&) = delete;

                /**
                 * @brief Starts the network transport and begins accepting worker connections.
                 *
                 * Blocks until initial worker node discovery is complete.
                 */
                void start();

                /**
                 * @brief Drains inflight remote work and stops the network transport.
                 *
                 * Safely tears down cluster connections.
                 */
                void stop();

                /**
                 * @brief Checks if at least one active worker can execute a specific operation.
                 *
                 * Queried during graph compilation to determine if a node is offloadable.
                 *
                 * @param op The operation ID to query.
                 * @return True if supported by a live worker, false otherwise.
                 */
                bool supports(Graph::OpID op) const;

                /**
                 * @brief Installs the hook used to release a node's successors upon completion.
                 *
                 * The scheduler provides this closure to mirror local event polling behavior.
                 *
                 * @param hook The completion callback to register.
                 */
                void set_completion_hook(CompletionHook hook);

                /**
                 * @brief Installs the hook used to re-inject an orphaned node.
                 *
                 * The scheduler provides this closure to handle worker failures gracefully.
                 *
                 * @param hook The resubmission callback to register.
                 */
                void set_resubmit_hook(ResubmitHook hook);

                /**
                 * @brief Sets the maximum silence duration before declaring a worker dead.
                 *
                 * Dead workers trigger task redispatch. Any inbound message refreshes the timer.
                 *
                 * @param timeout The duration to wait before timing out a worker.
                 */
                void set_liveness_timeout(std::chrono::milliseconds timeout);

                /**
                 * @brief Sets the per-task execution deadline to prevent permanent stalls.
                 *
                 * A task failing to complete within this bound is redispatched. A value of zero disables the deadline.
                 *
                 * @param deadline The maximum allowed duration for task completion.
                 */
                void set_task_deadline(std::chrono::milliseconds deadline);

                /**
                 * @brief Attempts to offload a ready task to a remote worker.
                 *
                 * Delegates to the configured offload policy to select a suitable destination.
                 *
                 * @param node The ready, offloadable node to dispatch.
                 * @return True if dispatched to a worker, false if the local fallback should be used.
                 */
                bool try_offload(Graph::Node* node);

                /**
                 * @brief Retrieves the total number of tasks successfully offloaded.
                 *
                 * Used for cluster telemetry and performance monitoring.
                 *
                 * @return The cumulative offloaded task count.
                 */
                uint64_t offloaded_count() const;

                /**
                 * @brief Retrieves the number of offloadable tasks that ran locally due to policy decisions.
                 *
                 * Used for cluster telemetry and load balancing analysis.
                 *
                 * @return The cumulative declined task count.
                 */
                uint64_t declined_count() const;

                /**
                 * @brief Retrieves the number of tasks re-injected due to remote failures.
                 *
                 * Tracks fault tolerance events like worker death or network timeouts.
                 *
                 * @return The cumulative redispatched task count.
                 */
                uint64_t redispatched_count() const;

                /**
                 * @brief Retrieves the number of writebacks rejected by version fencing.
                 *
                 * Indicates how many stale or superseded results were discarded.
                 *
                 * @return The cumulative stale writeback count.
                 */
                uint64_t stale_writeback_count() const;

                /**
                 * @brief Retrieves the number of active, fully registered cluster workers.
                 *
                 * Represents the currently available distributed execution capacity.
                 *
                 * @return The active worker count.
                 */
                int alive_worker_count() const;

            private:
                struct Impl;
                sushi_ptr<Impl> impl_;
        };

    } // namespace Distributed
} // namespace SushiRuntime
