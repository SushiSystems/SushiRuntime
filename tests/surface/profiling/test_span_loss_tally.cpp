/**************************************************************************/
/* test_span_loss_tally.cpp                                               */
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
 * @file test_span_loss_tally.cpp
 * @brief Tests SpanLossTally, the count of lost device spans by cause and its throttled report.
 * @see tests/surface/profiling/README.md
 * @author Mustafa Garip
 */

#include <cstdint>
#include <optional>
#include <string>

#include <gtest/gtest.h>

#include <SushiRuntime/profiling/span_loss_tally.hpp>

using namespace SushiRuntime;
using SushiRuntime::Internal::Profiling::SpanLoss;
using SushiRuntime::Internal::Profiling::SpanLossTally;

namespace
{
    /** @brief One second on the steady clock. */
    constexpr std::uint64_t K_SECOND_NS = 1'000'000'000ULL;
}

TEST(Small_SpanLossTally, NamesEachCauseWithItsCountInOneLine)
{
    SpanLossTally tally;
    DropCounts drops;
    drops.per_cause[static_cast<std::size_t>(DropCause::FullRow)] = 3;
    tally.add(drops);
    tally.add(SpanLoss::OlderThanPeriod, 2U);
    tally.add(SpanLoss::QueueOverflow, 0U);
    EXPECT_EQ(tally.take_report(10 * K_SECOND_NS), std::optional<std::string>("3 full row, 2 older than its period"));
}

TEST(Small_SpanLossTally, SaysNothingWhenNoSpanWasLost)
{
    SpanLossTally tally;
    EXPECT_EQ(tally.take_report(10 * K_SECOND_NS), std::nullopt);
}

/** @brief Tests that at most one report comes per steady interval, and the counts held back arrive after it. */
TEST(Small_SpanLossTally, ReportsAtMostOncePerInterval)
{
    SpanLossTally tally;
    tally.add(SpanLoss::NoPairing, 1U);
    ASSERT_TRUE(tally.take_report(10 * K_SECOND_NS).has_value());
    tally.add(SpanLoss::NoPairing, 4U);

    EXPECT_EQ(tally.take_report(12 * K_SECOND_NS), std::nullopt) << "two seconds is inside the interval";
    EXPECT_EQ(tally.take_report(15 * K_SECOND_NS), std::optional<std::string>("4 no clock pairing"));
}

/** @brief Tests that a report clears the counts it named, so the next one names only what came after. */
TEST(Small_SpanLossTally, AReportClearsTheCountsItNamed)
{
    SpanLossTally tally;
    tally.add(SpanLoss::NoPairing, 1U);
    ASSERT_TRUE(tally.take_report(10 * K_SECOND_NS).has_value());
    tally.add(SpanLoss::Refused, 1U);

    EXPECT_EQ(tally.take_report(15 * K_SECOND_NS), std::optional<std::string>("1 refused by the table"));
}
