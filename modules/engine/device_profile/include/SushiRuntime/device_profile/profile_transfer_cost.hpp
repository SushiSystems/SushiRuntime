/**************************************************************************/
/* profile_transfer_cost.hpp                                            */
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
 * @file profile_transfer_cost.hpp
 * @brief Declares select_transfer_cost(), which backs a TransferCost with a CostModel when one is loaded.
 * @see modules/engine/device_profile/README.md
 * @author Mustafa Garip
 */

#pragma once

#include <functional>
#include <memory>
#include <optional>

#include <SushiRuntime/device_profile/cost_model.hpp>
#include <SushiRuntime/handle/device_key.hpp>
#include <SushiRuntime/handle/memory_space_id.hpp>
#include <SushiRuntime/transfer/transfer_cost.hpp>

namespace SushiRuntime
{
    namespace Internal::Coherence
    {
        /** @brief Returns the device a memory space belongs to, or nothing when the space has none. */
        using SpaceDeviceResolver = std::function<std::optional<DeviceKey>(MemorySpaceID)>;

        /**
         * @brief Returns the cost function a planner is given.
         *
         * Two devices: the path's seconds, else both host links. A device and the host or a file:
         * that device's host link. Unmeasured: infinity, logged once. No model: uniform.
         * @param model    The frozen model, or null when no profile file was loaded.
         * @param resolver Maps a memory space to its device; read only when model is not null.
         */
        [[nodiscard]] TransferCost select_transfer_cost(std::shared_ptr<const CostModel> model,
                                                        SpaceDeviceResolver resolver);
    } // namespace Internal::Coherence
} // namespace SushiRuntime
