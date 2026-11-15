/**************************************************************************/
/* partition_report_builder.cpp                                           */
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
 * @file partition_report_builder.cpp
 * @brief Implements make_partition_report().
 * @see modules/engine/partitioning/README.md
 * @author Mustafa Garip
 */

#include <SushiRuntime/partitioning/partition_report_builder.hpp>

#include <algorithm>
#include <cstddef>
#include <map>
#include <optional>
#include <utility>

#include <SushiRuntime/partitioning/fill_plan.hpp>
#include <SushiRuntime/partitioning/partition_model.hpp>

namespace SushiRuntime
{
    namespace Internal::Coherence
    {
        namespace
        {
            using Rows = std::map<DeviceKey, PartitionDeviceRow>;

            /** @brief Returns one zeroed row per device of problem, the first row of a repeated key winning. */
            [[nodiscard]] Rows empty_rows(const PartitionProblem& problem)
            {
                Rows rows;
                for (const PartitionDevice& device : problem.devices)
                {
                    PartitionDeviceRow row;
                    row.device = device.key;
                    row.pool = device.pool;
                    rows.emplace(device.key, row);
                }
                return rows;
            }

            /** @brief Adds the nodes of assignment, and their modelled times, to the rows of their devices. */
            void add_nodes(Rows& rows, const PartitionProblem& problem, const CostModel& model,
                           const PartitionAssignment& assignment)
            {
                const std::size_t count = std::min(problem.nodes.size(), assignment.node_devices.size());
                for (std::size_t node = 0; node < count; ++node)
                {
                    const auto row = rows.find(assignment.node_devices[node]);
                    if (row == rows.end())
                        continue;
                    ++row->second.nodes;
                    const std::optional<double> seconds = model.node_time(row->first, problem.nodes[node].cost);
                    if (seconds.has_value())
                        row->second.modelled_seconds += *seconds;
                }
            }

            /** @brief Adds the bytes of each buffer to the row of its home. */
            void add_buffers(Rows& rows, const PartitionProblem& problem, const PartitionAssignment& assignment)
            {
                for (const PartitionBuffer& buffer : problem.buffers)
                {
                    const std::optional<DeviceKey> home =
                        buffer.device.has_value() ? buffer.device : assignment.home_of(buffer.handle);
                    if (!home.has_value())
                        continue;
                    const auto row = rows.find(*home);
                    if (row != rows.end())
                        row->second.bytes += buffer.bytes;
                }
            }

            /** @brief Gives each row the bytes its pool lacked. */
            void add_shortfalls(Rows& rows, const PartitionRefusal& refusal)
            {
                for (auto& entry : rows)
                {
                    for (const PoolShortfall& shortfall : refusal.pools)
                    {
                        if (shortfall.pool == entry.second.pool)
                            entry.second.shortfall_bytes = shortfall.missing();
                    }
                }
            }
            /** @brief Gives each row the device time and the copies measured gives its device. */
            void add_measurement(Rows& rows, const PartitionMeasurement& measured)
            {
                for (auto& entry : rows)
                {
                    const auto seconds = measured.device_seconds.find(entry.first);
                    if (seconds != measured.device_seconds.end())
                        entry.second.measured_seconds = seconds->second;
                    const auto copies = measured.copies.find(entry.first);
                    if (copies != measured.copies.end())
                        entry.second.copies = copies->second;
                }
            }

            /** @brief Writes the floor's modelled step and devices into report; nothing when no floor fits. */
            void add_floor(PartitionReport& report, const PartitionProblem& problem, const CostModel& model)
            {
                const Result<PartitionModel, PartitionRefusal> built = PartitionModel::build(problem, model);
                if (!built)
                    return;
                const std::optional<FloorPlan> floor = floor_plan(built.value());
                if (!floor)
                    return;
                report.floor_step_seconds = floor->step_time;
                for (const std::size_t device : floor->devices)
                    report.floor_devices.push_back(built.value().device_key(device));
            }
        } // namespace

        PartitionReport make_partition_report(const PartitionProblem& problem, const CostModel& model,
                                              const Result<PartitionAssignment, PartitionRefusal>& answer,
                                              const PartitionMeasurement& measured)
        {
            Rows rows = empty_rows(problem);
            PartitionReport report;
            if (answer.has_value())
            {
                const PartitionAssignment& assignment = answer.value();
                report.step_seconds = assignment.step_time;
                report.measured_step_seconds = measured.step_seconds;
                report.floor_taken = assignment.floor_taken;
                add_nodes(rows, problem, model, assignment);
                add_buffers(rows, problem, assignment);
                add_measurement(rows, measured);
                add_floor(report, problem, model);
            }
            else
            {
                report.refused = true;
                report.refusal = answer.error().error.message;
                add_shortfalls(rows, answer.error());
            }
            report.rows.reserve(rows.size());
            for (auto& entry : rows)
                report.rows.push_back(std::move(entry.second));
            return report;
        }
    } // namespace Internal::Coherence
} // namespace SushiRuntime
