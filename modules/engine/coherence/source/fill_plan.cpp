/**************************************************************************/
/* fill_plan.cpp                                                          */
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

/**
 * @file fill_plan.cpp
 * @brief Implements fill_plan and floor_plan.
 * @see modules/engine/coherence/README.md
 * @author Mustafa Garip
 */

#include <SushiRuntime/coherence/fill_plan.hpp>

#include <algorithm>
#include <cstdint>
#include <utility>

namespace SushiRuntime
{
    namespace Internal::Coherence
    {
        namespace
        {
            /** @brief Returns the seconds device takes to run every movable node of model alone. */
            [[nodiscard]] double alone_time(const PartitionModel& model, std::size_t device)
            {
                double seconds = 0.0;
                for (std::size_t node = 0; node < model.node_count(); ++node)
                {
                    if (!model.fixed_device(node))
                        seconds += model.node_time(node, device);
                }
                return seconds;
            }
        } // namespace

        FillWalk fill_plan(const PartitionModel& model, const std::vector<std::size_t>& subset)
        {
            std::vector<std::pair<double, std::size_t>> order;
            order.reserve(subset.size());
            for (const std::size_t device : subset)
                order.emplace_back(alone_time(model, device), device);
            std::sort(order.begin(), order.end());

            FillWalk walk;
            walk.placement.assign(model.node_count(), 0);
            PoolOccupancy occupancy = model.empty_occupancy();
            std::size_t current = 0;
            for (std::size_t node = 0; node < model.node_count(); ++node)
            {
                std::size_t device = 0;
                if (const std::optional<std::size_t> fixed = model.fixed_device(node))
                {
                    device = *fixed;
                }
                else
                {
                    while (current + 1 < order.size())
                    {
                        const std::size_t pool = model.pool_of(order[current].second);
                        if (occupancy.used(pool) + occupancy.added_bytes(node, pool) <= model.usable_bytes(pool))
                            break;
                        ++current;
                    }
                    device = order[current].second;
                }
                walk.placement[node] = device;
                occupancy.add(node, model.pool_of(device));
            }

            walk.fits = true;
            for (std::size_t pool = 0; pool < model.pool_count(); ++pool)
            {
                if (occupancy.used(pool) > model.usable_bytes(pool))
                    walk.fits = false;
            }
            return walk;
        }

        std::optional<FloorPlan> floor_plan(const PartitionModel& model)
        {
            std::optional<FloorPlan> best;
            const std::uint32_t masks = 1U << model.device_count();
            for (std::uint32_t mask = 1; mask < masks; ++mask)
            {
                std::vector<std::size_t> subset;
                for (std::size_t device = 0; device < model.device_count(); ++device)
                {
                    if ((mask >> device) & 1U)
                        subset.push_back(device);
                }
                FillWalk walk = fill_plan(model, subset);
                if (!walk.fits)
                    continue;
                const double step = model.step_time(walk.placement);
                if (best && !(step < best->step_time))
                    continue;
                best = FloorPlan{std::move(walk.placement), std::move(subset), step};
            }
            return best;
        }
    } // namespace Internal::Coherence
} // namespace SushiRuntime
