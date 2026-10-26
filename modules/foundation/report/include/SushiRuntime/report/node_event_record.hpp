/**************************************************************************/
/* node_event_record.hpp                                                  */
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
 * @file node_event_record.hpp
 * @brief Declares NodeEventRecord, the timing of one node dispatch.
 * @author Mustafa Garip
 * @see modules/foundation/report/README.md
 */

#pragma once

#include <cstdint>
#include <SushiRuntime/report/dispatch_width.hpp>

namespace SushiRuntime
{
    /** @brief Names the device of a record that ran on a host thread and used no device queue. */
    inline constexpr std::uint32_t K_HOST_DEVICE_INDEX = 0xFFFFFFFFu;

    /**
     * @brief Times one dispatch of one node, in nanoseconds on the backend's event clock.
     * @see modules/foundation/report/README.md
     */
    struct NodeEventRecord
    {
        std::uint32_t node_index     = 0;                   /**< Registration index of the node in its graph. */
        std::uint32_t invocation     = 0;                   /**< Zero-based dispatch count of the node within the run. */
        std::uint32_t launched_width = K_WIDTH_UNKNOWN;     /**< Elements the dispatch launched over. */
        std::uint32_t live_width     = K_WIDTH_UNKNOWN;     /**< Elements that did work, when known. */
        std::uint64_t submit_ns      = 0;                   /**< SYCL command_submit, or the steady clock for a host node. */
        std::uint64_t start_ns       = 0;                   /**< SYCL command_start, or the steady clock for a host node. */
        std::uint64_t end_ns         = 0;                   /**< SYCL command_end, or the steady clock for a host node. */
        std::uint32_t device_index   = K_HOST_DEVICE_INDEX; /**< Device whose queue ran it, or K_HOST_DEVICE_INDEX. */
        std::uint32_t submit_thread  = 0;                   /**< Profiler thread index of the submitting thread; 0 when unknown. */
    };
} // namespace SushiRuntime
