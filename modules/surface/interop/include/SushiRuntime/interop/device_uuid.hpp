/**************************************************************************/
/* device_uuid.hpp                                                        */
/**************************************************************************/
/*                          This file is part of:                         */
/*                              SushiRuntime                              */
/*              https://github.com/SushiSystems/SushiRuntime              */
/*                         https://sushisystems.io                        */
/**************************************************************************/
/* Copyright (c) 2026-present Mustafa Garip & Sushi Systems               */
/*                                                                        */
/* Licensed under the PolyForm Noncommercial License 1.0.0 (the           */
/* "License"); you may not use this file except in compliance with the    */
/* License. You may obtain a copy of the License at                       */
/*                                                                        */
/*     https://polyformproject.org/licenses/noncommercial/1.0.0           */
/*                                                                        */
/* Noncommercial use is free. Commercial use requires a separate licence  */
/* from Sushi Systems; see COMMERCIAL.md. The software is provided        */
/* "as is", without warranty of any kind.                                 */
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
        struct DeviceUUID
        {
            std::array<std::uint8_t, 16> bytes{}; /**< Holds the raw UUID bytes. */
        };

        /**
         * @brief Compares two device UUIDs for equality.
         */
        inline bool operator==(const DeviceUUID& left, const DeviceUUID& right) noexcept
        {
            return left.bytes == right.bytes;
        }

        /**
         * @brief Compares two device UUIDs for inequality.
         */
        inline bool operator!=(const DeviceUUID& left, const DeviceUUID& right) noexcept
        {
            return !(left == right);
        }

    } // namespace Interop
} // namespace SushiRuntime
