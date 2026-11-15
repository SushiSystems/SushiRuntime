/**************************************************************************/
/* rebalancing_parts.hpp                                                  */
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
 * @file rebalancing_parts.hpp
 * @brief Builds the partitioner factories and session parts the rebalancing tests run over.
 * @see tests/engine/coherence/README.md
 * @author Mustafa Garip
 */

#pragma once

#include <memory>
#include <optional>
#include <utility>

#include <SushiRuntime/coherence/floor_partitioner.hpp>
#include <SushiRuntime/coherence/list_scheduling_partitioner.hpp>
#include <SushiRuntime/coherence/partition_session.hpp>

#include "partition_problem_builder.hpp"

namespace SushiRuntime::Internal::Coherence
{
    /** @brief Returns the factories a runtime gives its session: the list scheduler and the floor. */
    [[nodiscard]] inline PartitionerFactories runtime_factories()
    {
        PartitionerFactories factories;
        factories.scheduler = [](std::shared_ptr<const CostModel> costs)
        { return std::unique_ptr<Partitioner>(std::make_unique<ListSchedulingPartitioner>(std::move(costs))); };
        factories.floor = [](std::shared_ptr<const CostModel> costs)
        { return std::unique_ptr<Partitioner>(std::make_unique<FloorPartitioner>(std::move(costs))); };
        return factories;
    }

    /** @brief Returns the parts of an active session over host_key(1) onwards, one round_profile per speed. */
    [[nodiscard]] inline PartitionSessionParts session_parts(const std::vector<double>& speeds)
    {
        PartitionProblem devices;
        add_devices(devices, speeds.size());
        PartitionSessionParts parts;
        parts.model = speed_model(speeds);
        parts.partitioners = runtime_factories();
        parts.devices = devices.devices;
        parts.copies = std::make_shared<TransferPathTally>();
        parts.device_of = [](MemorySpaceID space) -> std::optional<DeviceKey>
        {
            if (space == MemorySpaceID(2))
                return host_key(2);
            return std::nullopt;
        };
        return parts;
    }

    /** @brief Adds to session's ledger one step of seconds in which each device ran one node of independent_nodes() for its seconds. */
    inline void measure_step(PartitionSession& session, double seconds, const std::vector<std::pair<DeviceKey, double>>& devices)
    {
        NodeCost cost;
        cost.operations = 1000;
        std::vector<DispatchObservation> observations;
        for (const std::pair<DeviceKey, double>& device : devices)
            observations.push_back(DispatchObservation{device.first, cost, device.second});
        session.ledger().add_steps(1, seconds);
        session.ledger().add_dispatches(observations);
    }
} // namespace SushiRuntime::Internal::Coherence
