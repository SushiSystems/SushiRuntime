/**************************************************************************/
/* partition_model.cpp                                                    */
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
 * @file partition_model.cpp
 * @brief Implements PartitionModel.
 * @see modules/engine/coherence/README.md
 * @author Mustafa Garip
 */

#include <SushiRuntime/coherence/partition_model.hpp>

#include <algorithm>
#include <initializer_list>
#include <limits>
#include <map>
#include <string>
#include <utility>

namespace SushiRuntime
{
    namespace Internal::Coherence
    {
        namespace
        {
            /** @brief Returns a refusal of class code that carries no pool rows. */
            [[nodiscard]] PartitionRefusal refusal(ErrorCode code, std::string message)
            {
                return PartitionRefusal{Error{code, std::move(message)}, {}};
            }

            /** @brief Returns the index of key among devices, or nothing. */
            [[nodiscard]] std::optional<std::size_t> index_of(const std::vector<DeviceKey>& devices,
                                                              const DeviceKey& key)
            {
                const auto found = std::lower_bound(devices.begin(), devices.end(), key);
                if (found == devices.end() || *found != key)
                    return std::nullopt;
                return static_cast<std::size_t>(found - devices.begin());
            }
        } // namespace

        Result<PartitionModel, PartitionRefusal> PartitionModel::build(const PartitionProblem& problem,
                                                                       const CostModel& costs)
        {
            PartitionModel model(problem, costs);

            std::map<DeviceKey, const PartitionDevice*> listed;
            for (const PartitionDevice& device : problem.devices)
                listed.emplace(device.key, &device);

            std::map<std::uint32_t, std::size_t> pool_index;
            for (const auto& entry : listed)
            {
                const DeviceProfile* profile = costs.profile(entry.first);
                if (profile == nullptr)
                    continue;
                model.devices_.push_back(entry.first);
                model.copy_paths_.push_back(entry.second->copy_path);
                const auto inserted = pool_index.emplace(entry.second->pool, model.pool_ids_.size());
                if (inserted.second)
                {
                    model.pool_ids_.push_back(entry.second->pool);
                    model.usable_.push_back(profile->capacity_bytes -
                                            std::min(profile->reserve_bytes, profile->capacity_bytes));
                }
                model.device_pool_.push_back(inserted.first->second);
            }
            if (model.devices_.size() > K_MAX_DEVICES)
                return make_error(refusal(ErrorCode::Unsupported, "the partitioner takes at most sixteen profiled devices"));
            if (!problem.nodes.empty() && model.devices_.empty())
                return make_error(refusal(ErrorCode::InvalidState, "no device has a profile; measure one first"));

            const auto resolve = [&](const DeviceKey& key) -> Result<std::size_t, PartitionRefusal>
            {
                if (listed.find(key) == listed.end())
                    return make_error(refusal(ErrorCode::InvalidArgument, "a node or buffer names a device the problem does not list"));
                const std::optional<std::size_t> index = index_of(model.devices_, key);
                if (!index)
                    return make_error(refusal(ErrorCode::InvalidState, "a node or buffer is on a device with no profile; measure it first"));
                return *index;
            };

            model.buffer_pool_.assign(problem.buffers.size(), std::nullopt);
            std::vector<std::optional<std::size_t>> buffer_device(problem.buffers.size());
            for (std::size_t buffer = 0; buffer < problem.buffers.size(); ++buffer)
            {
                if (!problem.buffers[buffer].device)
                    continue;
                Result<std::size_t, PartitionRefusal> device = resolve(*problem.buffers[buffer].device);
                if (!device)
                    return make_error(device.error());
                buffer_device[buffer] = device.value();
                model.buffer_pool_[buffer] = model.device_pool_[device.value()];
            }

            model.fixed_.assign(problem.nodes.size(), std::nullopt);
            for (std::size_t node = 0; node < problem.nodes.size(); ++node)
            {
                const PartitionNode& entry = problem.nodes[node];
                std::optional<std::size_t> fixed;
                if (entry.placed)
                {
                    Result<std::size_t, PartitionRefusal> device = resolve(*entry.placed);
                    if (!device)
                        return make_error(device.error());
                    fixed = device.value();
                }
                for (const std::vector<std::size_t>* list : {&entry.reads, &entry.writes})
                {
                    for (const std::size_t buffer : *list)
                    {
                        if (buffer >= problem.buffers.size())
                            return make_error(refusal(ErrorCode::InvalidArgument, "a node names a buffer index out of range"));
                    }
                }
                for (const std::size_t buffer : entry.writes)
                {
                    if (!buffer_device[buffer])
                        continue;
                    if (fixed && *fixed != *buffer_device[buffer])
                        return make_error(refusal(ErrorCode::InvalidArgument, "a node is fixed to two devices"));
                    fixed = buffer_device[buffer];
                }
                model.fixed_[node] = fixed;
            }

            model.edges_into_.assign(problem.nodes.size(), {});
            model.edges_out_of_.assign(problem.nodes.size(), {});
            for (std::size_t edge = 0; edge < problem.edges.size(); ++edge)
            {
                const PartitionEdge& entry = problem.edges[edge];
                if (entry.from >= entry.to || entry.to >= problem.nodes.size())
                    return make_error(refusal(ErrorCode::InvalidArgument, "an edge does not run forward in recording order"));
                model.edges_into_[entry.to].push_back(edge);
                model.edges_out_of_[entry.from].push_back(edge);
            }

            model.node_times_.assign(problem.nodes.size() * model.devices_.size(), 0.0);
            for (std::size_t node = 0; node < problem.nodes.size(); ++node)
            {
                for (std::size_t device = 0; device < model.devices_.size(); ++device)
                {
                    model.node_times_[node * model.devices_.size() + device] =
                        costs.node_time(model.devices_[device], problem.nodes[node].cost).value_or(
                            std::numeric_limits<double>::infinity());
                }
            }
            return model;
        }

