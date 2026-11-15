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
 * @see modules/engine/coherence/README.md
 * @author Mustafa Garip
 */

#pragma once

#include <vector>

#include <SushiRuntime/coherence/device_profile_set.hpp>
#include <SushiRuntime/coherence/dispatch_observation.hpp>

namespace SushiRuntime
{
    namespace Internal::Coherence
    {
        /**
         * @brief Returns profiles with each observed device and kind set to the throughput measured.
         *
         * A sample is the operations of a cost over its seconds less its bytes at the memory bandwidth,
         * so a folded profile prices the node at the measured time. An entry becomes the median of its
         * samples, the lower one of an even count; the generic kind sets base_throughput. An observation
         * of a device with no profile, or with no time left after its bytes, is dropped.
         */
        [[nodiscard]] DeviceProfileSet fold_observations(const DeviceProfileSet& profiles,
                                                         const std::vector<DispatchObservation>& observations);
    } // namespace Internal::Coherence
} // namespace SushiRuntime
