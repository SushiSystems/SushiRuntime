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
 * @see modules/engine/partitioning/README.md
 * @author Mustafa Garip
 */

#include <SushiRuntime/partitioning/list_scheduling_partitioner.hpp>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <numeric>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <SushiRuntime/partitioning/fill_plan.hpp>
#include <SushiRuntime/partitioning/list_schedule.hpp>
#include <SushiRuntime/partitioning/partition_model.hpp>
#include <SushiRuntime/partitioning/planned_homes.hpp>
#include <SushiRuntime/partitioning/split_expansion.hpp>
#include <SushiRuntime/partitioning/split_sizing.hpp>

namespace SushiRuntime
{
    namespace Internal::Coherence
    {
        namespace
        {
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

            /** @brief Holds a scheduled plan and whether the floor supplied it. */
            struct Schedule
            {
                ScheduledPlan plan;       /**< Placement, run order and modelled step. */
                bool floor_taken = false; /**< Whether the floor replaced the scheduled plan. */
            };

            /**
             * @brief Returns the scheduled plan of model, or its floor when the floor is faster.
             * @return Nothing when no plan fits.
             */
            [[nodiscard]] std::optional<Schedule> schedule(const PartitionModel& model)
            {
                std::optional<ScheduledPlan> scheduled = list_schedule(model);
                const std::optional<FloorPlan> floor = floor_plan(model);
                if (scheduled && (!floor || !(floor->step_time < scheduled->step_time)))
                    return Schedule{std::move(*scheduled), false};
                if (!floor)
                    return std::nullopt;
                ScheduledPlan taken{floor->placement, std::vector<std::size_t>(model.node_count()), floor->step_time};
                std::iota(taken.order.begin(), taken.order.end(), std::size_t{0});
                return Schedule{std::move(taken), true};
            }

            /**
             * @brief Returns the assignment of the original nodes from a schedule of model.
             * @param origin Original node of each node of model, in order.
             */
            [[nodiscard]] PartitionAssignment assignment_of(const PartitionModel& model, const Schedule& chosen,
                                                            const std::vector<std::size_t>& origin)
            {
                PartitionAssignment assignment;
                assignment.floor_taken = chosen.floor_taken;
                assignment.step_time = chosen.plan.step_time;
                assignment.homes = planned_homes(model, chosen.plan.placement);
                for (std::size_t node = 0; node < chosen.plan.placement.size(); ++node)
                {
                    if (origin[node] == assignment.node_devices.size())
                        assignment.node_devices.push_back(model.device_key(chosen.plan.placement[node]));
                }
                return assignment;
            }

            /** @brief Returns whether node needs more bytes than any pool holds. */
            [[nodiscard]] bool fits_no_pool(const PartitionModel& model, std::size_t node)
            {
                const PoolOccupancy empty = model.empty_occupancy();
                for (std::size_t pool = 0; pool < model.pool_count(); ++pool)
                {
                    if (empty.used(pool) + empty.added_bytes(node, pool) <= model.usable_bytes(pool))
                        return false;
                }
                return true;
            }

            /** @brief Returns the key of every device of model, in device order. */
            [[nodiscard]] std::vector<DeviceKey> device_keys(const PartitionModel& model)
            {
                std::vector<DeviceKey> keys;
                keys.reserve(model.device_count());
                for (std::size_t device = 0; device < model.device_count(); ++device)
                    keys.push_back(model.device_key(device));
                return keys;
            }

            /** @brief Returns the parts node is cut into, or nothing when it declares no split it may take. */
            [[nodiscard]] std::vector<SizedPart> declared_parts(const PartitionModel& model, std::size_t node)
            {
                const PartitionNode& entry = model.problem().nodes[node];
                if (!entry.split || !entry.split->regions || entry.split->extent < 2 || model.fixed_device(node))
                    return {};
                std::vector<double> times(model.device_count(), 0.0);
                for (std::size_t device = 0; device < model.device_count(); ++device)
                    times[device] = model.node_time(node, device);
                std::vector<SizedPart> parts = size_split(entry.split->axis, entry.split->extent, times);
                if (parts.size() < 2)
                    return {};
                return parts;
            }

            /**
             * @brief Returns the assignment of the problem with each node that has parts cut into them.
             * @return Nothing when that problem fits in no plan.
             */
            [[nodiscard]] std::optional<PartitionAssignment> split_assignment(
                const PartitionProblem& problem, const CostModel& costs,
                const std::vector<std::vector<SizedPart>>& parts, const std::vector<DeviceKey>& devices)
            {
                const SplitExpansion expansion = expand_splits(problem, parts, devices);
                Result<PartitionModel, PartitionRefusal> built = PartitionModel::build(expansion.problem, costs);
                if (!built)
                    return std::nullopt;
                const std::optional<Schedule> chosen = schedule(built.value());
                if (!chosen)
                    return std::nullopt;
                PartitionAssignment assignment = assignment_of(built.value(), *chosen, expansion.origin);
                assignment.splits = planned_splits(problem, parts, devices);
                return assignment;
            }
        } // namespace

        ListSchedulingPartitioner::ListSchedulingPartitioner(std::shared_ptr<const CostModel> costs,
                                                             Splitting splitting)
            : costs_(std::move(costs)), splitting_(splitting)
        {

        }

        Result<PartitionAssignment, PartitionRefusal> ListSchedulingPartitioner::partition(
            const PartitionProblem& problem) const
        {
            Result<PartitionModel, PartitionRefusal> built = PartitionModel::build(problem, *costs_);
            if (!built)
                return make_error(std::move(built.error()));
            const PartitionModel& model = built.value();

            std::vector<std::size_t> origin(model.node_count());
            std::iota(origin.begin(), origin.end(), std::size_t{0});
            std::optional<PartitionAssignment> best;
            if (const std::optional<Schedule> whole = schedule(model))
                best = assignment_of(model, *whole, origin);

            if (splitting_ == Splitting::Allowed)
            {
                const std::vector<DeviceKey> devices = device_keys(model);
                std::vector<std::vector<SizedPart>> taken(model.node_count());
                std::vector<std::size_t> optional_nodes;
                bool forced = false;
                for (std::size_t node = 0; node < model.node_count(); ++node)
                {
                    std::vector<SizedPart> parts = declared_parts(model, node);
                    if (parts.empty())
                        continue;
                    if (fits_no_pool(model, node))
                    {
                        taken[node] = std::move(parts);
                        forced = true;
                    }
                    else
                    {
                        optional_nodes.push_back(node);
                    }
                }
                if (forced)
                {
                    std::optional<PartitionAssignment> cut = split_assignment(problem, *costs_, taken, devices);
                    if (cut && (!best || cut->step_time < best->step_time))
                        best = std::move(cut);
                }
                for (const std::size_t node : optional_nodes)
                {
                    std::vector<std::vector<SizedPart>> trial = taken;
                    trial[node] = declared_parts(model, node);
                    std::optional<PartitionAssignment> cut = split_assignment(problem, *costs_, trial, devices);
                    if (cut && (!best || cut->step_time < best->step_time))
                    {
                        best = std::move(cut);
                        taken = std::move(trial);
                    }
                }
            }

            if (!best)
                return make_error(capacity_refusal(model));
            return std::move(*best);
        }
    } // namespace Internal::Coherence
} // namespace SushiRuntime
