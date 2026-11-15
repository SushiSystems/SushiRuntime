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

#include <SushiRuntime/coherence/transfer_cost.hpp>
#include <SushiRuntime/core/result.hpp>
#include <SushiRuntime/device/device_queue_set.hpp>

namespace SushiRuntime
{
    namespace Internal::Execution
    {
        /**
         * @brief Returns the cost the runtime's planners price copies with.
         *
         * An empty path, or one with no file, gives uniform_transfer_cost() and logs it; a file is
         * read once and frozen into a CostModel.
         * @param devices Names the key of each device; must outlive the result.
         * @return The cost, or the reader's error for a file that cannot be read.
         */
        [[nodiscard]] Result<Coherence::TransferCost> load_transfer_cost(const std::string& path,
                                                                         const Device::DeviceQueueSet& devices);
    } // namespace Internal::Execution
} // namespace SushiRuntime
