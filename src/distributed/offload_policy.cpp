/**************************************************************************/
/* offload_policy.cpp                                                     */
/**************************************************************************/
/*                          This file is part of:                         */
/*                              SushiRuntime                              */
/*              https://github.com/SushiSystems/SushiRuntime              */
/*                        https://sushisystems.io                         */
/**************************************************************************/
/* Copyright (c) 2026-present Mustafa Garip & Sushi Systems               */
/*                                                                        */
/* Licensed under the Apache License, Version 2.0 (the "License");        */
/* you may not use this file except in compliance with the License.       */
/* You may obtain a copy of the License at                                */
/*                                                                        */
/*     http://www.apache.org/licenses/LICENSE-2.0                         */
/*                                                                        */
/* Unless required by applicable law or agreed to in writing, software    */
/* distributed under the License is distributed on an "AS IS" BASIS,      */
/* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or        */
/* implied. See the License for the specific language governing           */
/* permissions and limitations under the License.                         */
/**************************************************************************/

#include <SushiRuntime/distributed/i_offload_policy.hpp>

#include <limits>

namespace SushiRuntime
{
    namespace Distributed
    {
        namespace
        {
            // --- cost-model tunables (file-local; documented heuristics) --------
            //
            // The policy ranks workers by an *estimated completion cost* in
            // microseconds, the network-tier analogue of the scheduler's NUMA
            // distance: a worker's cost is how long we expect the task to take to
            // get there, run, and free its slot, approximated from live telemetry.

            // Assumed effective wire bandwidth for the transfer-time term. ~1 GB/s
            // (1000 B/us) is a deliberately conservative homogeneous-lab figure: it
            // over-counts transfer cost slightly so a large payload only offloads
            // when a worker is genuinely worth the trip.
            constexpr double BYTES_PER_MICROSECOND = 1000.0;

            // How much each already-inflight task is assumed to delay a newcomer in
            // that worker's queue. Keeps the "least loaded" intent: between two
            // equally-near workers the emptier one wins.
            constexpr double QUEUE_PENALTY_US_PER_TASK = 1000.0;
        }

        /**
         * @brief Selects the optimal worker for offloading a task based on least-loaded heuristics.
         * Evaluates workers considering round-trip time, data transfer cost, and queue backlog to pick the best candidate.
         * @param node The Graph::Node to be offloaded.
         * @param workers A span containing statistics for all available workers.
         * @param est_bytes The estimated bytes of data to transfer for the task.
         * @return An std::optional containing the selected worker ID, or std::nullopt if no worker is suitable.
         */
        std::optional<int> LeastLoadedOffloadPolicy::pick_worker(const Graph::Node& /*node*/,
                                                                 SushiRuntime::span<const WorkerStats> workers,
                                                                 uint64_t est_bytes)
        {
            // Tiny payloads: the network would only add latency. Keep local.
            if (est_bytes < min_offload_bytes_)
                return std::nullopt;

            const double transfer_us =
                static_cast<double>(est_bytes) / BYTES_PER_MICROSECOND;

            int    best      = -1;
            double best_cost = std::numeric_limits<double>::max();
            for (const auto& w : workers)
            {
                if (!w.alive)
                    continue;
                if (w.free_mem != 0 && w.free_mem < est_bytes)
                    continue; // not enough headroom on that worker

                // cost = round trip + data transfer + queue backlog penalty.
                // rtt_micros is refreshed by the coordinator's PING/PONG probe; a
                // distant or laggy worker is naturally deprioritized here.
                const double cost = static_cast<double>(w.rtt_micros)
                                  + transfer_us
                                  + static_cast<double>(w.inflight) * QUEUE_PENALTY_US_PER_TASK;

                if (best < 0 || cost < best_cost)
                {
                    best      = w.worker_id;
                    best_cost = cost;
                }
            }

            if (best < 0)
                return std::nullopt; // no eligible worker -> transparent local fallback

            return best;
        }

    } // namespace Distributed
} // namespace SushiRuntime
