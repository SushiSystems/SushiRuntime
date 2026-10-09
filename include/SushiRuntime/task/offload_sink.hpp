/**************************************************************************/
/* offload_sink.hpp                                                       */
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
 * @file offload_sink.hpp
 * @brief Declares OffloadSink, through which a coordinator returns nodes to the scheduler.
 * @author Mustafa Garip
 * @see include/SushiRuntime/task/README.md
 */

#pragma once

namespace SushiRuntime
{
    namespace Graph
    {
        struct Node;

        /**
         * @brief Takes back the nodes an offload coordinator accepted at dispatch.
         *
         * Both members run on a coordinator thread, never on a pool worker, and several
         * threads may call at once. Each consumes the reference OffloadCoordinator::try_offload()
         * handed over, so the caller reads and writes the node before the call, not after.
         * @see include/SushiRuntime/task/README.md
         */
        class OffloadSink
        {
            public:
                /**
                 * @brief Queues a node that no worker finished for another dispatch.
                 *
                 * The implementer takes its own scheduling reference, releases the caller's
                 * and returns without waiting for the node to run. The node may be offered
                 * to try_offload() again.
                 * @param node Accepted by try_offload() and not yet returned through this sink.
                 */
                virtual void resubmit(Node& node) = 0;

                /**
                 * @brief Completes a node a worker ran, as the poller does for a local one.
                 *
                 * The implementer satisfies one dependency on every successor, queues those
                 * that became ready, and releases the caller's reference. It may take a
                 * queue lock; it does not wait for a successor to run.
                 * @param node Accepted by try_offload(); its outputs are already in place.
                 */
                virtual void complete_remote(Node& node) = 0;

            protected:
                /** @brief Keeps a sink from being destroyed through this interface. */
                ~OffloadSink() = default;
        };

    } // namespace Graph
} // namespace SushiRuntime
