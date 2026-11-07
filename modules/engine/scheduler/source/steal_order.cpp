/**************************************************************************/
/* steal_order.cpp                                                       */
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
 * @file steal_order.cpp
 * @brief Builds the NUMA-aware steal order of a worker pool.
 * @author Mustafa Garip
 * @see modules/engine/scheduler/README.md
 */

#include <SushiRuntime/scheduler/steal_order.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <random>
#include <vector>

#include <SushiRuntime/log/logger.hpp>
#include <SushiRuntime/scheduler/scheduler_log_category.hpp>
#include <SushiRuntime/topology/topology_view.hpp>

namespace SushiRuntime
{
    namespace Internal::Scheduler
    {
        StealOrder build_steal_order(const Internal::Topology::TopologyView& topology, std::size_t workers,
                                     PinPolicy pinning, std::size_t first_core)
        {
            StealOrder order;
            const std::vector<int>& core_to_numa = topology.core_to_numa();

            order.worker_numa.assign(workers, 0);
            if (pinning == PinPolicy::Pin)
            {
                for (std::size_t i = 0; i < workers; ++i)
                {
                    // Worker i is pinned to core first_core + i; an unknown core maps to node 0.
                    const std::size_t core = first_core + i;
                    if (core < core_to_numa.size())
                        order.worker_numa[i] = core_to_numa[core];
                    else
                        SR_LOG_DEBUG(K_SCHEDULER_LOG_CATEGORY, "Worker {} is pinned to core {}, which the topology "
                                     "does not list; it is assigned to NUMA node 0.", i, core);
                }
            }

            order.victim_order.assign(workers, {});
            order.local_tier_len.assign(workers, 0);

            std::mt19937 rng(0x51515151u);
            for (std::size_t th = 0; th < workers; ++th)
            {
                const int my_node = order.worker_numa[th];

                struct Victim { int id; int dist; std::uint64_t key; };
                std::vector<Victim> victims;
                victims.reserve(workers > 0 ? workers - 1 : 0);

                int local = 0;
                for (std::size_t j = 0; j < workers; ++j)
                {
                    if (j == th) continue;
                    const int dist = topology.numa_distance(my_node, order.worker_numa[j]);
                    victims.push_back({ static_cast<int>(j), dist, rng() });
                    if (order.worker_numa[j] == my_node) ++local;
                }

                std::sort(victims.begin(), victims.end(),
                          [](const Victim& a, const Victim& b)
                          {
                              if (a.dist != b.dist) return a.dist < b.dist;
                              return a.key < b.key; // random, stable tie-break
                          });

                auto& victims_of = order.victim_order[th];
                victims_of.reserve(victims.size());
                for (const auto& v : victims) victims_of.push_back(v.id);

                order.local_tier_len[th] = local;
            }
                    return order;
        }
    } // namespace Internal::Scheduler
} // namespace SushiRuntime
