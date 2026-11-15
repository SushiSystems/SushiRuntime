/**************************************************************************/
/* list_scheduling_partitioner.cpp                                        */
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
 * @file list_scheduling_partitioner.cpp
 * @brief Implements ListSchedulingPartitioner.
 * @see modules/engine/coherence/README.md
 * @author Mustafa Garip
 */

#include <SushiRuntime/coherence/list_scheduling_partitioner.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <map>
#include <limits>
#include <numeric>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <SushiRuntime/coherence/fill_plan.hpp>
#include <SushiRuntime/coherence/partition_model.hpp>

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
                for (std::size_t node = model.node_count(); node-- > 0;)
                {
                    samples.clear();
                    for (std::size_t device = 0; device < devices; ++device)
                        samples.push_back(model.node_time(node, device));
                    double successor_part = 0.0;
                    for (const std::size_t edge : model.edges_out_of(node))
                    {
                        std::vector<double> crossings;
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

            /**
             * @brief Places every node of model by earliest finish in descending rank.
             * @return Nothing when a movable node fits in no pool.
             */
            [[nodiscard]] std::optional<std::vector<std::size_t>> place(const PartitionModel& model)
            {
                const std::vector<double> ranks = upward_ranks(model);
                std::vector<std::size_t> order(model.node_count());
                std::iota(order.begin(), order.end(), std::size_t{0});
                std::stable_sort(order.begin(), order.end(),
                                 [&](std::size_t left, std::size_t right) { return ranks[left] > ranks[right]; });

                std::vector<std::size_t> placement(model.node_count(), 0);
                std::vector<double> finish(model.node_count(), 0.0);
                std::vector<double> device_free(model.device_count(), 0.0);
                PoolOccupancy occupancy = model.empty_occupancy();

                const auto finish_on = [&](std::size_t node, std::size_t device)
                {
                    double start = device_free[device];
                    for (const std::size_t edge : model.edges_into(node))
                    {
                        const std::size_t source = model.problem().edges[edge].from;
                        start = std::max(start, finish[source] + model.edge_time(edge, placement[source], device));
                    }
                    return start + model.node_time(node, device);
                };

                for (const std::size_t node : order)
                {
                    std::optional<std::size_t> chosen = model.fixed_device(node);
                    double chosen_finish = chosen ? finish_on(node, *chosen) : 0.0;
                    if (!chosen)
                    {
                        for (std::size_t device = 0; device < model.device_count(); ++device)
                        {
                            const std::size_t pool = model.pool_of(device);
                            if (occupancy.used(pool) + occupancy.added_bytes(node, pool) > model.usable_bytes(pool))
                                continue;
                            const double candidate = finish_on(node, device);
                            if (!chosen || candidate < chosen_finish)
                            {
                                chosen = device;
                                chosen_finish = candidate;
                            }
                        }
                    }
                    if (!chosen)
                        return std::nullopt;
                    placement[node] = *chosen;
                    finish[node] = chosen_finish;
                    device_free[*chosen] = std::max(device_free[*chosen], chosen_finish);
                    occupancy.add(node, model.pool_of(*chosen));
                }
                return placement;
            }

            /** @brief Moves nodes onto a neighbour's device while the step falls and the plan fits. */
            void refine(const PartitionModel& model, std::vector<std::size_t>& placement)
            {
                double best = model.step_time(placement);
                bool moved = true;
                while (moved)
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
                                const double step = model.step_time(placement);
                                if (step < best && model.fits(placement))
                                {
                                    best = step;
                                    moved = true;
                                    continue;
                                }
                                placement[node] = current;
                            }
                        }
                    }
                }
            }

            /** @brief Returns the home device of every planned buffer a node touches, writers first. */
            [[nodiscard]] std::map<GlobalHandle, DeviceKey> homes_of(const PartitionModel& model,
                                                                     const std::vector<std::size_t>& placement)
            {
                std::map<GlobalHandle, DeviceKey> homes;
                const PartitionProblem& problem = model.problem();
                for (const bool writers : {true, false})
                {
                    for (std::size_t node = 0; node < problem.nodes.size(); ++node)
                    {
                        const PartitionNode& entry = problem.nodes[node];
                        for (const std::size_t buffer : writers ? entry.writes : entry.reads)
                        {
                            if (!problem.buffers[buffer].device)
                                homes.emplace(problem.buffers[buffer].handle, model.device_key(placement[node]));
                        }
                    }
                }
                return homes;
            }

            /** @brief Returns the refusal of a job that fits in no plan, with the bytes it puts in each pool. */
            [[nodiscard]] PartitionRefusal capacity_refusal(const PartitionModel& model)
            {
                std::size_t widest = 0;
                std::uint64_t widest_bytes = 0;
                PoolOccupancy empty = model.empty_occupancy();
                for (std::size_t node = 0; node < model.node_count(); ++node)
                {
                    std::uint64_t smallest = std::numeric_limits<std::uint64_t>::max();
                    bool fits_somewhere = false;
                    for (std::size_t pool = 0; pool < model.pool_count(); ++pool)
                    {
                        const std::uint64_t bytes = empty.used(pool) + empty.added_bytes(node, pool);
                        smallest = std::min(smallest, bytes);
                        fits_somewhere = fits_somewhere || bytes <= model.usable_bytes(pool);
                    }
                    if (!fits_somewhere && !model.fixed_device(node) && smallest > widest_bytes)
                    {
                        widest = node;
                        widest_bytes = smallest;
                    }
                }

                if (widest_bytes != 0)
                {
                    PartitionRefusal refusal{Error{ErrorCode::AllocationFailed,
                                                   "node " + std::to_string(widest) +
                                                       " needs more bytes than any pool holds"},
                                             {}};
                    refusal.pools = model.pool_demand(std::vector<std::size_t>{});
                    for (std::size_t pool = 0; pool < model.pool_count(); ++pool)
                        refusal.pools[pool].required = empty.used(pool) + empty.added_bytes(widest, pool);
                    return refusal;
                }

                std::vector<std::size_t> all(model.device_count());
                std::iota(all.begin(), all.end(), std::size_t{0});
                return PartitionRefusal{Error{ErrorCode::AllocationFailed, "the job needs more bytes than the pools hold"},
                                        model.pool_demand(fill_plan(model, all).placement)};
            }
        } // namespace

        ListSchedulingPartitioner::ListSchedulingPartitioner(std::shared_ptr<const CostModel> costs)
            : costs_(std::move(costs))
        {

        }

        Result<PartitionAssignment, PartitionRefusal> ListSchedulingPartitioner::partition(
            const PartitionProblem& problem) const
        {
            Result<PartitionModel, PartitionRefusal> built = PartitionModel::build(problem, *costs_);
            if (!built)
                return make_error(std::move(built.error()));
            const PartitionModel& model = built.value();

            std::optional<std::vector<std::size_t>> scheduled = place(model);
            if (scheduled)
                refine(model, *scheduled);
            const std::optional<FloorPlan> floor = floor_plan(model);

            PartitionAssignment assignment;
            std::vector<std::size_t> placement;
            if (scheduled && (!floor || !(floor->step_time < model.step_time(*scheduled))))
            {
                placement = std::move(*scheduled);
            }
            else if (floor)
            {
                placement = floor->placement;
                assignment.floor_taken = true;
            }
            else
            {
                return make_error(capacity_refusal(model));
            }

            assignment.step_time = model.step_time(placement);
            assignment.homes = homes_of(model, placement);
            assignment.node_devices.reserve(placement.size());
            for (const std::size_t device : placement)
                assignment.node_devices.push_back(model.device_key(device));
            return assignment;
        }
    } // namespace Internal::Coherence
} // namespace SushiRuntime
