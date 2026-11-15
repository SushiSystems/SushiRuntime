/**************************************************************************/
/* profile_fold.hpp                                                       */
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
 * @file profile_fold.hpp
 * @brief Declares fold_observations(), which folds measured node throughput into device profiles.
 * @see modules/engine/device_profile/README.md
 * @author Mustafa Garip
 */

#pragma once

#include <cstddef>
#include <vector>

#include <SushiRuntime/device_profile/device_profile_set.hpp>
#include <SushiRuntime/device_profile/dispatch_observation.hpp>

namespace SushiRuntime
{
    namespace Internal::Coherence
    {
        /** @brief Samples a device and kind needs in one fold before its entry moves. */
        inline constexpr std::size_t K_MINIMUM_FOLD_SAMPLES = 8;

        /** @brief Share of the way an entry moves from its value toward the new median, in (0, 1]. */
        inline constexpr double K_FOLD_SMOOTHING_WEIGHT = 0.25;

        /**
         * @brief Returns profiles with each observed device and kind moved toward the throughput measured.
         *
         * A sample is a cost's operations over its seconds less its bytes at the memory bandwidth. A device
         * and kind with fewer than K_MINIMUM_FOLD_SAMPLES samples keeps its entry; otherwise the entry moves
         * K_FOLD_SMOOTHING_WEIGHT of the way to the lower median, or takes it when it holds no positive value.
         * The generic kind moves base_throughput. An observation with no profile or no time left is dropped.
         */
        [[nodiscard]] DeviceProfileSet fold_observations(const DeviceProfileSet& profiles,
                                                         const std::vector<DispatchObservation>& observations);
    } // namespace Internal::Coherence
} // namespace SushiRuntime
