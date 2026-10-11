/**************************************************************************/
/* offload_policy.cpp                                                   */
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
 * @file offload_policy.cpp
 * @brief Picks the worker with the lowest estimated completion cost for an offloaded task.
 * @author Mustafa Garip
 */

#include <SushiRuntime/distributed/offload_policy.hpp>

#include <limits>

namespace SushiRuntime
{
    namespace Distributed
    {
        namespace
        {
            /// @brief Holds the assumed wire bandwidth in B/us (modules/extension/distributed/README.md).
            constexpr double K_BYTES_PER_MICROSECOND = 1000.0;

            /// @brief Holds the microseconds one inflight task adds (modules/extension/distributed/README.md).
            constexpr double K_QUEUE_PENALTY_US_PER_TASK = 1000.0;
        }

        /** @brief Returns the alive worker of the lowest cost, or nothing to keep the task local. */
        std::optional<int> LeastLoadedOffloadPolicy::pick_worker(const Internal::Graph::Node& /*node*/,
                                                                 SushiRuntime::span<const WorkerLoad> workers,
                                                                 uint64_t est_bytes)
        {
            if (est_bytes < min_offload_bytes_)
                return std::nullopt;

            const double transfer_us =
                static_cast<double>(est_bytes) / K_BYTES_PER_MICROSECOND;

            int    best      = -1;
            double best_cost = std::numeric_limits<double>::max();
            for (const auto& w : workers)
            {
                if (!w.alive)
                    continue;
                if (w.free_mem != 0 && w.free_mem < est_bytes)
                    continue;

                // Cost is round trip plus transfer plus queue backlog (modules/extension/distributed/README.md).
                const double cost = static_cast<double>(w.rtt_micros)
                                  + transfer_us
                                  + static_cast<double>(w.inflight) * K_QUEUE_PENALTY_US_PER_TASK;

                if (best < 0 || cost < best_cost)
                {
                    best      = w.worker_id;
                    best_cost = cost;
                }
            }

            if (best < 0)
                return std::nullopt;

            return best;
        }

    } // namespace Distributed
} // namespace SushiRuntime
