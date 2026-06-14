/**************************************************************************/
/* offload_policy.cpp                                                     */
/**************************************************************************/
/*                          This file is part of:                         */
/*                              SushiRuntime                              */
/*              https://github.com/SushiSystems/SushiRuntime              */
/*                        https://sushisystems.io                         */
/**************************************************************************/
/* Copyright (c) 2026-present Mustafa Garip & Sushi Systems               */
/* All Rights Reserved.                                                   */
/*                                                                        */
/* CONFIDENTIAL: This software is the proprietary information of          */
/* Mustafa Garip & Sushi Systems. Unauthorized copying of this file,      */
/* via any medium is strictly prohibited.                                 */
/*                                                                        */
/* This source code and the intellectual property contained herein        */
/* is confidential and may not be disclosed, copied, or used without      */
/* explicit written permission from the copyright holders.                */
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

        std::optional<int> LeastLoadedOffloadPolicy::pick_worker(const Graph::Node& /*node*/,
                                                                 std::span<const WorkerStats> workers,
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
