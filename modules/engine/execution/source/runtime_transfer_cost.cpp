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
#include <vector>

#include <SushiRuntime/device_profile/cost_model.hpp>
#include <SushiRuntime/device_profile/device_profile_file.hpp>
#include <SushiRuntime/device_profile/profile_transfer_cost.hpp>
#include <SushiRuntime/execution/execution_log_category.hpp>
#include <SushiRuntime/log/logger.hpp>
#include <SushiRuntime/task/handle_location.hpp>

#include "runtime_partition_parts.hpp"
#include "runtime_profile_measurement.hpp"

namespace SushiRuntime
{
    namespace Internal::Execution
    {
        namespace
        {
            /** @brief Returns the profiles of the file at path, measuring the devices and writing it first when it is absent. */
            [[nodiscard]] Result<Coherence::DeviceProfileSet>
            profiles_at(const std::string& path, Device::DeviceQueueSet& devices,
                        const std::vector<const Memory::MemorySpace*>& spaces,
                        const std::vector<Coherence::TransferPath*>& paths)
            {
                std::error_code failed;
                if (std::filesystem::exists(path, failed))
                {
                    Result<Coherence::DeviceProfileSet> read = Coherence::DeviceProfileFile::read(path);
                    if (read)
                        SR_LOG_INFO(K_EXECUTION_LOG_CATEGORY,
                                    "Loaded the device profile file {}; copies are priced by it.", path);
                    return read;
                }

                SR_LOG_INFO(K_EXECUTION_LOG_CATEGORY,
                            "No device profile file at {}; measuring {} device(s) for it.", path, devices.count());
                Result<Coherence::DeviceProfileSet> measured = measure_runtime_profiles(devices, spaces, paths);
                if (!measured)
                    return measured;
                Result<bool> written = Coherence::DeviceProfileFile::write(path, measured.value());
                if (!written)
                    return make_error(std::move(written.error()));
                SR_LOG_INFO(K_EXECUTION_LOG_CATEGORY, "Wrote the device profile file {}; copies are priced by it.",
                            path);
                return measured;
            }
        } // namespace

        Result<RuntimeCost> load_transfer_cost(const std::string& path, Device::DeviceQueueSet& devices,
                                               const std::vector<const Memory::MemorySpace*>& spaces,
                                               const std::vector<Coherence::TransferPath*>& paths)
        {
            if (path.empty())
            {
                SR_LOG_INFO(K_EXECUTION_LOG_CATEGORY,
                            "No device profile file is configured; copies are priced by their bytes.");
                return RuntimeCost{Coherence::uniform_transfer_cost(), nullptr};
            }

            Result<Coherence::DeviceProfileSet> profiles = profiles_at(path, devices, spaces, paths);
            if (!profiles)
                return make_error(std::move(profiles.error()));

            auto model = std::make_shared<const Coherence::CostModel>(std::move(profiles.value()));
            return RuntimeCost{Coherence::select_transfer_cost(model, make_space_device_resolver(devices)), model};
        }
    } // namespace Internal::Execution
} // namespace SushiRuntime
