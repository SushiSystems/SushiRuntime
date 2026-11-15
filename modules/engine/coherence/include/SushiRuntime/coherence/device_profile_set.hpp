/**************************************************************************/
/* device_profile_set.hpp                                               */
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
 * @file device_profile_set.hpp
 * @brief Declares DeviceProfileSet, the profiles of every measured device, ordered by DeviceKey.
 * @see modules/engine/coherence/README.md
 * @author Mustafa Garip
 */

#pragma once

#include <map>
#include <utility>

#include <SushiRuntime/coherence/device_profile.hpp>
#include <SushiRuntime/handle/device_key.hpp>

namespace SushiRuntime
{
    namespace Internal::Coherence
    {
        /**
         * @brief Holds one DeviceProfile per DeviceKey, in DeviceKey order.
         *
         * A device with no entry has no profile, and the partitioner gives it no node.
         */
        class DeviceProfileSet
        {
            public:
                /** @brief Returns whether key has a profile. */
                [[nodiscard]] bool has_profile(const DeviceKey& key) const
                {
                    return profiles_.find(key) != profiles_.end();
                }

                /** @brief Returns the profile of key, or nullptr when it has none. */
                [[nodiscard]] const DeviceProfile* find(const DeviceKey& key) const
                {
                    const auto found = profiles_.find(key);
                    return found == profiles_.end() ? nullptr : &found->second;
                }

                /**
                 * @brief Stores profile for key, replacing the one it had.
                 * @return Whether key had no profile before.
                 */
                bool set(const DeviceKey& key, DeviceProfile profile)
                {
                    return profiles_.insert_or_assign(key, std::move(profile)).second;
                }

                /** @brief Returns the profiles in DeviceKey order. */
                [[nodiscard]] const std::map<DeviceKey, DeviceProfile>& profiles() const noexcept
                {
                    return profiles_;
                }

                /** @brief Returns whether both sets hold equal profiles for equal keys. */
                [[nodiscard]] bool operator==(const DeviceProfileSet& other) const
                {
                    return profiles_ == other.profiles_;
                }

            private:
                /// @brief The profiles, ordered by key.
                std::map<DeviceKey, DeviceProfile> profiles_;
        };
    } // namespace Internal::Coherence
} // namespace SushiRuntime