        double PartitionModel::edge_time(std::size_t edge, std::size_t source_device, std::size_t target_device) const
        {
            if (device_pool_[source_device] == device_pool_[target_device])
                return 0.0;
            const std::optional<double> seconds =
                costs_->transfer_time(devices_[source_device], devices_[target_device], copy_paths_[source_device],
                                      static_cast<std::size_t>(problem_->edges[edge].bytes));
            return seconds.value_or(std::numeric_limits<double>::infinity());
        }

        double PartitionModel::step_time(const std::vector<std::size_t>& placement) const
        {
            std::vector<double> device_free(devices_.size(), 0.0);
            std::vector<double> finish(placement.size(), 0.0);
            double step = 0.0;
            for (std::size_t node = 0; node < placement.size(); ++node)
            {
                const std::size_t device = placement[node];
                double start = device_free[device];
                for (const std::size_t edge : edges_into_[node])
                {
                    const std::size_t source = problem_->edges[edge].from;
                    start = std::max(start, finish[source] + edge_time(edge, placement[source], device));
                }
                finish[node] = start + node_time(node, device);
                device_free[device] = finish[node];
                step = std::max(step, finish[node]);
            }
            return step;
        }

        std::vector<PoolShortfall> PartitionModel::pool_demand(const std::vector<std::size_t>& placement) const
        {
            PoolOccupancy occupancy = empty_occupancy();
            for (std::size_t node = 0; node < placement.size(); ++node)
                occupancy.add(node, device_pool_[placement[node]]);
            std::vector<PoolShortfall> demand;
            demand.reserve(pool_ids_.size());
            for (std::size_t pool = 0; pool < pool_ids_.size(); ++pool)
                demand.push_back(PoolShortfall{pool_ids_[pool], occupancy.used(pool), usable_[pool]});
            return demand;
        }

        bool PartitionModel::fits(const std::vector<std::size_t>& placement) const
        {
            for (const PoolShortfall& pool : pool_demand(placement))
            {
                if (pool.missing() != 0)
                    return false;
            }
            return true;
        }
    } // namespace Internal::Coherence
} // namespace SushiRuntime
