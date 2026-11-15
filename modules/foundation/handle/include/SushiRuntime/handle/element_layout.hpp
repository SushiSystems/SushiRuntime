/**************************************************************************/
/* element_layout.hpp                                                     */
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
 * @file element_layout.hpp
 * @brief Declares ElementLayout, the size and alignment of one element of a buffer.
 * @see modules/foundation/handle/README.md
 * @author Mustafa Garip
 */

#pragma once

#include <cstddef>

namespace SushiRuntime
{
    /** @brief Holds the size and alignment of one element of a buffer. */
    struct ElementLayout
    {
        std::size_t size = 1;      /**< Bytes per element. */
        std::size_t alignment = 1; /**< Alignment of an element in bytes. */

        /** @brief Returns whether the buffer holds raw bytes, one byte per element. */
        [[nodiscard]] constexpr bool is_bytes() const noexcept
        {
            return size == 1 && alignment == 1;
        }

        /** @brief Returns whether both layouts have the same size and alignment. */
        [[nodiscard]] friend constexpr bool operator==(const ElementLayout& left,
                                                       const ElementLayout& right) noexcept
        {
            return left.size == right.size && left.alignment == right.alignment;
        }

        /** @brief Returns whether the layouts differ in size or alignment. */
        [[nodiscard]] friend constexpr bool operator!=(const ElementLayout& left,
                                                       const ElementLayout& right) noexcept
        {
            return !(left == right);
        }
    };
} // namespace SushiRuntime
