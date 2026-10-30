/**************************************************************************/
/* span_loss_tally.cpp                                                    */
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
 * @file span_loss_tally.cpp
 * @brief Implements SpanLossTally.
 * @author Mustafa Garip
 * @see modules/surface/profiling/README.md
 */

#include <SushiRuntime/profiling/span_loss_tally.hpp>

#include <array>
#include <string_view>

namespace SushiRuntime
{
    namespace Internal::Profiling
    {
        namespace
        {
            /** @brief The words a report uses for each cause, indexed by SpanLoss. */
            constexpr std::array<std::string_view, 9> K_CAUSE_NAMES = {
                "unreadable event", "full row", "unknown node", "no event", "unprofiled queue",
                "no clock pairing", "queue overflow", "older than its period", "refused by the table"};
        } // namespace

        void SpanLossTally::add(SpanLoss cause, std::uint64_t count) noexcept
        {
            counts_[static_cast<std::size_t>(cause)] += count;
        }

        void SpanLossTally::add(const DropCounts& drops) noexcept
        {
            add(SpanLoss::UnreadableEvent, drops.of(DropCause::UnreadableEvent));
            add(SpanLoss::FullRow, drops.of(DropCause::FullRow));
            add(SpanLoss::UnknownNode, drops.of(DropCause::UnknownNode));
            add(SpanLoss::NoEvent, drops.of(DropCause::NoEvent));
            add(SpanLoss::NoQueue, drops.of(DropCause::NoQueue));
        }

        std::optional<std::string> SpanLossTally::take_report(std::uint64_t now_steady_ns)
        {
            if (reported_ns_ != 0 && now_steady_ns - reported_ns_ < K_REPORT_INTERVAL_NS)
                return std::nullopt;
            std::string line;
            for (std::size_t cause = 0; cause < K_CAUSES; ++cause)
            {
                if (counts_[cause] == 0)
                    continue;
                line += line.empty() ? "" : ", ";
                line += std::to_string(counts_[cause]) + " " + std::string(K_CAUSE_NAMES[cause]);
            }
            if (line.empty())
                return std::nullopt;
            counts_ = {};
            reported_ns_ = now_steady_ns;
            return line;
        }
    } // namespace Internal::Profiling
} // namespace SushiRuntime
