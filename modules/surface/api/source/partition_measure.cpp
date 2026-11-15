/**************************************************************************/
/* partition_measure.cpp                                                  */
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
 * @file partition_measure.cpp
 * @brief Implements measure_partitioned_run() and fold_partition_ledger().
 * @see modules/engine/coherence/README.md
 * @author Mustafa Garip
 */

#include "partition_measure.hpp"

#include <filesystem>
#include <memory>
#include <string>

#include <SushiRuntime/api/api_log_category.hpp>
#include <SushiRuntime/coherence/dispatch_observation.hpp>
#include <SushiRuntime/coherence/partition_session.hpp>
#include <SushiRuntime/core/error.hpp>
#include <SushiRuntime/log/logger.hpp>
#include <SushiRuntime/report/node_table.hpp>

namespace SushiRuntime
{
    namespace Detail
    {
        void measure_partitioned_run(Internal::Execution::RuntimeContext& context,
                                     const Internal::Graph::CompiledGraph& plan, std::size_t steps, double seconds)
        {
            Internal::Coherence::PartitionSession& session = context.partitioning();
            if (!session.has_plan())
                return;
            session.ledger().add_steps(steps, seconds);
            const std::shared_ptr<const NodeTable>& table =
                plan.observation.table ? plan.observation.table : plan.node_table;
            if (!plan.profiling || table == nullptr)
                return;
            Internal::Coherence::DeviceKeyTable devices;
            for (const Internal::Coherence::PartitionDevice& device : session.devices())
                devices.by_index.push_back(device.key);
            session.ledger().add_dispatches(
                Internal::Coherence::observe_dispatches(plan.observation.events, *table, devices));
        }

        void fold_partition_ledger(Internal::Execution::RuntimeContext& context)
        {
            const std::string& path = context.settings().device_profile_file;
            if (path.empty() || !context.partitioning().has_plan())
                return;
            const Result<bool> folded = context.partitioning().ledger().fold_into(std::filesystem::path(path));
            if (!folded)
                SR_LOG_WARN(K_API_LOG_CATEGORY,
                            "The measured device times were not folded into the profile file {}: {}; the next run "
                            "reads the file as it was.",
                            path, folded.error().message);
        }
    } // namespace Detail
} // namespace SushiRuntime
