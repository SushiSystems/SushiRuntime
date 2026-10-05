/****************************************************************/
/* i_offload_policy.hpp                                         */
/* SushiRuntime - https://github.com/SushiSystems/SushiRuntime  */
/* Copyright (c) 2026 Sushi Systems                             */
/* Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.    */
/* Commercial use requires a licence from Sushi Systems.        */
/****************************************************************/

/**
 * @file i_offload_policy.hpp
 * @brief Declares IOffloadPolicy, WorkerStats and the least-loaded offload policy.
 * @author Mustafa Garip
 */

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
         * @brief Live telemetry and health metrics for a single cluster worker.
         *
         * Supplies the state data needed by the offload policy to make task placement decisions.
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
         * @brief Defines the strategy for selecting an execution node for an offloadable task.
         *
         * Evaluates cluster metrics to choose between remote dispatch and local fallback execution.
         */
        class SUSHIRUNTIME_API IOffloadPolicy
        {
            public:
                virtual ~IOffloadPolicy() = default;

                /**
                 * @brief Selects the optimal worker for executing a specific task.
                 *
                 * @param node The task under consideration for offloading.
                 * @param workers Current telemetry metrics for all available workers.
                 * @param est_bytes The estimated amount of data that must cross the network.
                 * @return The ID of the chosen worker, or std::nullopt to execute locally.
                 */
                virtual std::optional<int> pick_worker(const Graph::Node& node,
                                                       SushiRuntime::span<const WorkerStats> workers,
                                                       uint64_t est_bytes) = 0;
        };

        /**
         * @brief Strategy that routes tasks to the worker with the lowest estimated completion cost.
         *
         * Incorporates network latency, transfer times, and remote queue backlogs to minimize execution span.
         * Retains tiny payloads locally to avoid network overhead.
         */
        class SUSHIRUNTIME_API LeastLoadedOffloadPolicy final : public IOffloadPolicy
        {
            public:
                /**
                 * @brief Constructs a policy configured with a minimum payload size.
                 *
                 * @param min_offload_bytes The byte threshold below which tasks are forced to run locally.
                 */
                explicit LeastLoadedOffloadPolicy(uint64_t min_offload_bytes = 64 * 1024)
                    : min_offload_bytes_(min_offload_bytes) {}

                /**
                 * @brief Selects the optimal worker based on load and latency.
                 *
                 * @param node The task under consideration for offloading.
                 * @param workers Current telemetry metrics for all available workers.
                 * @param est_bytes The estimated amount of data that must cross the network.
                 * @return The ID of the chosen worker, or std::nullopt to execute locally.
                 */
                std::optional<int> pick_worker(const Graph::Node& node,
                                               SushiRuntime::span<const WorkerStats> workers,
                                               uint64_t est_bytes) override;

            private:
                uint64_t min_offload_bytes_;
        };

    } // namespace Distributed
} // namespace SushiRuntime
