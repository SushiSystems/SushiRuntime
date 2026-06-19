/**************************************************************************/
/* i_offload_policy.hpp                                                   */
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

#pragma once

#include <SushiRuntime/core/span.hpp>
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
            uint64_t free_mem     = 0;   /**< device memory headroom (bytes), from the worker's last PONG. */
            uint32_t rtt_micros   = 0;   /**< round-trip latency (us), refreshed by the master's PING/PONG probe. */
        };

        /**
         * @brief Decides whether (and where) a ready task should be offloaded. [swappable]
         *
         * Same plug-in shape as the scheduler's ThermalProxyStrategy, and the same
         * locality intuition: a worker is the most distant tier of the cost
         * hierarchy. The default (LeastLoadedOffloadPolicy) ranks live workers by an
         * estimated completion cost built from telemetry — round-trip latency, data
         * transfer time, and queue backlog — and keeps tiny payloads local. It can
         * be replaced at runtime without touching the coordinator.
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
                                                       SushiRuntime::span<const WorkerStats> workers,
                                                       uint64_t est_bytes) = 0;
        };

        /**
         * @brief Default policy: pick the live worker with the lowest estimated
         * completion cost (rtt + transfer_time + queue backlog) among those with
         * enough free memory; bail out (local) if none qualifies or the payload is
         * tiny enough that the network would only add latency.
         */
        class SUSHIRUNTIME_API LeastLoadedOffloadPolicy final : public IOffloadPolicy
        {
            public:
                /** @param min_offload_bytes Payloads below this stay local. */
                explicit LeastLoadedOffloadPolicy(uint64_t min_offload_bytes = 64 * 1024)
                    : min_offload_bytes_(min_offload_bytes) {}

                std::optional<int> pick_worker(const Graph::Node& node,
                                               SushiRuntime::span<const WorkerStats> workers,
                                               uint64_t est_bytes) override;

            private:
                uint64_t min_offload_bytes_;
        };

    } // namespace Distributed
} // namespace SushiRuntime
