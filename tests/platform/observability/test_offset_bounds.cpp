/**************************************************************************/
/* test_offset_bounds.cpp                                                 */
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
 * @file test_offset_bounds.cpp
 * @brief Tests the bounds one dispatch places on its device's clock offset and the pairing they make.
 * @see tests/platform/observability/README.md
 * @author Mustafa Garip
 */

#include <cstdint>

#include <gtest/gtest.h>

#include <SushiRuntime/observability/offset_bounds.hpp>

using SushiRuntime::ClockPairing;
using SushiRuntime::Internal::Observability::bounds_of;
using SushiRuntime::Internal::Observability::DispatchWindow;
using SushiRuntime::Internal::Observability::drift_over;
using SushiRuntime::Internal::Observability::narrowed;
using SushiRuntime::Internal::Observability::OffsetBounds;
using SushiRuntime::Internal::Observability::pairing_from;

/** @brief Tests that a dispatch bounds the offset between submit minus start and reap minus end. */
TEST(Small_OffsetBounds, ADispatchBoundsTheOffsetBySubmitMinusStartAndReapMinusEnd)
{
    // 1000 - 400 = 600 and 1500 - 700 = 800.
    const OffsetBounds bounds = bounds_of(DispatchWindow{1000, 400, 700, 1500});

    EXPECT_EQ(bounds.low_ns, 600);
    EXPECT_EQ(bounds.high_ns, 800);
}

/** @brief Tests that a device clock ahead of the steady clock gives negative bounds. */
TEST(Small_OffsetBounds, ADeviceClockAheadOfTheSteadyClockGivesNegativeBounds)
{
    // 1000 - 9400 = -8400 and 1500 - 9700 = -8200.
    const OffsetBounds bounds = bounds_of(DispatchWindow{1000, 9400, 9700, 1500});

    EXPECT_EQ(bounds.low_ns, -8400);
    EXPECT_EQ(bounds.high_ns, -8200);
}

/** @brief Tests that narrowing keeps the larger low and the smaller high, whichever comes first. */
TEST(Small_OffsetBounds, NarrowingKeepsTheLargerLowAndTheSmallerHigh)
{
    const OffsetBounds wide{100, 900};
    const OffsetBounds tight{300, 400};

    const OffsetBounds forward = narrowed(wide, tight);
    const OffsetBounds backward = narrowed(tight, wide);

    EXPECT_EQ(forward.low_ns, 300);
    EXPECT_EQ(forward.high_ns, 400);
    EXPECT_EQ(backward.low_ns, forward.low_ns);
    EXPECT_EQ(backward.high_ns, forward.high_ns);
}

/** @brief Tests that two dispatches that each bound one side give the range both allow. */
TEST(Small_OffsetBounds, TwoDispatchesEachTightOnOneSideGiveBothSides)
{
    const OffsetBounds tight_low{590, 5000};
    const OffsetBounds tight_high{-3000, 610};

    const OffsetBounds kept = narrowed(tight_low, tight_high);

    EXPECT_EQ(kept.low_ns, 590);
    EXPECT_EQ(kept.high_ns, 610);
}

/** @brief Tests that the pairing converts with the midpoint of the bounds and states half their width. */
TEST(Small_OffsetBounds, ThePairingIsTheMidpointAndHalfTheWidth)
{
    // The midpoint of [600, 800] is 700, so event 400 is steady 1100, off by at most 100.
    const ClockPairing pairing = pairing_from(OffsetBounds{600, 800}, 400, 0);

    EXPECT_EQ(pairing.event_ns, 400u);
    EXPECT_EQ(pairing.steady_ns, 1100u);
    EXPECT_EQ(pairing.uncertainty_ns, 100u);
}

/** @brief Tests that an odd width rounds the uncertainty up, so the far bound is still inside it. */
TEST(Small_OffsetBounds, AnOddWidthRoundsTheUncertaintyUp)
{
    // [600, 801]: the midpoint is 700, 101 from the high bound.
    const ClockPairing pairing = pairing_from(OffsetBounds{600, 801}, 0, 0);

    EXPECT_EQ(pairing.steady_ns, 700u);
    EXPECT_EQ(pairing.uncertainty_ns, 101u);
}

/** @brief Tests that negative bounds place the reference event reading below it on the steady clock. */
TEST(Small_OffsetBounds, NegativeBoundsPlaceTheEventBelowItself)
{
    // The midpoint of [-8400, -8200] is -8300, so event 9400 is steady 1100.
    const ClockPairing pairing = pairing_from(OffsetBounds{-8400, -8200}, 9400, 0);

    EXPECT_EQ(pairing.steady_ns, 1100u);
    EXPECT_EQ(pairing.uncertainty_ns, 100u);
}

/** @brief Tests that crossed bounds keep the midpoint of the two and half the gap between them. */
TEST(Small_OffsetBounds, CrossedBoundsKeepTheMidpointAndHalfTheGap)
{
    // A low of 620 above a high of 600: the midpoint is 610, 10 from either.
    const ClockPairing pairing = pairing_from(OffsetBounds{620, 600}, 0, 0);

    EXPECT_EQ(pairing.steady_ns, 610u);
    EXPECT_EQ(pairing.uncertainty_ns, 10u);
}

/** @brief Tests that the drift the offset may make is added to the uncertainty and leaves the midpoint where it is. */
TEST(Small_OffsetBounds, TheDriftWidensTheUncertaintyAndKeepsTheMidpoint)
{
    const ClockPairing pairing = pairing_from(OffsetBounds{600, 800}, 400, 250);

    EXPECT_EQ(pairing.steady_ns, 1100u);
    EXPECT_EQ(pairing.uncertainty_ns, 350u);
}

/** @brief Tests that drift_over() allows 100 ppm of a span, rounded up to the next nanosecond. */
TEST(Small_OffsetBounds, TheDriftOverASpanIsAHundredPpmRoundedUp)
{
    EXPECT_EQ(drift_over(0), 0u);
    EXPECT_EQ(drift_over(1), 1u);
    EXPECT_EQ(drift_over(10'000), 1u);
    EXPECT_EQ(drift_over(10'001), 2u);
    EXPECT_EQ(drift_over(60'000'000'000ULL), 6'000'000u);
}
