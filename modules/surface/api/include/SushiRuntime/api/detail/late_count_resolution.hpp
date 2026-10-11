/**************************************************************************/
/* late_count_resolution.hpp                                              */
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
 * @file late_count_resolution.hpp
 * @brief Declares the helpers that register a LateCount's reads and read its live value.
 * @author Mustafa Garip
 * @see modules/surface/api/include/SushiRuntime/api/detail/README.md
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include <SushiRuntime/api/detail/device_fault.hpp>
#include <SushiRuntime/api/vocabulary/late_count.hpp>
#include <SushiRuntime/core/error.hpp>
#include <SushiRuntime/memory/resource_liveness.hpp>
#include <SushiRuntime/memory/resource_region.hpp>

namespace SushiRuntime
{
    namespace Detail
    {
        /**
         * @brief Appends the read and the observer of a device count's slot.
         * @param reads Gains the region of the slot's buffer when the count has a slot.
         * @param lives Gains the observer of that buffer when it watches an allocation.
         * @return True when the count is read from a device slot.
         * @see modules/surface/api/include/SushiRuntime/api/detail/README.md
         */
        inline bool gather_device_read(const LateCount& count,
                                       std::vector<Internal::Memory::ResourceRegion>& reads,
                                       std::vector<Internal::Memory::LivenessObserver>& lives)
        {
            if (LateCountAccess::slot(count) == nullptr)
                return false;
            reads.push_back(LateCountAccess::slot_region(count));
            const Internal::Memory::LivenessObserver& life = LateCountAccess::slot_life(count);
            if (!life.expired())
                lives.push_back(life);
            return true;
        }

        /**
         * @brief Names where one LateCount's live value comes from, in a form a kernel carries.
         */
        struct LiveCountSource
        {
            const std::uint32_t* slot = nullptr; /**< Device-written word, or null. */
            std::size_t host_live = 0;           /**< The live count when @c slot is null. */
            std::size_t capacity = 0;            /**< Bound a device value must not exceed. */
        };

        /**
         * @brief Resolves a LateCount's host side and names its device slot without reading it.
         * @param count The count to resolve.
         * @return The source a resolver kernel reads.
         * @throws SushiException With invalid_graph if a host provider returns more
         *         than the capacity.
         * @see modules/surface/api/include/SushiRuntime/api/detail/README.md
         */
        inline LiveCountSource late_count_source(const LateCount& count)
        {
            LiveCountSource source;
            source.capacity = count.capacity();
            source.slot = LateCountAccess::slot(count);
            if (source.slot != nullptr)
                return source;

            const LateCount::Provider* host = LateCountAccess::host(count);
            source.host_live = (host != nullptr) ? (*host)() : source.capacity;
            if (source.host_live > source.capacity)
                throw SushiException(
                    late_count_overflow(source.host_live, source.capacity));
            return source;
        }

        /**
         * @brief Reads a live count on the device; one over capacity faults and yields zero.
         * @param source Where the count lives, from late_count_source().
         * @param fault  The resolver's fault record; null only when @p source has no slot.
         * @return The live count, never above @p source.capacity.
         */
        inline std::size_t read_live_count(const LiveCountSource& source,
                                           std::uint32_t* fault) noexcept
        {
            if (source.slot == nullptr)
                return source.host_live;
            const std::size_t live = static_cast<std::size_t>(*source.slot);
            if (live <= source.capacity)
                return live;
            record_device_fault(fault, K_DEVICE_FAULT_LATE_COUNT, 0, live, source.capacity);
            return 0;
        }
    } // namespace Detail
} // namespace SushiRuntime
