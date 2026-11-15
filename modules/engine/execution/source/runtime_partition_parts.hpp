/**************************************************************************/
/* runtime_partition_parts.hpp                                            */
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
 * @file runtime_partition_parts.hpp
 * @brief Declares the builders of a runtime's partition session parts and space resolver.
 * @see modules/engine/execution/README.md
 * @author Mustafa Garip
 */

#pragma once

#include <memory>
#include <vector>

#include <SushiRuntime/coherence/cost_model.hpp>
#include <SushiRuntime/coherence/partition_session.hpp>
#include <SushiRuntime/coherence/profile_transfer_cost.hpp>
#include <SushiRuntime/coherence/transfer_path.hpp>
#include <SushiRuntime/coherence/transfer_path_tally.hpp>
#include <SushiRuntime/device/device_queue_set.hpp>
#include <SushiRuntime/memory/memory_space.hpp>

namespace SushiRuntime
{
    namespace Internal::Execution
    {
        /**
         * @brief Returns the resolver that names the DeviceKey of a device's memory space; the host
         *        space and a file space have none. devices must outlive it.
         */
        [[nodiscard]] Coherence::SpaceDeviceResolver make_space_device_resolver(const Device::DeviceQueueSet& devices);

        /**
         * @brief Returns the parts of the runtime's partition session.
         *
         * A CPU or integrated device draws from pool 0, device i otherwise from pool i + 1; its copy
         * path is the first path to another device. A null model gives no partitioner.
         * @param spaces The runtime's spaces, indexed by space id.
         * @see modules/engine/execution/README.md
         */
        [[nodiscard]] Coherence::PartitionSessionParts
        make_partition_parts(const Device::DeviceQueueSet& devices, const std::vector<const Memory::MemorySpace*>& spaces,
                             const std::vector<Coherence::TransferPath*>& paths,
                             std::shared_ptr<const Coherence::CostModel> model,
                             std::shared_ptr<Coherence::TransferPathTally> copies);
    } // namespace Internal::Execution
} // namespace SushiRuntime
