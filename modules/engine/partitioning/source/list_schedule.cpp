/**************************************************************************/
/* list_schedule.cpp                                                      */
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
 * @file list_schedule.cpp
 * @brief Defines rank_order(), place_by_rank(), refine_schedule() and list_schedule().
 * @see modules/engine/partitioning/README.md
 * @author Mustafa Garip
 */

#include <SushiRuntime/partitioning/list_schedule.hpp>

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <numeric>
#include <utility>

#include <SushiRuntime/partitioning/pool_occupancy.hpp>
#include <SushiRuntime/partitioning/step_simulation.hpp>

namespace SushiRuntime
{
    namespace Internal::Coherence
    {
        namespace
        {
            /** @brief Returns the mean of the finite values, or zero when there is none. */
            [[nodiscard]] double finite_mean(const std::vector<double>& values)
            {
                double sum = 0.0;
                std::size_t count = 0;
                for (const double value : values)
                {
                    if (std::isfinite(value))
                    {
                        sum += value;
                        ++count;
                    }
                }
                return count == 0 ? 0.0 : sum / static_cast<double>(count);
            }

            /** @brief Returns the upward rank of every node of model. */
            [[nodiscard]] std::vector<double> upward_ranks(const PartitionModel& model)
            {
                const std::size_t devices = model.device_count();
                std::vector<double> ranks(model.node_count(), 0.0);
                std::vector<double> samples;
                std::vector<double> crossings;
                for (std::size_t node = model.node_count(); node-- > 0;)
                {
                    samples.clear();
                    for (std::size_t device = 0; device < devices; ++device)
                        samples.push_back(model.node_time(node, device));
                    double successor_part = 0.0;
                    for (const std::size_t edge : model.edges_out_of(node))
                    {
                        crossings.clear();
                        for (std::size_t source = 0; source < devices; ++source)
                        {
                            for (std::size_t target = 0; target < devices; ++target)
                            {
                                if (source != target)
                                    crossings.push_back(model.edge_time(edge, source, target));
                            }
                        }
                        const std::size_t successor = model.problem().edges[edge].to;
                        successor_part = std::max(successor_part, finite_mean(crossings) + ranks[successor]);
                    }
                    ranks[node] = finite_mean(samples) + successor_part;
                }
                return ranks;
            }
        } // namespace

        std::vector<std::size_t> rank_order(const PartitionModel& model)
        {
            const std::vector<double> ranks = upward_ranks(model);
            std::vector<std::size_t> order(model.node_count());
            std::iota(order.begin(), order.end(), std::size_t{0});
            std::stable_sort(order.begin(), order.end(),
                             [&](std::size_t left, std::size_t right) { return ranks[left] > ranks[right]; });
            return order;
        }

        std::optional<ScheduledPlan> place_by_rank(const PartitionModel& model)
        {
            ScheduledPlan plan;
            plan.order = rank_order(model);
            plan.placement.assign(model.node_count(), 0);
            StepSimulation simulation(model);
            PoolOccupancy occupancy = model.empty_occupancy();

            for (const std::size_t node : plan.order)
            {
                std::optional<std::size_t> chosen = model.fixed_device(node);
                if (!chosen)
                {
                    double chosen_finish = 0.0;
                    for (std::size_t device = 0; device < model.device_count(); ++device)
                    {
                        const std::size_t pool = model.pool_of(device);
                        if (occupancy.used(pool) + occupancy.added_bytes(node, pool) > model.usable_bytes(pool))
                            continue;
                        const double candidate = simulation.finish_on(node, device);
                        if (!chosen || candidate < chosen_finish)
                        {
                            chosen = device;
                            chosen_finish = candidate;
                        }
                    }
                }
                if (!chosen)
                    return std::nullopt;
                plan.placement[node] = *chosen;
                simulation.commit(node, *chosen);
                occupancy.add(node, model.pool_of(*chosen));
            }
            // A planned home placed after its first reader is known only now, so the step is priced again.
            plan.step_time = model.step_time(plan.placement, plan.order);
            return plan;
        }

        void refine_schedule(const PartitionModel& model, ScheduledPlan& plan)
        {
            std::vector<std::size_t>& placement = plan.placement;
            plan.step_time = model.step_time(placement, plan.order);
            bool moved = true;
            for (std::size_t pass = 0; pass < K_REFINE_PASSES && moved; ++pass)
            {
                moved = false;
                for (std::size_t node = 0; node < model.node_count(); ++node)
                {
                    if (model.fixed_device(node))
                        continue;
                    for (const std::vector<std::size_t>* edges : {&model.edges_into(node), &model.edges_out_of(node)})
                    {
                        for (const std::size_t edge : *edges)
                        {
                            const PartitionEdge& entry = model.problem().edges[edge];
                            const std::size_t neighbour = entry.from == node ? entry.to : entry.from;
                            const std::size_t current = placement[node];
                            if (placement[neighbour] == current)
                                continue;
                            placement[node] = placement[neighbour];
                            const double step = model.step_time(placement, plan.order);
                            if (step < plan.step_time && model.fits(placement))
                            {
                                plan.step_time = step;
                                moved = true;
                                continue;
                            }
                            placement[node] = current;
                        }
                    }
                }
            }
        }

        std::optional<ScheduledPlan> list_schedule(const PartitionModel& model)
        {
            std::optional<ScheduledPlan> plan = place_by_rank(model);
            if (plan)
                refine_schedule(model, *plan);
            return plan;
        }
    } // namespace Internal::Coherence
} // namespace SushiRuntime
