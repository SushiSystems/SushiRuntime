/**************************************************************************/
/* device_uuid.hpp                                                        */
/**************************************************************************/
/*                          This file is part of:                         */
/*                              SushiRuntime                              */
/*              https://github.com/SushiSystems/SushiRuntime              */
/*                        https://sushisystems.io                         */
/**************************************************************************/
/* Copyright (c) 2026-present Mustafa Garip & Sushi Systems               */
/*                                                                        */
/* Licensed under the Apache License, Version 2.0 (the "License");        */
/* you may not use this file except in compliance with the License.       */
/* You may obtain a copy of the License at                                */
/*                                                                        */
/*     http://www.apache.org/licenses/LICENSE-2.0                         */
/*                                                                        */
/* Unless required by applicable law or agreed to in writing, software    */
/* distributed under the License is distributed on an "AS IS" BASIS,      */
/* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or        */
/* implied. See the License for the specific language governing           */
/* permissions and limitations under the License.                         */
/**************************************************************************/

/**
 * @file device_uuid.hpp
 * @brief Identifies a physical device the same way across SYCL and Vulkan.
 * @author Sushi (mustafagarip@sushisystems.io)
 */

#pragma once

#include <array>
#include <cstdint>

namespace SushiRuntime
{
    namespace Interop
    {
        /**
         * @brief Holds the sixteen-byte UUID both SYCL and Vulkan expose for a device.
         */
        struct DeviceUuid
        {
            std::array<std::uint8_t, 16> bytes{}; /**< Holds the raw UUID bytes. */
        };

        /**
         * @brief Compares two device UUIDs for equality.
         */
        inline bool operator==(const DeviceUuid& left, const DeviceUuid& right) noexcept
        {
            return left.bytes == right.bytes;
        }

        /**
         * @brief Compares two device UUIDs for inequality.
         */
        inline bool operator!=(const DeviceUuid& left, const DeviceUuid& right) noexcept
        {
            return !(left == right);
        }

    } // namespace Interop
} // namespace SushiRuntime
