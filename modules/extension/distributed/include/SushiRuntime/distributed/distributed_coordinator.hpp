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
#include <cstddef>
#include <memory>
#include <SushiRuntime/core/export.hpp>
#include <SushiRuntime/core/span.hpp>
#include <SushiRuntime/core/sushi_ptr.hpp>
#include <SushiRuntime/memory/usm_release_observer.hpp>
#include <SushiRuntime/task/offload_coordinator.hpp>
#include <SushiRuntime/distributed/i_transport.hpp>
#include <SushiRuntime/distributed/i_offload_policy.hpp>
#include <SushiRuntime/distributed/kernel_registry.hpp>
#include <SushiRuntime/distributed/buffer_registry.hpp>

namespace SushiRuntime
{
    namespace Distributed
    {
        /**
         * @brief Orchestrates distributed task offloading and remote execution on the master node.
         *
         * Hides the complexity of network transport, worker heartbeats, and cluster state from the local task scheduler.
         * Remote buffer coherence is managed transparently via version bumping on local writes.
         */
        class SUSHIRUNTIME_API DistributedCoordinator final : public Graph::OffloadCoordinator,
                                                              public Memory::UsmReleaseObserver
        {
            public:
                /**
                 * @brief Constructs a coordinator from the kernel registry, buffers, transport and
                 *        policy.
                 */
                DistributedCoordinator(sushi_ptr<KernelRegistry> kernels,
                                       BufferRegistry&           buffers,
                                       std::shared_ptr<ITransport>     transport,
                                       std::shared_ptr<IOffloadPolicy> policy);
                ~DistributedCoordinator() override;

                DistributedCoordinator(const DistributedCoordinator&) = delete;
                DistributedCoordinator& operator=(const DistributedCoordinator&) = delete;

                /**
                 * @brief Starts the network transport and begins accepting worker connections.
                 *
                 * Blocks until initial worker node discovery is complete.
                 */
                void start();

                /**
                 * @brief Joins the monitor thread and stops the transport; a second call does
                 *        nothing more.
                 * @see modules/extension/distributed/README.md
                 */
                void stop() noexcept override;

                /**
                 * @brief Stores the sink that takes back finished and orphaned nodes.
                 * @pre start() has not been called.
                 * @param sink Outlives every call made before stop() returns.
                 */
                void bind(Graph::OffloadSink& sink) override;

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
                [[nodiscard]] bool try_offload(Graph::Node& node) override;

                /**
                 * @brief Marks the nodes of a compiled graph that a worker may run.
                 *
                 * A node qualifies when a live worker runs its op id and every buffer it
                 * touches is registered.
                 * @param nodes Borrowed for the call; a slot may be null.
                 */
                void prepare(span<const sushi_ptr<Graph::Node>> nodes) override;

                /**
                 * @brief Advances the versions of the buffers a locally finished node wrote.
                 * @param node Borrowed for the call; called before its successors are released.
                 */
                void on_local_completion(Graph::Node& node) noexcept override;

                /**
                 * @brief Registers an allocation in the buffer registry; a known address keeps
                 *        its handle.
                 * @param address Base of a live USM allocation of at least @p bytes.
                 */
                void share(void* address, std::size_t bytes) override;

                /**
                 * @brief Returns the four counters and the live worker count.
                 * @return A snapshot; the worker count is read under the coordinator's lock.
                 */
                [[nodiscard]] Graph::OffloadStatistics statistics() const noexcept override;

                /** @brief Unregisters the buffer at a released address, if it has one. */
                void on_release(void* address) noexcept override;

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
