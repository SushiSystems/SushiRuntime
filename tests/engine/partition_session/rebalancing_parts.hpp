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
 * @brief Builds the partitioner factories, profile stores and sessions the rebalancing tests run over.
 * @see tests/engine/partition_session/README.md
 * @author Mustafa Garip
 */

#pragma once

#include <cstddef>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include <SushiRuntime/partition_session/journal_plan_source.hpp>
#include <SushiRuntime/partition_session/live_plan_source.hpp>
#include <SushiRuntime/partition_session/partition_session.hpp>
#include <SushiRuntime/partition_session/partition_sessions.hpp>
#include <SushiRuntime/partition_session/profile_store.hpp>
#include <SushiRuntime/partitioning/floor_partitioner.hpp>
#include <SushiRuntime/partitioning/list_scheduling_partitioner.hpp>

#include "support/partition_problem_builder.hpp"

namespace SushiRuntime::Internal::Coherence
{
    /** @brief Returns the factories a runtime gives its sessions: the list scheduler and the floor. */
    [[nodiscard]] inline PartitionerFactories runtime_factories()
    {
        PartitionerFactories factories;
        factories.scheduler = [](std::shared_ptr<const CostModel> costs)
        { return std::unique_ptr<Partitioner>(std::make_unique<ListSchedulingPartitioner>(std::move(costs))); };
        factories.floor = [](std::shared_ptr<const CostModel> costs)
        { return std::unique_ptr<Partitioner>(std::make_unique<FloorPartitioner>(std::move(costs))); };
        return factories;
    }

    /** @brief Returns a profile store over host_key(1) onwards, one round_profile per speed. */
    [[nodiscard]] inline std::shared_ptr<ProfileStore> speed_store(const std::vector<double>& speeds)
    {
        return std::make_shared<ProfileStore>(speed_model(speeds));
    }

    /** @brief Returns the parts of a session over devices devices, host_key(1) onwards; space 2 is host_key(2)'s. */
    [[nodiscard]] inline PartitionSessionParts session_parts(std::size_t devices)
    {
        PartitionProblem listed;
        add_devices(listed, devices);
        PartitionSessionParts parts;
        parts.devices = listed.devices;
        parts.copies = std::make_shared<TransferPathTally>();
        parts.device_of = [](MemorySpaceID space) -> std::optional<DeviceKey>
        {
            if (space == MemorySpaceID(2))
                return host_key(2);
            return std::nullopt;
        };
        return parts;
    }

    /** @brief Returns a session that plans live over store, one device per profile. */
    [[nodiscard]] inline PartitionSession live_session(std::shared_ptr<ProfileStore> store, std::size_t devices)
    {
        return PartitionSession(session_parts(devices),
                                std::make_unique<LivePlanSource>(std::move(store), runtime_factories(),
                                                                 K_DEFAULT_REBALANCE_MARGIN));
    }

    /** @brief Returns a session that plans live over its own store, one round_profile per speed. */
    [[nodiscard]] inline PartitionSession live_session(const std::vector<double>& speeds)
    {
        return live_session(speed_store(speeds), speeds.size());
    }

    /** @brief Returns a session over devices devices that answers from entries. */
    [[nodiscard]] inline PartitionSession replay_session(std::size_t devices, std::vector<PlacementJournalEntry> entries)
    {
        return PartitionSession(session_parts(devices),
                                std::make_unique<JournalPlanSource>(std::move(entries), runtime_factories()));
    }

    /** @brief Returns the parts of a runtime's registry over its own store, one round_profile per speed. */
    [[nodiscard]] inline PartitionSessionsParts registry_parts(const std::vector<double>& speeds)
    {
        PartitionSessionParts session = session_parts(speeds.size());
        PartitionSessionsParts parts;
        parts.profiles = speed_store(speeds);
        parts.partitioners = runtime_factories();
        parts.devices = session.devices;
        parts.copies = session.copies;
        parts.device_of = session.device_of;
        return parts;
    }

    /** @brief Returns the dispatches of one step in which each listed device ran one node of 1000 operations for its seconds. */
    [[nodiscard]] inline std::vector<DispatchObservation>
    step_dispatches(const std::vector<std::pair<DeviceKey, double>>& devices)
    {
        NodeCost cost;
        cost.operations = 1000;
        std::vector<DispatchObservation> observations;
        for (const std::pair<DeviceKey, double>& device : devices)
            observations.push_back(DispatchObservation{device.first, cost, device.second});
        return observations;
    }

    /** @brief Adds to session's ledger one step of seconds of the plan in force, with step_dispatches() of devices. */
    inline void measure_step(PartitionSession& session, double seconds, const std::vector<std::pair<DeviceKey, double>>& devices)
    {
        static_cast<void>(session.measure(session.problem_hash(), 1, seconds, step_dispatches(devices)));
    }
} // namespace SushiRuntime::Internal::Coherence
