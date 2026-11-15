/**************************************************************************/
/* profile_update.cpp                                                     */
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
 * @file profile_update.cpp
 * @brief Implements update_profile_file().
 * @see modules/engine/device_profile/README.md
 * @author Mustafa Garip
 */

#include <SushiRuntime/device_profile/profile_update.hpp>

#include <utility>

#include <SushiRuntime/device_profile/device_profile_file.hpp>
#include <SushiRuntime/device_profile/profile_fold.hpp>

namespace SushiRuntime
{
    namespace Internal::Coherence
    {
        Result<bool> update_profile_file(const std::filesystem::path& path,
                                         const std::vector<DispatchObservation>& observations)
        {
            Result<DeviceProfileSet> current = DeviceProfileFile::read(path);
            if (!current)
                return make_error(std::move(current.error()));
            return DeviceProfileFile::write(path, fold_observations(current.value(), observations));
        }
    } // namespace Internal::Coherence
} // namespace SushiRuntime
