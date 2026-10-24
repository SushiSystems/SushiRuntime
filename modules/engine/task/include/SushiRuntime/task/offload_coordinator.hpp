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
 * @see modules/engine/task/README.md
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <SushiRuntime/core/span.hpp>
#include <SushiRuntime/core/sushi_ptr.hpp>

namespace SushiRuntime
{
    class PlacementRules;

    namespace Internal::Graph
    {
        struct Node;
        class OffloadSink;

        /**
         * @brief Runs graph nodes somewhere other than the local pool.
         *
         * The scheduler, the graph and the context call it; one implementer is installed
         * per runtime, or none.
         * @see modules/engine/task/README.md
         */
        class OffloadCoordinator
        {
            public:
                /** @brief Destroys the implementer through the pointer the runtime owns. */
                virtual ~OffloadCoordinator() = default;

                /**
                 * @brief Receives the sink that takes offloaded nodes back.
                 *
                 * Called by the thread that installs the coordinator, while the pool runs no
                 * node, and possibly once before that with the same sink. The implementer
                 * stores the address and does not block.
                 * @param sink Outlives every call the coordinator makes before stop() returns.
                 */
                virtual void bind(OffloadSink& sink) = 0;

                /**
                 * @brief Marks the nodes of a compiled graph that a worker may run.
                 *
                 * Called once per compile(), on the compiling thread, before a node is submitted;
                 * it sets OffloadState::offloadable and its payload, and may lock and allocate.
                 * @param nodes Borrowed for the call; a slot may be null.
                 * @param rules The rules of the snapshot the compile read; null for none.
                 */
                virtual void prepare(span<const sushi_ptr<Node>> nodes,
                                     const std::shared_ptr<const PlacementRules>& rules) = 0;

                /**
                 * @brief Offers a ready node to a worker; may lock, never blocks on I/O.
                 *
                 * Called on a pool worker at dispatch, never in an aborted run. On true the
                 * implementer owns the scheduling reference until the sink gets the node, and does
                 * not touch the node once another thread can reach it; on false the caller runs it.
                 * @pre node.offload_state() is not null and its offloadable is true.
                 */
                [[nodiscard]] virtual bool try_offload(Node& node) = 0;

                /**
                 * @brief Hears that the pool finished a node itself.
                 *
                 * Called on a pool worker for every node the pool ran, before its successors are
                 * released, whether or not it carries an OffloadState; a node released unrun is
                 * not reported. The implementer may take a short lock and never allocates.
                 * @param node Borrowed for the call; no reference changes hands.
                 */
                virtual void on_local_completion(Node& node) noexcept = 0;

                /**
                 * @brief Ends all remote work before the scheduler shuts down.
                 *
                 * Called on the thread that tears the runtime down, while the pool still runs; it
                 * may be called again. The implementer joins its threads, so the call blocks. When
                 * it returns, every node the coordinator still held has been passed to the sink,
                 * the sink is called no more and try_offload() returns false.
                 */
                virtual void stop() noexcept = 0;
        };

    } // namespace Graph
} // namespace SushiRuntime
