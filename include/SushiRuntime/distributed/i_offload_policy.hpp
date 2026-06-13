/**************************************************************************/
/* i_offload_policy.hpp                                                   */
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

#pragma once

#include <span>
#include <cstdint>
#include <optional>
#include <SushiRuntime/core/export.hpp>

namespace SushiRuntime
{
    namespace Graph { struct Node; }

    namespace Distributed
    {
        /**
         * @brief Live, per-worker metrics the policy reasons over.
         *
         * This is the top tier of the same locality hierarchy the scheduler uses
         * for NUMA stealing: a worker node is one more (much more distant) tier.
         */
        struct WorkerStats
        {
            int      worker_id    = -1;
            bool     alive        = false;
            uint32_t inflight     = 0;   /**< tasks currently dispatched to it. */
            uint64_t free_mem     = 0;   /**< reported device memory headroom (bytes). */
            uint32_t rtt_micros   = 0;   /**< last measured round-trip latency. */
        };

        /**
         * @brief Decides whether (and where) a ready task should be offloaded. [swappable]
         *
         * Same plug-in shape as the scheduler's ThermalProxyStrategy: the default
         * implements the Idea.md heuristic
         *
         *     offload iff  local_queue_wait > rtt + transfer_time + remote_exec
         *
         * but it can be replaced at runtime without touching the coordinator.
         *
         * Returning std::nullopt means "run it locally" — the safe default that
         * keeps the 8-of-10 small jobs on the home machine.
         */
        class SUSHIRUNTIME_API IOffloadPolicy
        {
            public:
                virtual ~IOffloadPolicy() = default;

                /**
                 * @brief Picks a worker for an offloadable, ready node.
                 * @param node The task under consideration (already offloadable).
                 * @param workers Current cluster telemetry.
                 * @param est_bytes Bytes that would have to cross the wire.
                 * @return Chosen worker id, or nullopt to keep it local.
                 */
                virtual std::optional<int> pick_worker(const Graph::Node& node,
                                                       std::span<const WorkerStats> workers,
                                                       uint64_t est_bytes) = 0;
        };

        /**
         * @brief Default policy: pick the live worker with the fewest inflight
         * tasks and enough free memory; bail out (local) if none qualifies or the
         * payload is tiny enough that the network would only add latency.
         */
        class SUSHIRUNTIME_API LeastLoadedOffloadPolicy final : public IOffloadPolicy
        {
            public:
                /** @param min_offload_bytes Payloads below this stay local. */
                explicit LeastLoadedOffloadPolicy(uint64_t min_offload_bytes = 64 * 1024)
                    : min_offload_bytes_(min_offload_bytes) {}

                std::optional<int> pick_worker(const Graph::Node& node,
                                               std::span<const WorkerStats> workers,
                                               uint64_t est_bytes) override;

            private:
                uint64_t min_offload_bytes_;
        };

    } // namespace Distributed
} // namespace SushiRuntime
