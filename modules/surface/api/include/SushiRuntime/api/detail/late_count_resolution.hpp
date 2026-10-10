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
 * @brief Declares the helpers that validate a LateCount and read its live value.
 * @author Mustafa Garip
 * @see modules/surface/api/include/SushiRuntime/api/detail/README.md
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>

#include <SushiRuntime/api/data/buffer.hpp>
#include <SushiRuntime/api/detail/device_fault.hpp>
#include <SushiRuntime/api/vocabulary/late_count.hpp>
#include <SushiRuntime/core/error.hpp>

namespace SushiRuntime
{
    namespace API
    {
        namespace detail
        {
            /**
             * @brief Reads a LateCount's live value and checks it against its capacity.
             * @throws Core::SushiException With invalid_access if @c lc.device is set
             *        but is not Residency::Host, or invalid_graph if the live count
             *        exceeds @c lc.capacity, the same way an out-of-window based_at()
             *        fails the run.
             * @see modules/surface/api/include/SushiRuntime/api/detail/README.md
             */
            inline std::size_t resolve_late_count(const LateCount& lc)
            {
                std::size_t live = lc.capacity;
                if (lc.host)
                {
                    live = lc.host();
                }
                else if (lc.device != nullptr)
                {
                    if (lc.device->residency() != Residency::Host)
                        throw Core::SushiException(Core::Error{Core::Errc::invalid_access,
                            "LateCount: the device slot is Residency::" +
                            std::string(residency_name(lc.device->residency())) +
                            ", but the live count is read on the host while kernels "
                            "may run; allocate it with Residency::Host."});
                    if (lc.device_index >= lc.device->size())
                        throw Core::SushiException(Core::Error{Core::Errc::invalid_access,
                            "LateCount: device_index " + std::to_string(lc.device_index) +
                            " is out of range for a buffer of " +
                            std::to_string(lc.device->size()) + " elements."});
                    live = static_cast<std::size_t>(lc.device->data()[lc.device_index]);
                }

                if (live > lc.capacity)
                    throw Core::SushiException(Core::Error{Core::Errc::invalid_graph,
                        "LateCount: the live count (" + std::to_string(live) +
                        ") exceeds the capacity it was planned for (" +
                        std::to_string(lc.capacity) + ")."});
                return live;
            }

            /**
             * @brief Rejects an ill-formed LateCount at registration, before add() changes the
             *        graph.
             * @throws Core::SushiException With invalid_access for a non-host
             *        device slot, or invalid_graph for an oversized capacity.
             * @see modules/surface/api/include/SushiRuntime/api/detail/README.md
             */
            inline void validate_late_count(const LateCount& lc, const char* what)
            {
                if (lc.capacity > static_cast<std::size_t>(
                        std::numeric_limits<std::uint32_t>::max()))
                    throw Core::SushiException(Core::Error{Core::Errc::invalid_graph,
                        std::string(what) + ": capacity (" + std::to_string(lc.capacity) +
                        ") exceeds what a uint32 live-count slot can hold (" +
                        std::to_string(std::numeric_limits<std::uint32_t>::max()) + ")."});
                if (lc.device != nullptr && lc.device->residency() != Residency::Host)
                    throw Core::SushiException(Core::Error{Core::Errc::invalid_access,
                        std::string(what) + ": the device slot is Residency::" +
                        std::string(residency_name(lc.device->residency())) +
                        ", but the live count is read while kernels may run; "
                        "allocate it with Residency::Host."});
            }
        }

        namespace detail
        {
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
             * @param lc The count to resolve.
             * @return The source a resolver kernel reads.
             * @throws Core::SushiException As resolve_late_count(), or invalid_access for a device
             *         index out of range.
             */
            inline LiveCountSource late_count_source(const LateCount& lc)
            {
                LiveCountSource source;
                source.capacity = lc.capacity;
                if (lc.host || lc.device == nullptr)
                {
                    source.host_live = resolve_late_count(lc);
                    return source;
                }
                if (lc.device_index >= lc.device->size())
                    throw Core::SushiException(Core::Error{Core::Errc::invalid_access,
                        "LateCount: device_index " + std::to_string(lc.device_index) +
                        " is out of range for a buffer of " +
                        std::to_string(lc.device->size()) + " elements."});
                source.slot = lc.device->data() + lc.device_index;
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
                record_device_fault(fault, DEVICE_FAULT_LATE_COUNT, 0, live, source.capacity);
                return 0;
            }
        }

    } // namespace API
} // namespace SushiRuntime
