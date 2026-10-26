/**************************************************************************/
/* run_report.hpp                                                         */
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
 * @file run_report.hpp
 * @brief Declares RunReport and the statistics records it carries.
 * @author Mustafa Garip
 * @see modules/foundation/report/README.md
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace SushiRuntime
{
    /**
     * @brief Describes the per-NUMA-node execution and work-stealing metrics.
     *
     * Tracks tasks executed locally and stolen across nodes for a single NUMA domain.
     */
    struct NUMANodeStatistics
    {
        int numa_id = -1;                       /**< NUMA node this row describes. Always -1 in v1: one row covers the run. */

        std::size_t cpu_tasks_executed = 0;     /**< Tasks run on CPU devices on this node. */
        std::size_t gpu_tasks_executed = 0;     /**< Tasks run on GPU devices attached to this node. */

        std::size_t tasks_popped_local = 0;     /**< Tasks taken from the worker's own queue. */
        std::size_t tasks_stolen_intra_numa = 0;/**< Steals from a victim on the same NUMA node. */
        std::size_t tasks_stolen_inter_numa = 0;/**< Steals that crossed to another NUMA node. */

        std::size_t local_memory_allocations_bytes = 0; /**< USM bytes pinned to this node. Reserved: always 0 in v1. */
    };

    /**
     * @brief Describes the distributed master-worker offload and network metrics.
     *
     * Tracks tasks shipped to remote workers and bytes transferred across the network.
     */
    struct DistributedStatistics
    {
        std::size_t tasks_offloaded_to_network = 0;  /**< Tasks shipped to remote workers. */
        std::size_t tasks_received_from_network = 0; /**< Remote tasks executed locally. Reserved: always 0 in v1. */

        std::size_t network_bytes_tx = 0;            /**< Total bytes sent. Reserved: always 0 in v1. */
        std::size_t network_bytes_rx = 0;            /**< Total bytes received. Reserved: always 0 in v1. */
    };

    /**
     * @brief Names the execution path that submitted a run's device work.
     */
    enum class ExecutionLayer : std::uint8_t
    {
        Scheduler, /**< Nodes dispatched by the worker pool as their predecessors complete. */
        Chain      /**< Nodes submitted in compiled order to one in-order queue, one wait per run. */
    };

    /**
     * @brief Summarizes the execution statistics of a single graph run.
     *
     * Holds the run's outcome, NUMA topology breakdown and distributed network metrics. Timing is
     * not part of it: a RunObserver receives it.
     */
    struct RunReport
    {
        // A. Overall status
        bool is_successful = true;                  /**< Whether the run completed without error. */
        bool cancelled = false;                     /**< Whether the run stopped early on a cancellation request. */
        std::size_t total_tasks_executed = 0;       /**< Local (all NUMA) + remote tasks. */
        ExecutionLayer layer = ExecutionLayer::Scheduler; /**< Path that submitted this run's device work. */

        // B. Waiting
        double network_latency_wait_ms = 0.0;       /**< Idle time awaiting remote results. Reserved: always 0 in v1. */

        // C. NUMA topology breakdown
        std::vector<NUMANodeStatistics> numa_statistics;      /**< Exactly one entry in v1, with the run's totals; every task counts under device 0's class. */

        // D. Distributed breakdown
        DistributedStatistics distributed_statistics;         /**< Offload and network figures; zero with no cluster. */

        // E. Diagnostics
        std::vector<std::string> warnings;          /**< Non-fatal notes raised during the run. Reserved: always empty in v1. */

        /**
         * @brief Returns the report to its as-constructed state, keeping capacity.
         * @see modules/foundation/report/README.md
         */
        void reset() noexcept
        {
            is_successful = true;
            cancelled = false;
            total_tasks_executed = 0;
            layer = ExecutionLayer::Scheduler;

            network_latency_wait_ms = 0.0;

            numa_statistics.clear();
            distributed_statistics = DistributedStatistics{};

            warnings.clear();
        }
    };

} // namespace SushiRuntime
