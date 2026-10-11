/**************************************************************************/
/* resource_region.hpp                                                    */
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
 * @file resource_region.hpp
 * @brief Declares ResourceRegion, a byte interval within one tracked allocation.
 * @author Mustafa Garip
 * @see modules/platform/memory/README.md
 */

#pragma once

#include <cstddef>

namespace SushiRuntime
{
    namespace Memory
    {
        /**
         * @brief Identity of the allocation a resource region belongs to.
         *
         * A raw base pointer.
         */
        using ResourceID = void*;

        /**
         * @brief Describes a half-open byte interval within one tracked allocation.
         * @see modules/platform/memory/README.md
         */
        struct ResourceRegion
        {
            /** @brief Sentinel length meaning "to the end of the allocation". */
            static constexpr std::size_t WHOLE = ~std::size_t{0};

            ResourceID  base   = nullptr; /**< Owning allocation; the identity key. */
            std::size_t offset = 0;       /**< Byte offset of the interval start. */
            std::size_t length = WHOLE;   /**< Byte length, or WHOLE for the rest. */

            /** @brief Constructs an empty region that overlaps nothing. */
            constexpr ResourceRegion() noexcept = default;

            /**
             * @brief Wraps a base pointer as the whole-buffer region.
             *
             * Implicit: a base pointer converts to the whole-buffer region.
             * @param p Base pointer of the allocation; null yields an empty region.
             */
            constexpr ResourceRegion(ResourceID p) noexcept : base(p) {}

            /**
             * @brief Constructs a sub-region of one allocation.
             * @param b   Base pointer of the owning allocation.
             * @param off Byte offset of the interval start within @p b.
             * @param len Byte length of the interval; WHOLE means to the end.
             */
            constexpr ResourceRegion(ResourceID b, std::size_t off, std::size_t len) noexcept
                : base(b), offset(off), length(len) {}

            /** @brief True if this region keys on a real allocation. */
            constexpr bool valid() const noexcept { return base != nullptr; }

            /**
             * @brief Reports whether two regions of the same allocation intersect.
             * @return True if both regions are valid and share a base and any byte.
             * @see modules/platform/memory/README.md
             */
            constexpr bool overlaps(const ResourceRegion& o) const noexcept
            {
                if (base != o.base || !valid())
                    return false;

                const bool a_whole = (length == WHOLE);
                const bool b_whole = (o.length == WHOLE);

                // [a0, a1) and [b0, b1) intersect iff a0 < b1 && b0 < a1; a WHOLE end is unbounded.
                if (a_whole && b_whole) return true;
                if (a_whole)            return offset < o.offset + o.length;
                if (b_whole)            return o.offset < offset + length;
                return offset < o.offset + o.length && o.offset < offset + length;
            }

            /**
             * @brief Reports whether this region fully covers another.
             * @return True if both regions are valid and every byte of r lies in this one.
             * @see modules/platform/memory/README.md
             */
            constexpr bool contains(const ResourceRegion& r) const noexcept
            {
                if (base != r.base || !valid()) return false;
                if (offset > r.offset) return false;
                if (length == WHOLE)   return true;          // [offset, +inf)
                if (r.length == WHOLE) return false;         // finite cannot cover infinite
                return r.offset + r.length <= offset + length;
            }
        };

    } // namespace Memory
} // namespace SushiRuntime
