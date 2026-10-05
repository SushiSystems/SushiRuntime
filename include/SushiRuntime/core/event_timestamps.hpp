/****************************************************************/
/* event_timestamps.hpp                                         */
/* SushiRuntime - https://github.com/SushiSystems/SushiRuntime  */
/* Copyright (c) 2026 Sushi Systems                             */
/* Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.    */
/* Commercial use requires a licence from Sushi Systems.        */
/****************************************************************/

#pragma once

#include <cstdint>
#include <sycl/sycl.hpp>

namespace SushiRuntime
{
    namespace Core
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
         *
         * A backend with no start/end reading leaves the dispatch unrecorded; one
         * with no submit reading degrades to submit_ns == start_ns.
         * @param event A completed event from a queue built with enable_profiling.
         * @param out   Filled only when the function returns true.
         * @return True when start and end were read.
         */
        inline bool read_event_timestamps(const sycl::event& event, EventTimestamps& out) noexcept
        {
            try
            {
                out.start_ns = event.get_profiling_info<sycl::info::event_profiling::command_start>();
                out.end_ns   = event.get_profiling_info<sycl::info::event_profiling::command_end>();
            }
            catch (...)
            {
                return false;
            }

            try
            {
                out.submit_ns  = event.get_profiling_info<sycl::info::event_profiling::command_submit>();
                out.has_submit = true;
            }
            catch (...)
            {
                out.submit_ns  = out.start_ns;
                out.has_submit = false;
            }
            return true;
        }

    } // namespace Core
} // namespace SushiRuntime
