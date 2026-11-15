/**************************************************************************/
/* floor_partitioner.cpp                                                  */
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
 * @file floor_partitioner.cpp
 * @brief Implements FloorPartitioner.
 * @see modules/engine/coherence/README.md
 * @author Mustafa Garip
 */

#include <SushiRuntime/coherence/floor_partitioner.hpp>

#include <cstddef>
#include <numeric>
#include <optional>
#include <utility>
#include <vector>

#include <SushiRuntime/coherence/fill_plan.hpp>
#include <SushiRuntime/coherence/partition_model.hpp>
#include <SushiRuntime/coherence/planned_homes.hpp>
#include <SushiRuntime/core/assert.hpp>

namespace SushiRuntime
{
    namespace Internal::Coherence
    {
        FloorPartitioner::FloorPartitioner(std::shared_ptr<const CostModel> costs) : costs_(std::move(costs))
        {
            SR_VERIFY(costs_ != nullptr, "FloorPartitioner: pass a cost model, not null.");
        }

        Result<PartitionAssignment, PartitionRefusal> FloorPartitioner::partition(const PartitionProblem& problem) const
        {
            Result<PartitionModel, PartitionRefusal> built = PartitionModel::build(problem, *costs_);
            if (!built)
                return make_error(std::move(built.error()));
            const PartitionModel& model = built.value();
            const std::optional<FloorPlan> floor = floor_plan(model);
            if (!floor)
            {
                std::vector<std::size_t> all(model.device_count());
                std::iota(all.begin(), all.end(), std::size_t{0});
                return make_error(PartitionRefusal{
                    Error{ErrorCode::AllocationFailed, "the job fits in no fill plan of any device subset"},
                    model.pool_demand(fill_plan(model, all).placement)});
            }
            PartitionAssignment assignment;
            assignment.floor_taken = true;
            assignment.step_time = floor->step_time;
            assignment.homes = planned_homes(model, floor->placement);
            assignment.node_devices.reserve(floor->placement.size());
            for (const std::size_t device : floor->placement)
                assignment.node_devices.push_back(model.device_key(device));
            return assignment;
        }
    } // namespace Internal::Coherence
} // namespace SushiRuntime
