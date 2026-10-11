/**************************************************************************/
/* remapping_topology_provider.hpp                                        */
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
 * @file remapping_topology_provider.hpp
 * @brief Declares RemappingTopologyProvider, which overlays a synthetic NUMA layout.
 * @author Mustafa Garip
 */

#pragma once

#include <memory>
#include <SushiRuntime/core/export.hpp>
#include <SushiRuntime/topology/i_topology_provider.hpp>

namespace SushiRuntime
{
    namespace Internal::Topology
    {
        /**
         * @brief Decorator that overlays a synthetic NUMA layout on real devices.
         *
         * Wraps an underlying ITopologyProvider, preserving real SYCL devices while reassigning
         * NUMA node IDs across a configurable number of synthetic nodes in a round-robin fashion.
         */
        class SUSHIRUNTIME_API RemappingTopologyProvider : public ITopologyProvider
        {
            public:
                /**
                 * @brief Constructs a provider that overrides NUMA node assignments.
                 *
                 * Wraps the real provider to distribute devices across synthetic nodes.
                 * @param inner The underlying real topology provider. Takes shared ownership.
                 * @param fake_node_count The target number of synthetic NUMA nodes to distribute across.
                 * @warning The inner provider must not be null.
                 */
                RemappingTopologyProvider(std::shared_ptr<ITopologyProvider> inner, int fake_node_count);

                /**
                 * @brief Discovers hardware devices and assigns them to synthetic NUMA nodes.
                 *
                 * Calls the underlying provider and modifies the resulting NUMA assignments using round-robin distribution.
                 * @param config The hardware filter configuration defining inclusion or exclusion rules.
                 * @return The list of hardware nodes with overridden NUMA assignments.
                 */
                std::vector<HardwareNode> enumerate(const HardwareFilterConfig& config = {}) override;

                /**
                 * @brief Maps CPU cores to synthetic NUMA nodes.
                 *
                 * Queries the underlying core mapping and re-assigns them across the synthetic nodes.
                 * @return An array mapping core ID to the assigned synthetic NUMA node ID.
                 */
                std::vector<int> cpu_core_to_numa() const override;

            private:
                std::shared_ptr<ITopologyProvider> inner_;
                int fake_node_count_;
        };

    } // namespace Topology
} // namespace SushiRuntime
