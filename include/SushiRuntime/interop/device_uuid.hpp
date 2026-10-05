/****************************************************************/
/* device_uuid.hpp                                              */
/* SushiRuntime - https://github.com/SushiSystems/SushiRuntime  */
/* Copyright (c) 2026 Sushi Systems                             */
/* Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.    */
/* Commercial use requires a licence from Sushi Systems.        */
/****************************************************************/

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
