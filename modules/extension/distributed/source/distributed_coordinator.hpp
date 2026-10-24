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
 * @brief Declares DistributedCoordinator, which composes the master's bricks behind the seam.
 * @author Mustafa Garip
 * @see modules/extension/distributed/README.md
 */

#pragma once

#include <chrono>
#include <cstddef>
#include <memory>

#include <SushiRuntime/core/export.hpp>
#include <SushiRuntime/core/span.hpp>
#include <SushiRuntime/core/sushi_ptr.hpp>
#include <SushiRuntime/machine/node_facts.hpp>
#include <SushiRuntime/memory/usm_release_observer.hpp>
#include <SushiRuntime/task/offload_coordinator.hpp>
#include <SushiRuntime/task/task_types.hpp>
#include <SushiRuntime/distributed/cluster.hpp>
#include <SushiRuntime/cluster_load/cluster_statistics.hpp>
#include <SushiRuntime/transport/transport.hpp>
#include <SushiRuntime/buffer_exchange/buffer_registry.hpp>


namespace SushiRuntime
{
    namespace Distributed
    {
        /**
         * @brief Holds the coordinator's four counters since it was built and one gauge.
         */
        struct CoordinatorStatistics
        {
            std::uint64_t offloaded = 0;        /**< Dispatches queued to a worker. */
            std::uint64_t declined = 0;         /**< Offloadable dispatches left to run locally. */
            std::uint64_t redispatched = 0;     /**< Nodes taken back from a lost worker or task. */
            std::uint64_t stale_writebacks = 0; /**< Worker outputs dropped as out of date. */
            std::size_t alive_workers = 0;      /**< Workers registered and answering now. */
        };

        /**
         * @brief Implements Internal::Graph::OffloadCoordinator by wiring the master's bricks together.
         *
         * It builds the directory, the pending table, the exchange, the dispatcher and the
         * monitor, is the transport's only event receiver, and routes each frame through a
         * MessageRouter to a handler that calls bricks in turn. It owns no mutex and holds no
         * `switch` over message types; every lock belongs to a brick.
         */
        class SUSHIRUNTIME_API DistributedCoordinator final : public Internal::Graph::OffloadCoordinator,
                                                              public Internal::Memory::USMReleaseObserver
        {
            public:
                /**
                 * @brief Builds the bricks; nothing runs until start().
                 * @param registry Outlives the coordinator; Cluster::State owns both.
                 * @param transport A master-side transport that was never started; not null.
                 * @param options Moved in; the secret stays here for the directory to read.
                 * @param session_id Names this master to its workers; drawn at random.
                 * @param master This machine's facts; copied.
                 */
                DistributedCoordinator(BufferRegistry& registry,
                                       std::shared_ptr<Transport> transport,
                                       ClusterOptions options, std::uint64_t session_id,
                                       NodeFacts master);

                /** @brief Calls stop(), then destroys the bricks. */
                ~DistributedCoordinator() override;

                DistributedCoordinator(const DistributedCoordinator&) = delete;
                DistributedCoordinator& operator=(const DistributedCoordinator&) = delete;
                DistributedCoordinator(DistributedCoordinator&&) = delete;
                DistributedCoordinator& operator=(DistributedCoordinator&&) = delete;

                /**
                 * @brief Stores the sink that takes nodes back.
                 * @pre start() has not been called.
                 */
                void bind(Internal::Graph::OffloadSink& sink) override;

                /**
                 * @brief Starts the transport, then the monitor.
                 * @pre bind() was called.
                 * @return The transport's result; on a failure nothing is left running.
                 */
                [[nodiscard]] TransportResult start();

                /**
                 * @brief Ends all remote work; a second call does nothing.
                 *
                 * In order: close the pending table, the held table and the directory, so
                 * nothing more is dispatched or held; join the monitor; stop the transport,
                 * which joins every reader; drain the pending and the held tables and resubmit
                 * each node through the sink.
                 */
                void stop() noexcept override;

                /** @brief Forwards to OffloadDispatcher::prepare() with the compile's rules. */
                void prepare(span<const sushi_ptr<Internal::Graph::Node>> nodes,
                             const std::shared_ptr<const PlacementRules>& rules) override;

                /**
                 * @brief Forwards to OffloadDispatcher::try_offload(); false once stopped. A
                 *        node held for its rule wakes the monitor for the end of the wait.
                 */
                [[nodiscard]] bool try_offload(Internal::Graph::Node& node) override;

                /** @brief Forwards to OffloadDispatcher::complete_local(). */
                void on_local_completion(Internal::Graph::Node& node) noexcept override;

                /** @brief Shares an allocation through BufferRegistry::register_buffer(). */
                void share(void* address, std::size_t bytes);

                /**
                 * @brief Returns the four counters and the alive worker count.
                 *
                 * Reads atomics of the bricks only; takes no lock.
                 */
                [[nodiscard]] CoordinatorStatistics statistics() const noexcept;

                /**
                 * @brief Unshares a released address and tells each worker to drop the buffer.
                 *
                 * BUFFER_RELEASE goes out as a control frame per alive worker; a failure to
                 * queue one is swallowed, since the worker's budget evicts the buffer later.
                 */
                void on_release(void* address) noexcept override;

                /** @brief Returns every counter, the two gauges and one entry per worker. */
                [[nodiscard]] ClusterStatistics cluster_statistics() const;

                /** @brief Reports whether an alive worker has a kernel for an operation. */
                [[nodiscard]] bool supports(OperationID op) const;

                /** @brief Forwards to WorkerDirectory::wait_for_workers(). */
                [[nodiscard]] bool wait_for_workers(std::size_t count,
                                                    std::chrono::milliseconds timeout);

                /** @brief Sets the liveness timeout the monitor applies and WELCOME carries. */
                void set_liveness_timeout(std::chrono::milliseconds timeout) noexcept;

                /** @brief Sets the task deadline the monitor applies; 0 sets no bound. */
                void set_task_deadline(std::chrono::milliseconds deadline) noexcept;

            private:
                struct Impl;

                /**
                 * @brief Holds the options, the transport and the bricks, declared in the
                 *        order they are built: directory, pending, held, exchange, dispatcher,
                 *        monitor.
                 */
                std::unique_ptr<Impl> impl_;
        };

    } // namespace Distributed
} // namespace SushiRuntime
