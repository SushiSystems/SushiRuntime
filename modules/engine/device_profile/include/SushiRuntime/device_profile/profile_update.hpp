/**************************************************************************/
/* profile_update.hpp                                                     */
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
 * @file profile_update.hpp
 * @brief Declares update_profile_file(), which writes what a run measured into the profile file.
 * @see modules/engine/device_profile/README.md
 * @author Mustafa Garip
 */

#pragma once

#include <filesystem>
#include <vector>

#include <SushiRuntime/core/result.hpp>
#include <SushiRuntime/device_profile/dispatch_observation.hpp>

namespace SushiRuntime
{
    namespace Internal::Coherence
    {
        /**
         * @brief Folds observations into the profile file at path and writes the file back.
         *
         * Entries the run did not measure are written as they were, through a temporary file, so a
         * failed write leaves the old file.
         * @return The error of the read or of the write; a missing file is an error.
         */
        [[nodiscard]] Result<bool> update_profile_file(const std::filesystem::path& path,
                                                       const std::vector<DispatchObservation>& observations);
    } // namespace Internal::Coherence
} // namespace SushiRuntime
