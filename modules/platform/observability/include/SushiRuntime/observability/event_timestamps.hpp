/**************************************************************************/
/* event_timestamps.hpp                                                   */
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
 * @file event_timestamps.hpp
 * @brief Declares EventTimestamps and the function that reads them from a SYCL event.
 * @author Mustafa Garip
 * @see modules/platform/observability/README.md
 */

#pragma once

#include <algorithm>
#include <cstdint>
#include <vector>
#include <sycl/sycl.hpp>

namespace SushiRuntime
{
    namespace Internal::Observability
    {
        /**
         * @brief Holds one event's start and end timestamps on the backend's event clock.
         * @see modules/platform/observability/README.md
         */
        struct EventTimestamps
        {
            std::uint64_t start_ns = 0; /**< command_start. */
            std::uint64_t end_ns   = 0; /**< command_end. */
        };

        /**
         * @brief Reads an event's start and end timestamps without throwing.
         * @param event A completed event from a queue built with enable_profiling.
         * @param out Filled only when the function returns true.
         * @return True when start and end were read.
         * @see modules/platform/observability/README.md
         */
        inline bool read_event_timestamps(const sycl::event& event, EventTimestamps& out) noexcept
        {
            std::uint64_t start_ns = 0;
            std::uint64_t end_ns   = 0;
            try
            {
                start_ns = event.get_profiling_info<sycl::info::event_profiling::command_start>();
                end_ns   = event.get_profiling_info<sycl::info::event_profiling::command_end>();
            }
            catch (...)
            {
                return false;
            }
            out.start_ns = start_ns;
            out.end_ns   = end_ns;
            return true;
        }

        /** @brief Folds @p next into @p into: the earliest start, the latest end. */
        inline void merge_timestamps(EventTimestamps& into, const EventTimestamps& next) noexcept
        {
            into.start_ns = std::min(into.start_ns, next.start_ns);
            into.end_ns   = std::max(into.end_ns, next.end_ns);
        }

        /**
         * @brief Reads the timestamps of one dispatch, across every event its body handed over.
         * @param returned The event the dispatch returned; read alone when @p handed_over is empty.
         * @param handed_over The events the body handed over; the dispatch is timed across them.
         * @param out Filled only when the function returns true.
         * @return True when every event read.
         * @see modules/platform/observability/README.md
         */
        inline bool read_dispatch_timestamps(const sycl::event& returned,
                                             const std::vector<sycl::event>& handed_over,
                                             EventTimestamps& out) noexcept
        {
            if (handed_over.empty())
                return read_event_timestamps(returned, out);

            EventTimestamps total;
            bool first = true;
            for (const sycl::event& event : handed_over)
            {
                EventTimestamps one;
                if (!read_event_timestamps(event, one))
                    return false;
                if (first)
                    total = one;
                else
                    merge_timestamps(total, one);
                first = false;
            }
            out = total;
            return true;
        }

    } // namespace Observability
} // namespace SushiRuntime
