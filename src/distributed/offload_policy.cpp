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

namespace SushiRuntime
{
    namespace Distributed
    {
        std::optional<int> LeastLoadedOffloadPolicy::pick_worker(const Graph::Node& /*node*/,
                                                                 std::span<const WorkerStats> workers,
                                                                 uint64_t est_bytes)
        {
            // Tiny payloads: the network would only add latency. Keep local.
            if (est_bytes < min_offload_bytes_)
                return std::nullopt;

            int best = -1;
            uint32_t best_inflight = 0;
            for (const auto& w : workers)
            {
                if (!w.alive)
                    continue;
                if (w.free_mem != 0 && w.free_mem < est_bytes)
                    continue; // not enough headroom on that worker

                if (best < 0 || w.inflight < best_inflight)
                {
                    best          = w.worker_id;
                    best_inflight = w.inflight;
                }
            }

            if (best < 0)
                return std::nullopt; // no eligible worker -> transparent local fallback

            return best;
        }

    } // namespace Distributed
} // namespace SushiRuntime
