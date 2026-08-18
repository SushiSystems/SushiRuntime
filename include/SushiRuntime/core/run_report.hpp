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

            std::size_t remote_nodes_involved = 0;       /**< Live remote workers that took part. */
        };

        /**
         * @brief Per-node (per-system) timing accumulated over a run.
         *
         * The breakdown a frame budget is tuned against: how long each node spent on
         * the device versus on a worker thread, summed across every step the run
         * replayed. Populated only when the runtime was created with profiling on;
         * otherwise the report carries no node rows.
         */
        struct NodeTiming
        {
            std::size_t plan_index = 0;     /**< Stable index of the node in the compiled plan; the identity. */
            std::string name;               /**< Node label, or the default task id when none was set. */
            std::size_t invocations = 0;    /**< Times the node was dispatched across the run. */
            double device_ms = 0.0;         /**< Summed kernel device time (from SYCL event profiling). */
            double host_ms = 0.0;           /**< Summed wall-clock for native/host work run on a worker. */
        };

        /**
         * @brief Per-worker time breakdown that attributes where a run's time went.
         *
         * Separates real work from starvation and scheduling overhead so a stall can
         * be attributed: a worker mostly idle or stealing means the graph is too
         * narrow, while high polling time points at completion/sync overhead.
         * Populated only when profiling is on.
         */
        struct WorkerTiming
        {
            int worker_id = -1;      /**< Worker thread this row describes. */
            double busy_ms = 0.0;    /**< Time spent inside task execution. */
            double stealing_ms = 0.0;/**< Time spent searching victims for work to steal. */
            double polling_ms = 0.0; /**< Time spent polling in-flight events for completion. */
            double idle_ms = 0.0;    /**< Time spent backing off or waiting to be armed. */
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
            double scheduling_overhead_ms = 0.0;        /**< Time workers spent polling for completions. */
            double network_latency_wait_ms = 0.0;       /**< Idle time awaiting remote results. Reserved: always 0 in v1. */

            // C. NUMA topology breakdown
            std::vector<NumaNodeStatistics> numa_statistics;      /**< Per-node breakdown (one entry per active node). */

            // D. Distributed breakdown
            DistributedStatistics distributed_statistics;         /**< Offload/network stats (Master mode). */

            // E. PCIe / SYCL host-device transfers (reserved)
            std::size_t host_to_device_bytes = 0;       /**< Bytes moved by explicit write_range calls during the run. */
            std::size_t device_to_host_bytes = 0;       /**< Bytes moved by explicit read_range calls during the run. */

            // F. Diagnostics
            std::vector<std::string> warnings;          /**< Non-fatal notes raised during the run. */

            // G. Profiling breakdown (populated only when the runtime has profiling on)
            std::vector<NodeTiming>   node_timings;     /**< Per-node device/host timing; empty when profiling is off. */
            std::vector<WorkerTiming> worker_timings;   /**< Per-worker busy/steal/poll/idle split; empty when profiling is off. */

            /**
             * @brief Returns the report to its as-constructed state, keeping capacity.
             *
             * What makes a report reusable. Assigning a fresh RunReport over an old
             * one would free every vector's storage and the next run would allocate
             * it again; clearing keeps the buffers, so a report handed to
             * `run(RunReport&)` every tick stops allocating after the first run.
             *
             * The retained capacity covers the rows, not what a row owns: clearing
             * node_timings destroys each row's name string. That is why the
             * zero-allocation claim is stated for a profiling-off steady state — with
             * profiling on, the node labels are rebuilt each run by design.
             */
            void reset() noexcept
            {
                is_successful = true;
                cancelled = false;
                total_tasks_executed = 0;

                total_duration_ms = 0.0;
                graph_analysis_duration_ms = 0.0;
                scheduling_overhead_ms = 0.0;
                network_latency_wait_ms = 0.0;

                numa_statistics.clear();
                distributed_statistics = DistributedStatistics{};

                host_to_device_bytes = 0;
                device_to_host_bytes = 0;

                warnings.clear();
                node_timings.clear();
                worker_timings.clear();
            }
        };

    } // namespace Core
} // namespace SushiRuntime
