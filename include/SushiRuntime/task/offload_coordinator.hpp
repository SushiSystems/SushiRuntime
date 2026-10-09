/**************************************************************************/
/* offload_coordinator.hpp                                                */
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
 * @file offload_coordinator.hpp
 * @brief Declares OffloadCoordinator, the seam through which nodes leave the local pool.
 * @author Mustafa Garip
 * @see include/SushiRuntime/task/README.md
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <SushiRuntime/core/span.hpp>
#include <SushiRuntime/core/sushi_ptr.hpp>

namespace SushiRuntime
{
    namespace Graph
    {
        struct Node;
        class OffloadSink;

        /**
         * @brief Reports what an offload coordinator did since it was built.
         * @see include/SushiRuntime/task/README.md
         */
        struct OffloadStatistics
        {
            std::uint64_t offloaded = 0;        /**< Dispatches a worker accepted. */
            std::uint64_t declined = 0;         /**< Offloadable dispatches left to run locally. */
            std::uint64_t redispatched = 0;     /**< Nodes taken back from a lost worker or task. */
            std::uint64_t stale_writebacks = 0; /**< Worker outputs dropped as out of date. */
            std::size_t alive_workers = 0;      /**< Workers registered and answering now. */
        };

        /**
         * @brief Runs graph nodes somewhere other than the local pool.
         *
         * The scheduler, the graph and the context call it; one implementer is installed
         * per runtime, or none.
         * @see include/SushiRuntime/task/README.md
         */
        class OffloadCoordinator
        {
            public:
                /** @brief Destroys the implementer through the pointer the runtime owns. */
                virtual ~OffloadCoordinator() = default;

                /**
                 * @brief Receives the sink that takes offloaded nodes back.
                 *
                 * Called once, by the thread that installs the coordinator, while the pool
                 * runs no node. The implementer stores the address and does not block.
                 * @param sink Outlives every call the coordinator makes before stop() returns.
                 */
                virtual void bind(OffloadSink& sink) = 0;

                /**
                 * @brief Marks the nodes of a compiled graph that a worker may run.
                 *
                 * Called once per compile(), on the compiling thread, before any of the nodes
                 * is submitted. The implementer sets OffloadState::offloadable and its payload
                 * on each node it accepts; it may lock and allocate.
                 * @param nodes Borrowed for the call; a slot may be null.
                 */
                virtual void prepare(span<const sushi_ptr<Node>> nodes) = 0;

                /**
                 * @brief Offers a ready node to a worker.
                 *
                 * Called on a pool worker at dispatch; the implementer may lock and send, and
                 * does not wait for the result. On true it owns the caller's scheduling
                 * reference until it passes the node to the sink; on false the caller runs it.
                 * @pre node.offload_state() is not null and its offloadable is true.
                 */
                [[nodiscard]] virtual bool try_offload(Node& node) = 0;

                /**
                 * @brief Hears that the pool finished a node itself.
                 *
                 * Called on a pool worker for every node the poller reaps, before its
                 * successors are released. The implementer returns at once for a node with
                 * no OffloadState; otherwise it may take a short lock and never allocates.
                 * @param node Borrowed for the call; no reference changes hands.
                 */
                virtual void on_local_completion(Node& node) noexcept = 0;

                /**
                 * @brief Makes an allocation known to the workers.
                 *
                 * Called on any user thread, before the graph that touches the allocation is
                 * compiled. The implementer may lock and allocate; an address shared again
                 * stays one allocation to the workers.
                 * @param address Base of a live USM allocation of at least @p bytes.
                 */
                virtual void share(void* address, std::size_t bytes) = 0;

                /**
                 * @brief Returns the counters as they stand; any thread may call.
                 * @return A snapshot; the fields are not read at one instant.
                 */
                [[nodiscard]] virtual OffloadStatistics statistics() const noexcept = 0;

                /**
                 * @brief Ends all remote work before the scheduler shuts down.
                 *
                 * Called on the thread that tears the runtime down; it may be called again.
                 * The implementer joins its threads, so the call blocks. Once it returns the
                 * coordinator calls the sink no more and try_offload() returns false.
                 */
                virtual void stop() noexcept = 0;
        };

    } // namespace Graph
} // namespace SushiRuntime
