/**************************************************************************/
/* runtime_transfer_cost.cpp                                              */
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
 * @file runtime_transfer_cost.cpp
 * @brief Implements load_transfer_cost.
 * @see modules/engine/execution/README.md
 * @author Mustafa Garip
 */

#include "runtime_transfer_cost.hpp"

#include <cstddef>
#include <filesystem>
#include <memory>
#include <optional>
#include <system_error>
#include <utility>

#include <SushiRuntime/coherence/cost_model.hpp>
#include <SushiRuntime/coherence/device_profile_file.hpp>
#include <SushiRuntime/coherence/profile_transfer_cost.hpp>
#include <SushiRuntime/execution/execution_log_category.hpp>
#include <SushiRuntime/log/logger.hpp>
#include <SushiRuntime/task/handle_location.hpp>

namespace SushiRuntime
{
    namespace Internal::Execution
    {
        Result<Coherence::TransferCost> load_transfer_cost(const std::string& path,
                                                           const Device::DeviceQueueSet& devices)
        {
            if (path.empty())
            {
                SR_LOG_INFO(K_EXECUTION_LOG_CATEGORY,
                            "No device profile file is configured; copies are priced by their bytes.");
                return Coherence::uniform_transfer_cost();
            }
            std::error_code failed;
            if (!std::filesystem::exists(path, failed))
            {
                SR_LOG_INFO(K_EXECUTION_LOG_CATEGORY,
                            "No device profile file at {}; copies are priced by their bytes.", path);
                return Coherence::uniform_transfer_cost();
            }

            Result<Coherence::DeviceProfileSet> profiles = Coherence::DeviceProfileFile::read(path);
            if (!profiles)
                return make_error(std::move(profiles.error()));
            SR_LOG_INFO(K_EXECUTION_LOG_CATEGORY, "Loaded the device profile file {}; copies are priced by it.",
                        path);

            auto model = std::make_shared<const Coherence::CostModel>(std::move(profiles.value()));
            const Device::DeviceQueueSet* const set = &devices;
            Coherence::SpaceDeviceResolver resolver = [set](MemorySpaceID space) -> std::optional<DeviceKey>
            {
                const std::optional<std::size_t> device = Graph::device_of_space(space);
                if (!device || *device >= set->devices().size())
                    return std::nullopt;
                return set->devices()[*device].key;
            };
            return Coherence::select_transfer_cost(std::move(model), std::move(resolver));
        }
    } // namespace Internal::Execution
} // namespace SushiRuntime
