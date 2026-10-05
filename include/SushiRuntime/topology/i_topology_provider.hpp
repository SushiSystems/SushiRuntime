/****************************************************************/
/* i_topology_provider.hpp                                      */
/* SushiRuntime - https://github.com/SushiSystems/SushiRuntime  */
/* Copyright (c) 2026 Sushi Systems                             */
/* Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.    */
/* Commercial use requires a licence from Sushi Systems.        */
/****************************************************************/

#pragma once

#include <vector>
#include <SushiRuntime/core/export.hpp>
#include <SushiRuntime/topology/hw_discovery.hpp>

namespace SushiRuntime
{
    namespace Topology
    {
        /**
         * @brief Interface over hardware discovery and NUMA mapping.
         *
         * RuntimeContext depends on this interface instead of the concrete hwloc/SYCL
         * machinery, making topology substitutable at construction time:
         *
         *   - HwlocTopologyProvider      — real bare-metal discovery.
         *   - RemappingTopologyProvider  — real devices with a synthetic NUMA layout,
         *                                 so multi-node logic runs on single-node hardware.
         */
        class SUSHIRUNTIME_API ITopologyProvider
        {
            public:
                virtual ~ITopologyProvider() = default;

                /**
                 * @brief Discovers compute devices and assigns their NUMA ids.
                 * @param config Filter controlling which device classes are returned.
                 * @return Hardware nodes with numa_node_id resolved (>= 0) or -1 if unknown.
                 */
                virtual std::vector<HardwareNode> enumerate(const HardwareFilterConfig& config = {}) = 0;

                /**
                 * @brief Maps each logical CPU (OS cpu index) to its NUMA node.
                 *
                 * Indexed by the same logical-core id that thread pinning uses
                 * (pin_current_thread_to_core), so a worker bound to core c has
                 * NUMA node result[c] — with the default settings that is worker i
                 * on core i, and under a reserved-core configuration it is worker i
                 * on core (first_core + i). Enables NUMA-aware, hierarchical work
                 * stealing in the scheduler.
                 *
                 * Default returns empty → callers fall back to a single flat tier
                 * (NUMA-blind stealing). Override to provide real/synthetic maps.
                 */
                virtual std::vector<int> cpu_core_to_numa() const { return {}; }
        };

        /**
         * @brief Interface over the NUMA distance (ACPI SLIT) matrix.
         *
         * Kept separate from device discovery so it can be independently replaced.
         * Most topology-aware scheduling logic needs only distances, not device lists.
         */
        class SUSHIRUNTIME_API INumaDistanceModel
        {
            public:
                virtual ~INumaDistanceModel() = default;

                /**
                 * @brief Latency cost between two NUMA nodes.
                 * @return 10 for local (a == b), > 10 for remote (ACPI convention).
                 */
                virtual int distance(int numa_a, int numa_b) const = 0;

                /** @brief Number of NUMA nodes the model knows about. */
                virtual int node_count() const = 0;
        };

    } // namespace Topology
} // namespace SushiRuntime
