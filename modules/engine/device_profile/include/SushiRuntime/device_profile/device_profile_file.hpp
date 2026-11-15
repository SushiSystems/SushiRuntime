/**************************************************************************/
/* device_profile_file.hpp                                              */
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
 * @file device_profile_file.hpp
 * @brief Declares DeviceProfileFile, the reader and writer of the device profile file.
 * @see modules/engine/device_profile/README.md
 * @author Mustafa Garip
 */

#pragma once

#include <filesystem>
#include <string>
#include <string_view>

#include <SushiRuntime/core/result.hpp>
#include <SushiRuntime/device_profile/device_profile_set.hpp>

namespace SushiRuntime
{
    namespace Internal::Coherence
    {
        /**
         * @brief Owns the text format of the device profile file.
         *
         * The format is line based and versioned; modules/engine/coherence/README.md holds its schema.
         * The writer emits devices in DeviceKey order, then kinds by value and paths by name, so one
         * set always gives the same bytes. Doubles are written with 17 significant digits and read
         * back equal.
         */
        class DeviceProfileFile
        {
            public:
                /** @brief Holds the version the writer writes and the reader reads without upgrading. */
                static constexpr int K_CURRENT_VERSION = 2;

                /** @brief Holds the oldest version the reader reads and upgrades. */
                static constexpr int K_OLDEST_VERSION = 1;

                /**
                 * @brief Returns the text of profiles in the current version.
                 * @return InvalidArgument when a path name is empty or holds white space.
                 */
                [[nodiscard]] static Result<std::string> serialise(const DeviceProfileSet& profiles);

                /**
                 * @brief Returns the profiles that text holds.
                 *
                 * A version 1 file is upgraded in memory: it has no host links, so each device's
                 * links stay unmeasured.
                 * @return InvalidArgument for a malformed line, with its number; Unsupported for a
                 *         version this reader does not know, naming both versions.
                 */
                [[nodiscard]] static Result<DeviceProfileSet> parse(std::string_view text);

                /** @brief Reads and parses the file at path; InvalidArgument when it cannot be opened. */
                [[nodiscard]] static Result<DeviceProfileSet> read(const std::filesystem::path& path);

                /** @brief Writes profiles to path through a temporary file, so a failed write leaves the old file. */
                [[nodiscard]] static Result<bool> write(const std::filesystem::path& path,
                                                        const DeviceProfileSet& profiles);
        };
    } // namespace Internal::Coherence
} // namespace SushiRuntime
