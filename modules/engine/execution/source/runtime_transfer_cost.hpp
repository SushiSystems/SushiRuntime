/**************************************************************************/
/* runtime_transfer_cost.hpp                                              */
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
 * @file runtime_transfer_cost.hpp
 * @brief Declares load_transfer_cost, which reads the device profile file once and prices the runtime's copies by it.
 * @see modules/engine/execution/README.md
 * @author Mustafa Garip
 */

#pragma once

#include <string>
#include <vector>

#include <SushiRuntime/coherence/transfer_cost.hpp>
#include <SushiRuntime/coherence/transfer_path.hpp>
#include <SushiRuntime/memory/memory_space.hpp>
#include <SushiRuntime/core/result.hpp>
#include <SushiRuntime/device/device_queue_set.hpp>

namespace SushiRuntime
{
    namespace Internal::Execution
    {
        /**
         * @brief Returns the cost the runtime's planners price copies with.
         *
         * An empty path gives uniform_transfer_cost(). A path with no file is a first start: the
         * devices, spaces and paths are measured and the file is written atomically. A file there
         * is read once. devices must outlive the result.
         * @return The cost, or the error of the reader, the measurement or the writer.
         */
        [[nodiscard]] Result<Coherence::TransferCost>
        load_transfer_cost(const std::string& path, Device::DeviceQueueSet& devices,
                           const std::vector<const Memory::MemorySpace*>& spaces,
                           const std::vector<Coherence::TransferPath*>& paths);
    } // namespace Internal::Execution
} // namespace SushiRuntime
