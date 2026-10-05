/****************************************************************/
/* resource_region.hpp                                          */
/* SushiRuntime - https://github.com/SushiSystems/SushiRuntime  */
/* Copyright (c) 2026 Sushi Systems                             */
/* Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.    */
/* Commercial use requires a licence from Sushi Systems.        */
/****************************************************************/

#pragma once

#include <cstddef>

namespace SushiRuntime
{
    namespace Core
    {
        /**
         * @brief Identity of the allocation a resource region belongs to.
         *
         * A raw base pointer today; a type alias so a future integer global id
         * (the distributed layer's buffer id) is a one-line change rather than a
         * churn across every dependency-tracking call site.
         */
        using ResourceId = void*;

        /**
         * @brief A half-open byte interval within one tracked allocation.
         *
         * The dependency tracker keys ordering on regions, not bare pointers, so it
         * can tell apart two sub-ranges of the same allocation (an array's two
         * halves, a grid's interior and its halo). Two accesses are ordered iff
         * their regions overlap; disjoint sub-ranges of the same buffer stay
         * parallel, and distinct allocations never serialize.
         *
         * A bare pointer converts to the whole-buffer region implicitly, so every
         * caller that still speaks base pointers keeps the previous behaviour: a
         * whole region overlaps any region of the same base, degenerating the
         * overlap test to base-pointer equality.
         */
        struct ResourceRegion
        {
            /** @brief Sentinel length meaning "to the end of the allocation". */
            static constexpr std::size_t WHOLE = ~std::size_t{0};

            ResourceId  base   = nullptr; /**< Owning allocation; the identity key. */
            std::size_t offset = 0;       /**< Byte offset of the interval start. */
            std::size_t length = WHOLE;   /**< Byte length, or WHOLE for the rest. */

            /** @brief Constructs an empty region that overlaps nothing. */
            constexpr ResourceRegion() noexcept = default;

            /**
             * @brief Wraps a base pointer as the whole-buffer region.
             *
             * Non-explicit on purpose: it is the adaptor that lets pointer-keyed
             * call sites feed the region-based tracker unchanged.
             * @param p Base pointer of the allocation; null yields an empty region.
             */
            constexpr ResourceRegion(ResourceId p) noexcept : base(p) {}

            /**
             * @brief Constructs a sub-region of one allocation.
             * @param b   Base pointer of the owning allocation.
             * @param off Byte offset of the interval start within @p b.
             * @param len Byte length of the interval; WHOLE means to the end.
             */
            constexpr ResourceRegion(ResourceId b, std::size_t off, std::size_t len) noexcept
                : base(b), offset(off), length(len) {}

            /** @brief True if this region keys on a real allocation. */
            constexpr bool valid() const noexcept { return base != nullptr; }

            /**
             * @brief Reports whether two regions of the same allocation intersect.
             *
             * The overlap test the tracker uses to decide ordering: same base and
             * intersecting half-open intervals. WHOLE is treated as an unbounded
             * upper end, so the comparison never overflows on the sentinel.
             *
             * @param o The other region to test against.
             * @return True if the regions share a base and any byte, false otherwise.
             */
            constexpr bool overlaps(const ResourceRegion& o) const noexcept
            {
                if (base != o.base)
                    return false;

                const bool a_whole = (length == WHOLE);
                const bool b_whole = (o.length == WHOLE);

                // [a0, a1) and [b0, b1) intersect iff a0 < b1 && b0 < a1. A WHOLE
                // upper end is +inf, which drops the half of the test it dominates.
                if (a_whole && b_whole) return true;
                if (a_whole)            return offset < o.offset + o.length;
                if (b_whole)            return o.offset < offset + length;
                return offset < o.offset + o.length && o.offset < offset + length;
            }

            /**
             * @brief Reports whether this region fully covers another.
             *
             * Lets the tracker stop walking history early: once a write that covers
             * the accessed region is found, every earlier access to that range is
             * already ordered behind it transitively.
             *
             * @param r The region to test for containment.
             * @return True if @p r lies entirely within this region.
             */
            constexpr bool contains(const ResourceRegion& r) const noexcept
            {
                if (base != r.base)   return false;
                if (offset > r.offset) return false;
                if (length == WHOLE)   return true;          // [offset, +inf)
                if (r.length == WHOLE) return false;         // finite cannot cover infinite
                return r.offset + r.length <= offset + length;
            }
        };

    } // namespace Core
} // namespace SushiRuntime
