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
        /** @brief Holds one event's profiling timestamps on the backend's event clock. */
        struct EventTimestamps
        {
            std::uint64_t submit_ns = 0;   /**< command_submit, or start_ns when the backend lacks it. */
            std::uint64_t start_ns  = 0;   /**< command_start. */
            std::uint64_t end_ns    = 0;   /**< command_end. */
            bool has_submit = false;       /**< Whether submit_ns is the backend's own reading. */
        };

        /**
         * @brief Reads an event's start, end and submit timestamps without throwing.
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

            try
            {
                out.submit_ns  = event.get_profiling_info<sycl::info::event_profiling::command_submit>();
                out.has_submit = true;
            }
            catch (...)
            {
                out.submit_ns  = start_ns;
                out.has_submit = false;
            }
            return true;
        }

        /**
         * @brief Folds @p next into @p into: the earliest submit and start, the latest end.
         *
         * The submit reading is the backend's own only when both are.
         */
        inline void merge_timestamps(EventTimestamps& into, const EventTimestamps& next) noexcept
        {
            into.submit_ns  = std::min(into.submit_ns, next.submit_ns);
            into.start_ns   = std::min(into.start_ns, next.start_ns);
            into.end_ns     = std::max(into.end_ns, next.end_ns);
            into.has_submit = into.has_submit && next.has_submit;
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
