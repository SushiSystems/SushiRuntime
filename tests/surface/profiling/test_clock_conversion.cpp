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
 * @file test_clock_conversion.cpp
 * @brief Tests the conversion of a device's event clock to the steady clock through its pairing.
 * @see tests/surface/profiling/README.md
 * @author Mustafa Garip
 */

#include <cstdint>
#include <limits>

#include <gtest/gtest.h>

#include <SushiRuntime/profiling/clock_conversion.hpp>

using namespace SushiRuntime;
using namespace SushiRuntime::Internal::Profiling;

namespace
{
    /** @brief Pairs event time 5000 with steady time 2000. */
    constexpr ClockPairing K_PAIRING{5000, 2000, 25};
} // namespace

/** @brief Tests that the paired instant maps to its steady reading. */
TEST(Small_ClockConversion, ThePairedInstantMapsToItsSteadyReading)
{
    EXPECT_EQ(to_steady_ns(K_PAIRING, 5000), 2000u);
}

/** @brief Tests that a later event time lands the same distance after the steady reading. */
TEST(Small_ClockConversion, ALaterEventTimeLandsTheSameDistanceLater)
{
    EXPECT_EQ(to_steady_ns(K_PAIRING, 5750), 2750u);
}

/** @brief Tests that an earlier event time lands the same distance before the steady reading. */
TEST(Small_ClockConversion, AnEarlierEventTimeLandsTheSameDistanceEarlier)
{
    EXPECT_EQ(to_steady_ns(K_PAIRING, 4600), 1600u);
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
