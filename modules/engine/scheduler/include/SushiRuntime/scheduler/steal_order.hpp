/**************************************************************************/
/* steal_order.hpp                                                       */
/**************************************************************************/
/*                          This file is part of:                         */
/*                              SushiRuntime                              */
/*              https://github.com/SushiSystems/SushiRuntime              */
/*                         https://sushisystems.io                        */
/**************************************************************************/
/* Copyright (c) 2026-present Mustafa Garip & Sushi Systems               */
/*                                                                        */
/* Licensed under the Apache License, Version 2.0 (the "License"); you    */
/* may not use this file except in compliance with the License. You may   */
/* obtain a copy of the License at                                        */
/*                                                                        */
/*     https://www.apache.org/licenses/LICENSE-2.0                        */
/*                                                                        */
/* Unless required by applicable law or agreed to in writing, software    */
/* distributed under the License is distributed on an "AS IS" BASIS,      */
/* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or        */
/* implied. See the License for the specific language governing           */
/* permissions and limitations under the License.                         */
/**************************************************************************/

/**
 * @file steal_order.hpp
 * @brief Declares build_steal_order(), the NUMA-aware order each worker steals from the others in.
 * @author Mustafa Garip
 * @see modules/engine/scheduler/README.md
 */

#pragma once

#include <cstddef>
#include <vector>

#include <SushiRuntime/core/export.hpp>
#include <SushiRuntime/machine/pin_policy.hpp>

namespace SushiRuntime
{
    namespace Internal::Topology { class TopologyView; }

    namespace Internal::Scheduler
    {
        /** @brief The NUMA node of each worker and the order it steals from the others in. */
        struct StealOrder
        {
            std::vector<int> worker_numa;               /**< NUMA node of each worker. */
            std::vector<std::vector<int>> victim_order; /**< Victims of each worker, nearest first. */
            std::vector<int> local_tier_len;            /**< Leading victims of each worker on its own node. */
        };

        /**
         * @brief Builds a pool's steal order: victims by NUMA distance, ties by a fixed-seed key.
         * @param topology The core-to-NUMA map and the distances.
         * @param workers Worker count.
         * @param pinning Whether worker i is pinned to core first_core + i; a floating worker is on node 0.
         * @param first_core Core worker 0 is pinned to.
         * @see modules/engine/scheduler/README.md
         */
        [[nodiscard]] SUSHIRUNTIME_API StealOrder build_steal_order(const Internal::Topology::TopologyView& topology,
                                                                    std::size_t workers, PinPolicy pinning,
                                                                    std::size_t first_core);
    } // namespace Internal::Scheduler
} // namespace SushiRuntime
