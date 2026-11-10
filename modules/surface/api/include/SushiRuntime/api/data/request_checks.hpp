/**************************************************************************/
/* request_checks.hpp                                                    */
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
 * @file request_checks.hpp
 * @brief Declares the predicates a buffer or State request is checked with, without stopping.
 * @see modules/surface/api/include/SushiRuntime/api/data/README.md
 * @author Mustafa Garip
 */

#pragma once

#include <cstddef>
#include <limits>

namespace SushiRuntime
{
    /**
     * @brief Returns whether an alignment is 0, which asks for the device default, or a power of two.
     */
    [[nodiscard]] constexpr bool is_valid_alignment(std::size_t alignment) noexcept
    {
        return (alignment & (alignment - 1)) == 0;
    }

    /**
     * @brief Returns whether count elements of element_size bytes have a byte size std::size_t holds.
     * @pre element_size is above 0.
     */
    [[nodiscard]] constexpr bool fits_byte_count(std::size_t count, std::size_t element_size) noexcept
    {
        return count <= std::numeric_limits<std::size_t>::max() / element_size;
    }

    /**
     * @brief Returns whether a device index names one of the device_count devices a runtime drives.
     */
    [[nodiscard]] constexpr bool names_a_device(std::size_t device_index,
                                                std::size_t device_count) noexcept
    {
        return device_index < device_count;
    }
} // namespace SushiRuntime
