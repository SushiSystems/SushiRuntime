/**************************************************************************/
/* span_loss_tally.hpp                                                    */
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
 * @file span_loss_tally.hpp
 * @brief Declares SpanLossTally, which counts the device spans the device source loses, by cause.
 * @author Mustafa Garip
 * @see modules/surface/profiling/README.md
 */

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

#include <SushiRuntime/core/export.hpp>
#include <SushiRuntime/report/drop_counts.hpp>

namespace SushiRuntime
{
    namespace Internal::Profiling
    {
        /** @brief Names why the device source kept no span for a dispatch. */
        enum class SpanLoss : std::uint8_t
        {
            UnreadableEvent, /**< The run could not read the event's timestamps. */
            FullRow,         /**< The node's row in the run had no free slot. */
            UnknownNode,     /**< The registration index was past the run's rows. */
            NoEvent,         /**< The dispatch returned no event to read. */
            NoQueue,         /**< The dispatch ran on a queue the run does not profile. */
            NoPairing,       /**< The run paired no clock for the span's device. */
            QueueOverflow,   /**< A full observation queue dropped the run before the source read it. */
            OlderThanPeriod, /**< The span started before the period it arrived in. */
            Refused          /**< The period's span table refused the span. */
        };

        /**
         * @brief Counts lost device spans by cause and says so at most once per interval.
         *
         * The profiler's record keeps one total; this keeps the cause, so a log line can tell a
         * full row from a late span. Time is the steady end of the periods collected, not a clock
         * read, so the same run reports the same lines.
         * @see modules/surface/profiling/README.md
         */
        class SUSHIRUNTIME_API SpanLossTally final
        {
            public:
                /** @brief The shortest steady time between two reports: 5 s. */
                static constexpr std::uint64_t K_REPORT_INTERVAL_NS = 5'000'000'000ULL;

                /** @brief Adds @p count spans lost for @p cause. */
                void add(SpanLoss cause, std::uint64_t count) noexcept;

                /** @brief Adds every cause of a run's own drop counts. */
                void add(const DropCounts& drops) noexcept;

                /**
                 * @brief Returns one line of the losses since the last report, and starts again.
                 * @param now_steady_ns The steady end of the period just collected.
                 * @return The line, or nothing when no span was lost or the interval has not passed.
                 */
                [[nodiscard]] std::optional<std::string> take_report(std::uint64_t now_steady_ns);

            private:
                /** @brief Number of causes in SpanLoss. */
                static constexpr std::size_t K_CAUSES = 9;

                /** @brief Spans lost since the last report, indexed by SpanLoss. */
                std::array<std::uint64_t, K_CAUSES> counts_{};

                /** @brief Steady time of the last report; 0 before the first. */
                std::uint64_t reported_ns_ = 0;
        };
    } // namespace Internal::Profiling
} // namespace SushiRuntime
