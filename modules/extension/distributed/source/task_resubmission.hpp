/**************************************************************************/
/* task_resubmission.hpp                                                  */
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
 * @file task_resubmission.hpp
 * @brief Declares resubmit_tasks(), the one way a task taken back reaches the scheduler.
 * @author Mustafa Garip
 * @see modules/extension/distributed/README.md
 */

#pragma once

#include <cstddef>
#include <vector>

#include <SushiRuntime/core/export.hpp>
#include <SushiRuntime/core/span.hpp>
#include <SushiRuntime/buffer_exchange/buffer_exchange.hpp>
#include <SushiRuntime/buffer_exchange/buffer_registry.hpp>
#include <SushiRuntime/pending_tasks/pending_tasks.hpp>
#include <SushiRuntime/offload_coordination/held_nodes.hpp>
#include <SushiRuntime/offload_coordination/worker_directory.hpp>

namespace SushiRuntime
{
    namespace Internal::Graph
    {
        struct Node;
        class OffloadSink;
    }

    namespace Distributed
    {
        /**
         * @brief Returns tasks a removal of PendingTasks handed out to the scheduler.
         *
         * Per task, in order: bump its outputs in the registry, forget its transfer progress,
         * lower its worker's inflight count, pass the node to OffloadSink::resubmit(). An
         * exception out of the sink is caught without a rethrow, logged, and the node released.
         * @return The number of tasks handled, for the redispatched counter.
         */
        SUSHIRUNTIME_API std::size_t resubmit_tasks(SushiRuntime::span<PendingTask> tasks,
                                                    BufferRegistry& registry,
                                                    BufferExchange& exchange,
                                                    WorkerDirectory& directory,
                                                    Internal::Graph::OffloadSink& sink) noexcept;

        /**
         * @brief Offers nodes HeldNodes handed out to the scheduler again.
         *
         * Each node goes to OffloadSink::resubmit(); an exception out of the sink is caught
         * without a rethrow, logged, and the node released after @p held forgets its first offer.
         * @return The number of nodes handled.
         */
        SUSHIRUNTIME_API std::size_t reoffer_nodes(const std::vector<Internal::Graph::Node*>& nodes,
                                                   HeldNodes& held,
                                                   Internal::Graph::OffloadSink& sink) noexcept;

    } // namespace Distributed
} // namespace SushiRuntime
