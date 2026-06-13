/**************************************************************************/
/* remapping_topology_provider.hpp                                        */
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

#include <memory>
#include <SushiRuntime/core/export.hpp>
#include <SushiRuntime/topology/i_topology_provider.hpp>

namespace SushiRuntime
{
    namespace Topology
    {
        /**
         * @brief Decorator that overlays a synthetic NUMA layout on real devices.
         *
         * Wraps any inner ITopologyProvider (normally HwlocTopologyProvider),
         * keeps the *real* SYCL devices it discovers — so queues and kernels run
         * for real — but reassigns each node's numa_node_id across a configurable
         * number of fake NUMA nodes (round-robin).
         *
         * This is how multi-node routing/placement is exercised on single-node
         * hardware (Ryzen 7600X + GTX 1060) or under WSL where the real topology
         * collapses to node 0. Pair it with a MatrixDistanceModel sized to
         * @ref fake_node_count.
         */
        class SUSHIRUNTIME_API RemappingTopologyProvider : public ITopologyProvider
        {
            public:
                /**
                 * @param inner            Underlying real provider (must be non-null).
                 * @param fake_node_count  Number of synthetic NUMA nodes to spread across.
                 */
                RemappingTopologyProvider(std::shared_ptr<ITopologyProvider> inner, int fake_node_count);

                std::vector<HardwareNode> enumerate(const HardwareFilterConfig& config = {}) override;
                std::vector<int> cpu_core_to_numa() const override;

            private:
                std::shared_ptr<ITopologyProvider> inner_;
                int fake_node_count_;
        };

    } // namespace Topology
} // namespace SushiRuntime
