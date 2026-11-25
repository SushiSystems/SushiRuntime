/**************************************************************************/
/* runtime_coherence_parts.cpp                                            */
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
 * @file runtime_coherence_parts.cpp
 * @brief Implements RuntimeCoherenceParts and make_coherence_parts().
 * @see modules/engine/execution/README.md
 * @author Mustafa Garip
 */

#include "runtime_coherence_parts.hpp"

#include <utility>

#include <SushiRuntime/memory/system_memory_probe.hpp>

#include "runtime_partition_parts.hpp"

namespace SushiRuntime
{
    namespace Internal::Execution
    {
        RuntimeCoherenceParts::RuntimeCoherenceParts(Device::DeviceQueueSet& devices,
                                                     Memory::DeviceAccessGate* gate)
            : spaces(build_memory_spaces(devices, Memory::SystemMemoryProbe{})),
              paths(build_transfer_paths(devices, &transfer_observer, gate)),
              transfer_cost(Coherence::uniform_transfer_cost()),
              path_tally(std::make_shared<Coherence::TransferPathTally>())
        {

        }

        void RuntimeCoherenceParts::start_partitioning(const Device::DeviceQueueSet& devices,
                                                       std::shared_ptr<const Coherence::CostModel> model,
                                                       const std::filesystem::path& journal_base)
        {
            transfer_observer.install(path_tally);
            Coherence::PartitionSessionsParts parts =
                make_partition_parts(devices, spaces.views, paths.views, std::move(model), path_tally);
            if (!journal_base.empty())
                parts.journal_base = journal_base;
            partitioning = std::make_unique<Coherence::PartitionSessions>(std::move(parts));
        }

        std::unique_ptr<RuntimeCoherenceParts> make_coherence_parts(Device::DeviceQueueSet& devices,
                                                                    Memory::DeviceAccessGate* gate)
        {
            return std::make_unique<RuntimeCoherenceParts>(devices, gate);
        }
    } // namespace Internal::Execution
} // namespace SushiRuntime
