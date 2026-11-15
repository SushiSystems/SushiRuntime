/**************************************************************************/
/* buffer_region.hpp                                                      */
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
 * @file buffer_region.hpp
 * @brief Declares BufferRegion, a byte interval within one buffer named by its GlobalHandle.
 * @author Mustafa Garip
 * @see modules/foundation/region/README.md
 */

#pragma once

#include <cstddef>

#include <SushiRuntime/handle/global_handle.hpp>

namespace SushiRuntime
{
    /**
     * @brief Describes a half-open byte interval within one buffer named by its handle.
     * @see modules/foundation/region/README.md
     */
    struct BufferRegion
    {
        /** @brief Sentinel length meaning "to the end of the allocation". */
        static constexpr std::size_t K_WHOLE = ~std::size_t{0};

        GlobalHandle base;              /**< Handle of the owning buffer; the identity key. */
        std::size_t  offset = 0;        /**< Byte offset of the interval start. */
        std::size_t  length = K_WHOLE;  /**< Byte length, or K_WHOLE for the rest. */

        /** @brief Constructs an empty region that overlaps nothing. */
        constexpr BufferRegion() noexcept = default;

        /**
         * @brief Names the whole buffer of a handle.
         * @param handle Handle of the buffer; the null handle yields an empty region.
         */
        constexpr explicit BufferRegion(GlobalHandle handle) noexcept : base(handle) {}

        /**
         * @brief Constructs a sub-region of one buffer.
         * @param handle Handle of the owning buffer.
         * @param off    Byte offset of the interval start within the buffer.
         * @param len    Byte length of the interval; K_WHOLE means to the end.
         */
        constexpr BufferRegion(GlobalHandle handle, std::size_t off, std::size_t len) noexcept
            : base(handle), offset(off), length(len) {}

        /** @brief True if this region names a live handle rather than the null one. */
        constexpr bool valid() const noexcept { return !base.is_null(); }

        /**
         * @brief Reports whether two regions of the same allocation intersect.
         * @return True if both regions are valid and share a base and any byte.
         * @see modules/foundation/region/README.md
         */
        constexpr bool overlaps(const BufferRegion& o) const noexcept
        {
            if (base != o.base || !valid())
                return false;

            const bool a_whole = (length == K_WHOLE);
            const bool b_whole = (o.length == K_WHOLE);

            // [a0, a1) and [b0, b1) intersect iff a0 < b1 && b0 < a1; a K_WHOLE end is unbounded.
            if (a_whole && b_whole) return true;
            if (a_whole)            return offset < o.offset + o.length;
            if (b_whole)            return o.offset < offset + length;
            return offset < o.offset + o.length && o.offset < offset + length;
        }

        /**
         * @brief Reports whether this region fully covers another.
         * @return True if both regions are valid and every byte of r lies in this one.
         * @see modules/foundation/region/README.md
         */
        constexpr bool contains(const BufferRegion& r) const noexcept
        {
            if (base != r.base || !valid()) return false;
            if (offset > r.offset) return false;
            if (length == K_WHOLE)   return true;          // [offset, +inf)
            if (r.length == K_WHOLE) return false;         // finite cannot cover infinite
            return r.offset + r.length <= offset + length;
        }
    };
} // namespace SushiRuntime
