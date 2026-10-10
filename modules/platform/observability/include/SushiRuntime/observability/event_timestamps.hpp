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

#include <cstdint>
#include <sycl/sycl.hpp>

namespace SushiRuntime
{
    namespace Observability
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

    } // namespace Observability

    namespace Core
    {
        /** @brief Names Observability::EventTimestamps by its spelling before v0.4.0. */
        using EventTimestamps [[deprecated("use Observability::EventTimestamps")]] =
            Observability::EventTimestamps;

        /** @brief Forwards to Observability::read_event_timestamps(). */
        [[deprecated("use Observability::read_event_timestamps")]]
        inline bool read_event_timestamps(const sycl::event& event,
                                          Observability::EventTimestamps& out) noexcept
        {
            return Observability::read_event_timestamps(event, out);
        }

    } // namespace Core
} // namespace SushiRuntime
