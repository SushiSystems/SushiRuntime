/**************************************************************************/
/* physical_card_selector.hpp                                             */
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
 * @file physical_card_selector.hpp
 * @brief Declares select_physical_cards, which keeps one device per physical card.
 * @author Mustafa Garip
 * @see modules/platform/topology/README.md
 */

#pragma once

#include <cstddef>
#include <vector>

#include <SushiRuntime/core/export.hpp>
#include <SushiRuntime/topology/device_key_source.hpp>

namespace SushiRuntime
{
    namespace Internal::Topology
    {
        /**
         * @brief Returns the positions of the devices to keep, one for each physical card.
         *
         * Devices that share a UUID or a PCI address are one card. The native backend wins
         * over OpenCL, then the lowest backend value, name and position.
         * @param identities The devices, in any order.
         * @return Positions into @p identities, in ascending order.
         */
        [[nodiscard]] SUSHIRUNTIME_API std::vector<std::size_t>
        select_physical_cards(const std::vector<DeviceIdentity>& identities);
    } // namespace Internal::Topology
} // namespace SushiRuntime
