/**************************************************************************/
/* device_key_assigner.hpp                                                */
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
 * @file device_key_assigner.hpp
 * @brief Declares DeviceKeyAssigner, which gives every discovered device its DeviceKey.
 * @author Mustafa Garip
 * @see modules/platform/topology/README.md
 */

#pragma once

#include <memory>
#include <vector>

#include <SushiRuntime/core/export.hpp>
#include <SushiRuntime/topology/device_key_source.hpp>

namespace SushiRuntime
{
    namespace Internal::Topology
    {
        /**
         * @brief Gives each device the key of the first source that names it, then sets ordinals.
         *
         * Devices that share a backend, an id kind and an id (all devices without an id on
         * one backend share one) take the ordinals 0, 1, 2 in the sorted order of their vendor,
         * name, memory size and compute units. The input position breaks a tie between devices
         * that are equal in all of these. No other part of the input order reaches a key.
         */
        class SUSHIRUNTIME_API DeviceKeyAssigner
        {
            public:
                /** @param sources Asked in order; the first to answer names the device. */
                explicit DeviceKeyAssigner(std::vector<std::unique_ptr<DeviceKeySource>> sources);

                DeviceKeyAssigner(DeviceKeyAssigner&&) noexcept = default;
                DeviceKeyAssigner& operator=(DeviceKeyAssigner&&) noexcept = default;
                DeviceKeyAssigner(const DeviceKeyAssigner&) = delete;
                DeviceKeyAssigner& operator=(const DeviceKeyAssigner&) = delete;

                /**
                 * @brief Builds the chain discovery uses: CUDA UUID, HIP UUID, Level Zero UUID,
                 *        PCI address, then the ordinal fallback.
                 */
                [[nodiscard]] static DeviceKeyAssigner standard();

                /**
                 * @brief Returns the key of every device, at the position of the device.
                 * @param identities The devices to name.
                 */
                [[nodiscard]] std::vector<DeviceKey>
                assign(const std::vector<DeviceIdentity>& identities) const;

            private:
                /// @brief The sources, in the order they are asked.
                std::vector<std::unique_ptr<DeviceKeySource>> sources_;
        };
    } // namespace Internal::Topology
} // namespace SushiRuntime
