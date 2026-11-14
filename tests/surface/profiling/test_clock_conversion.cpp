/**************************************************************************/
/* test_clock_conversion.cpp                                              */
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
 * @file test_clock_conversion.cpp
 * @brief Tests the conversion of a device's event clock to the steady clock through its pairing.
 * @see tests/surface/profiling/README.md
 * @author Mustafa Garip
 */

#include <cstdint>
#include <limits>
#include <vector>

#include <gtest/gtest.h>

#include <SushiRuntime/profiling/clock_conversion.hpp>

using namespace SushiRuntime;
using namespace SushiRuntime::Internal::Profiling;

namespace
{
    /** @brief Pairs event time 5000 with steady time 2000. */
    constexpr ClockPairing K_PAIRING{5000, 2000, 25};
} // namespace

namespace
{
    /** @brief One event time and the steady reading the pairing maps it to. */
    struct ConversionRow
    {
        /** @brief The device's event time. */
        std::uint64_t event_ns;
        /** @brief The steady reading the pairing gives it. */
        std::uint64_t steady_ns;
    };

    /** @brief Checks that every row converts to its steady reading through the pairing. */
    void expect_converts(const ClockPairing& pairing, const std::vector<ConversionRow>& rows)
    {
        for (const ConversionRow& row : rows)
        {
            SCOPED_TRACE(row.event_ns);
            EXPECT_EQ(to_steady_ns(pairing, row.event_ns), row.steady_ns);
        }
    }
} // namespace

/** @brief Tests that an event time maps to the steady reading offset by its distance from the paired instant. */
TEST(Small_ClockConversion, AnEventTimeMapsByItsDistanceFromThePairedInstant)
{
    expect_converts(K_PAIRING, {{5000, 2000}, {5750, 2750}, {4600, 1600}});
}

/** @brief Tests that the event time one nanosecond past the steady origin reads 1, not 0. */
TEST(Small_ClockConversion, ATimeOneNanosecondPastTheOriginReadsOne)
{
    expect_converts(K_PAIRING, {{3001, 1}});
}

/** @brief Tests that a time before the steady clock's origin reads 0 and does not wrap. */
TEST(Small_ClockConversion, ATimeBeforeTheOriginReadsZero)
{
    EXPECT_EQ(to_steady_ns(K_PAIRING, 3000), 0u);
    EXPECT_EQ(to_steady_ns(K_PAIRING, 0), 0u);
}

/** @brief Tests that the pairing's event clock may be far larger than the steady one, as a GPU counter is. */
TEST(Small_ClockConversion, AnEventClockFarAheadOfTheSteadyClockConverts)
{
    const ClockPairing gpu{9'000'000'000'000ULL, 1'000'000ULL, 0};

    EXPECT_EQ(to_steady_ns(gpu, 9'000'000'000'500ULL), 1'000'500ULL);
}

/** @brief Tests that an uncertainty that fits 32 bits is kept and one that does not saturates. */
TEST(Small_ClockConversion, SaturatesTheUncertaintyAt32Bits)
{
    constexpr std::uint32_t K_LARGEST = std::numeric_limits<std::uint32_t>::max();

    EXPECT_EQ(saturate_uncertainty(0), 0u);
    EXPECT_EQ(saturate_uncertainty(12345), 12345u);
    EXPECT_EQ(saturate_uncertainty(K_LARGEST), K_LARGEST);
    EXPECT_EQ(saturate_uncertainty(std::uint64_t{K_LARGEST} + 1), K_LARGEST);
    EXPECT_EQ(saturate_uncertainty(std::numeric_limits<std::uint64_t>::max()), K_LARGEST);
}

/**
 * @brief Tests that the stated uncertainty bounds the error of a converted time.
 *
 * The pairing's steady reading is the midpoint of its bounds, so the true steady instant of its event
 * reading is anywhere within the uncertainty of it, on either side. Whichever instant it is, a
 * converted event time is off its true steady time by at most the uncertainty.
 */
TEST(Small_ClockConversion, TheStatedUncertaintyBoundsTheErrorOfAConvertedTime)
{
    const std::vector<std::uint64_t> events{4000, 5000, 5001, 5300, 9000};

    for (std::uint64_t offset = 0; offset <= 2 * K_PAIRING.uncertainty_ns; ++offset)
    {
        const std::uint64_t true_instant = K_PAIRING.steady_ns - K_PAIRING.uncertainty_ns + offset;
        for (const std::uint64_t event : events)
        {
            const std::uint64_t truth = true_instant + event - K_PAIRING.event_ns;
            const std::uint64_t converted = to_steady_ns(K_PAIRING, event);
            const std::uint64_t error = converted > truth ? converted - truth : truth - converted;
            EXPECT_LE(error, K_PAIRING.uncertainty_ns) << "event " << event << " offset " << offset;
        }
    }
}
