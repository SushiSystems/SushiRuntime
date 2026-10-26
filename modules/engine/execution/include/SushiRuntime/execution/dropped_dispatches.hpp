/**************************************************************************/
/* dropped_dispatches.hpp                                                 */
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
 * @file dropped_dispatches.hpp
 * @brief Declares log_dropped_dispatches, which names the dispatches a run left out of its timing.
 * @author Mustafa Garip
 * @see modules/engine/execution/README.md
 */

#pragma once

#include <cstdint>
#include <SushiRuntime/log/logger.hpp>
#include <SushiRuntime/report/drop_counts.hpp>
#include <SushiRuntime/execution/execution_log_category.hpp>

namespace SushiRuntime
{
    namespace Internal::Execution
    {
        /**
         * @brief Logs one WARN naming each cause and its count when @p drops holds any loss; nothing otherwise.
         * @see modules/engine/execution/README.md
         */
        inline void log_dropped_dispatches(const DropCounts& drops)
        {
            if (!drops.any())
                return;

                        std::uint32_t total = 0;
            for (const std::uint32_t count : drops.per_cause)
                total += count;

            if (drops.first_node == DropCounts::K_NO_NODE)
                SR_LOG_WARN(K_EXECUTION_LOG_CATEGORY,
                            "The run's timing record lacks {} dispatches: {} with an unreadable event, {} for a full "
                            "ledger row, {} for a node past the table, {} kernels with no event, {} with no profiling "
                            "queue; the record is incomplete.",
                            total, drops.of(DropCause::UnreadableEvent), drops.of(DropCause::FullRow),
                            drops.of(DropCause::UnknownNode), drops.of(DropCause::NoEvent),
                            drops.of(DropCause::NoQueue));
            else
                SR_LOG_WARN(K_EXECUTION_LOG_CATEGORY,
                            "The run's timing record lacks {} dispatches: {} with an unreadable event, {} for a full "
                            "ledger row, {} for a node past the table, {} kernels with no event, {} with no profiling "
                            "queue; the first was node {}; the record is incomplete.",
                            total, drops.of(DropCause::UnreadableEvent), drops.of(DropCause::FullRow),
                            drops.of(DropCause::UnknownNode), drops.of(DropCause::NoEvent),
                            drops.of(DropCause::NoQueue), drops.first_node);
        }
    } // namespace Execution
} // namespace SushiRuntime
