/**************************************************************************/
/* ordinal_device_key_source.hpp                                          */
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
 * @file ordinal_device_key_source.hpp
 * @brief Declares OrdinalDeviceKeySource, the key of a device that has no id.
 * @author Mustafa Garip
 * @see modules/platform/topology/README.md
 */

#pragma once

#include <optional>

#include <SushiRuntime/core/export.hpp>
#include <SushiRuntime/topology/device_key_source.hpp>

namespace SushiRuntime
{
    namespace Internal::Topology
    {
        /**
         * @brief Names a device that no other source can name, by its backend alone.
         *
         * It always answers, so it goes last in a chain. The key it returns holds no id; the
         * ordinal that tells such devices apart is set by DeviceKeyAssigner, from the sorted
         * order of their backend, vendor and name.
         */
        class SUSHIRUNTIME_API OrdinalDeviceKeySource final : public DeviceKeySource
        {
            public:
                [[nodiscard]] std::optional<DeviceKey>
                key_of(const DeviceIdentity& identity) const override;
        };
    } // namespace Internal::Topology
} // namespace SushiRuntime
