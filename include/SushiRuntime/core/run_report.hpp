/**************************************************************************/
/* run_report.hpp                                                         */
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

#include <cstddef>
#include <string>
#include <vector>

namespace SushiRuntime
{
    namespace Core
    {
        /**
         * @brief Describes the per-NUMA-node execution and work-stealing metrics.
         *
         * Tracks tasks executed locally and stolen across nodes for a single NUMA domain.
         */
        struct NumaNodeStatistics
        {
            int numa_id = -1;                       /**< NUMA node this row describes. */

            std::size_t cpu_tasks_executed = 0;     /**< Tasks run on CPU devices on this node. */
            std::size_t gpu_tasks_executed = 0;     /**< Tasks run on GPU devices attached to this node. */

            std::size_t tasks_popped_local = 0;     /**< Tasks taken from the worker's own queue. */
            std::size_t tasks_stolen_intra_numa = 0;/**< Steals from a victim on the same NUMA node. */
            std::size_t tasks_stolen_inter_numa = 0;/**< Steals that crossed to another NUMA node. */

            std::size_t local_memory_allocations_bytes = 0; /**< Reserved: USM bytes pinned to this node. */
        };

        /**
         * @brief Describes the distributed master-worker offload and network metrics.
         *
         * Tracks tasks shipped to remote workers and bytes transferred across the network.
         */
        struct DistributedStatistics
        {
            std::size_t tasks_offloaded_to_network = 0;  /**< Tasks shipped to remote workers. */
            std::size_t tasks_received_from_network = 0; /**< Reserved: remote tasks executed locally. */

            std::size_t network_bytes_tx = 0;            /**< Reserved: total bytes sent. */
            std::size_t network_bytes_rx = 0;            /**< Reserved: total bytes received. */

            std::size_t remote_nodes_involved = 0;       /**< Live remote workers that took part. */
        };

        /**
         * @brief Summarizes the execution statistics of a single graph run.
         *
         * Aggregates execution times, NUMA topology breakdowns, and distributed network metrics.
         */
        struct RunReport
        {
            // A. Overall status
            bool is_successful = true;                  /**< Whether the run completed without error. */
            bool cancelled = false;                     /**< Whether the run stopped early on a cancellation request. */
            std::size_t total_tasks_executed = 0;       /**< Local (all NUMA) + remote tasks. */

            // B. Timing / bottleneck analysis
            double total_duration_ms = 0.0;             /**< Wall-clock time of the whole run. */
            double graph_analysis_duration_ms = 0.0;    /**< Dependency analysis + compile time. */
            double scheduling_overhead_ms = 0.0;        /**< Reserved: dispatch/enqueue time. */
            double network_latency_wait_ms = 0.0;       /**< Reserved: idle time awaiting remote results. */

            // C. NUMA topology breakdown
            std::vector<NumaNodeStatistics> numa_statistics;      /**< Per-node breakdown (one entry per active node). */

            // D. Distributed breakdown
            DistributedStatistics distributed_statistics;         /**< Offload/network stats (Master mode). */

            // E. PCIe / SYCL host-device transfers (reserved)
            std::size_t host_to_device_bytes = 0;       /**< Reserved: bytes copied host -> device. */
            std::size_t device_to_host_bytes = 0;       /**< Reserved: bytes copied device -> host. */

            // F. Diagnostics
            std::vector<std::string> warnings;          /**< Non-fatal notes raised during the run. */
        };

    } // namespace Core
} // namespace SushiRuntime
