/**************************************************************************/
/* offload_dispatcher.hpp                                                 */
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
 * @file offload_dispatcher.hpp
 * @brief Declares OffloadDispatcher, which marks offloadable nodes and dispatches them.
 * @author Mustafa Garip
 * @see modules/extension/distributed/README.md
 */

#pragma once

#include <atomic>
#include <cstdint>

#include <SushiRuntime/core/export.hpp>
#include <SushiRuntime/core/span.hpp>
#include <SushiRuntime/core/sushi_ptr.hpp>
#include <SushiRuntime/distributed/i_transport.hpp>

#include "buffer_registry.hpp"
#include "pending_tasks.hpp"
#include "worker_directory.hpp"

namespace SushiRuntime
{
    namespace Graph { struct Node; }

    namespace Distributed
    {
        /** @brief Holds the dispatches to a worker one node may have in one run. */
        inline constexpr std::uint32_t K_MAXIMUM_OFFLOAD_ATTEMPTS = 2;

        /**
         * @brief Holds the dispatcher's counters since it was built.
         */
        struct OffloadDispatcherStatistics
        {
            std::uint64_t offloaded = 0;       /**< Dispatches queued to a worker. */
            std::uint64_t declined = 0;        /**< try_offload() calls that returned false. */
            std::uint64_t send_queue_full = 0; /**< Dispatches refused by a full queue. */
        };

        /**
         * @brief Translates between graph nodes and the bricks: which node may leave, and how.
         *
         * It is the only brick that reads a node's buffer lists and writes its OffloadState.
         * It owns no mutex and no thread; it calls the registry, the directory, the pending
         * table and the transport one after another, each under that brick's own short lock.
         */
        class SUSHIRUNTIME_API OffloadDispatcher final
        {
            public:
                /** @brief Builds a dispatcher over bricks that outlive it. */
                OffloadDispatcher(BufferRegistry& registry, WorkerDirectory& directory,
                                  PendingTasks& pending, ITransport& transport);

                OffloadDispatcher(const OffloadDispatcher&) = delete;
                OffloadDispatcher& operator=(const OffloadDispatcher&) = delete;
                OffloadDispatcher(OffloadDispatcher&&) = delete;
                OffloadDispatcher& operator=(OffloadDispatcher&&) = delete;

                /**
                 * @brief Marks each node a worker may run; the body of prepare() on the seam.
                 *
                 * A node qualifies when its op id is not 0, an alive worker supports it and
                 * every buffer it touches is shared. A fixed-buffer node gets its handles as
                 * an OffloadHandles payload. Logs one WARN per call that leaves op-tagged
                 * nodes local, with the count per reason.
                 */
                void prepare(SushiRuntime::span<const sushi_ptr<Graph::Node>> nodes);

                /**
                 * @brief Dispatches a ready node; the body of OffloadCoordinator::try_offload().
                 * In order: decline at the attempt cap, else count the attempt, the last write
                 * to the node; resolve the handles; WorkerDirectory::pick(); bump the outputs;
                 * PendingTasks::insert(); ITransport::send(). No socket call, no INFO line.
                 * @return False when the pool must run the node; true also when the send failed
                 *         and take() found that a reaper holds the node.
                 */
                [[nodiscard]] bool try_offload(Graph::Node& node);

                /**
                 * @brief Bumps the buffers a locally finished node wrote and clears its attempt
                 *        count; the body of OffloadCoordinator::on_local_completion().
                 *
                 * A node with live lists bumps the addresses in live_writes, not its baked
                 * list. A node without an OffloadState costs one null check.
                 */
                void complete_local(Graph::Node& node) noexcept;

                /**
                 * @brief Clears the attempt count of a node a worker finished.
                 * @pre The caller owns the node: PendingTasks::take() returned it.
                 */
                void complete_remote(Graph::Node& node) noexcept;

                /** @brief Returns the counters; reads atomics and takes no lock. */
                [[nodiscard]] OffloadDispatcherStatistics statistics() const noexcept;

            private:
                /// @brief Names and versions of the shared buffers.
                BufferRegistry& registry_;

                /// @brief Picks the worker and counts its load.
                WorkerDirectory& directory_;

                /// @brief Takes ownership of each dispatched node.
                PendingTasks& pending_;

                /// @brief Carries the dispatch frame.
                ITransport& transport_;

                /// @brief Dispatches queued to a worker.
                std::atomic<std::uint64_t> offloaded_{0};

                /// @brief try_offload() calls that returned false.
                std::atomic<std::uint64_t> declined_{0};

                /// @brief Dispatches refused by a full queue.
                std::atomic<std::uint64_t> send_queue_full_{0};
        };

    } // namespace Distributed
} // namespace SushiRuntime
