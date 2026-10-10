/**************************************************************************/
/* test_late_count.cpp                                                    */
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
 * @file test_late_count.cpp
 * @brief Tests the tiled level plan and LateCount's resolution point, both pure host functions.
 * @see modules/surface/api/tests/README.md
 * @author Mustafa Garip
 */

#include <cstddef>
#include <cstdint>
#include <vector>

#include <gtest/gtest.h>

#include <SushiRuntime/SushiRuntime.h>

using namespace SushiRuntime;
using SushiRuntime::API::detail::live_tile_levels;
using SushiRuntime::API::detail::plan_reduction;
using SushiRuntime::API::detail::plan_tiled_levels;
using SushiRuntime::API::detail::resolve_late_count;
using SushiRuntime::API::detail::fill_radix_plan;
using SushiRuntime::API::detail::radix_digit_count;
namespace detail = SushiRuntime::API::detail;

/** @brief Tests the level count for a capacity at the tile-size boundaries. */
TEST(Unit_LateCount, LevelCountsAtTileBoundaries)
{
    EXPECT_EQ(plan_tiled_levels(1).size(), 1u);
    EXPECT_EQ(plan_tiled_levels(255).size(), 1u);
    EXPECT_EQ(plan_tiled_levels(256).size(), 1u);
    EXPECT_EQ(plan_tiled_levels(257).size(), 2u);
    EXPECT_EQ(plan_tiled_levels(65536).size(), 2u);
    EXPECT_EQ(plan_tiled_levels(65537).size(), 3u);
    EXPECT_EQ(plan_tiled_levels(1000000).size(), 3u);
}

/**
 * @brief Tests that a live count below capacity folds into the tile boundaries plan_reduction gives
 *        that count.
 */
TEST(Unit_LateCount, LiveCountBelowCapacityMatchesThatCountAtCapacity)
{
    const std::size_t capacity = 1000000;
    const auto capacity_levels = plan_tiled_levels(capacity);

    const std::size_t live_counts[] = { 1, 255, 256, 257, 65536, 65537 };
    for (std::size_t n : live_counts)
    {
        const auto live = live_tile_levels(capacity_levels, n);
        const auto fixed = plan_reduction(n);

        ASSERT_EQ(live.size(), capacity_levels.size());
        for (std::size_t i = 0; i < fixed.size(); ++i)
        {
            EXPECT_EQ(live[i].in_count, fixed[i].in_count) << "n=" << n << " level " << i;
            EXPECT_EQ(live[i].out_count, fixed[i].out_count) << "n=" << n << " level " << i;
        }
        // Every level past what plan_reduction(n) needs is a 1-element pass-through; see README.md.
        for (std::size_t i = fixed.size(); i < live.size(); ++i)
        {
            EXPECT_EQ(live[i].in_count, 1u) << "n=" << n << " level " << i;
            EXPECT_EQ(live[i].out_count, 1u) << "n=" << n << " level " << i;
        }
    }
}

// A live count of zero folds no level anywhere in the tree.
TEST(Unit_LateCount, LiveCountZeroFoldsNothing)
{
    const auto capacity_levels = plan_tiled_levels(1000000);
    const auto live = live_tile_levels(capacity_levels, 0);
    for (const auto& level : live)
    {
        EXPECT_EQ(level.in_count, 0u);
        EXPECT_EQ(level.out_count, 0u);
    }
}

/**
 * @brief Tests that a LateCount with neither a host provider nor a device slot resolves to its
 *        capacity.
 */
TEST(Unit_LateCount, DefaultResolvesToCapacity)
{
    LateCount lc;
    lc.capacity = 42;
    EXPECT_EQ(resolve_late_count(lc), 42u);
}

// A host provider below capacity is honoured as the live count.
TEST(Unit_LateCount, HostProviderBelowCapacity)
{
    LateCount lc;
    lc.capacity = 1000000;
    lc.host = []() { return std::size_t{700000}; };
    EXPECT_EQ(resolve_late_count(lc), 700000u);
}

/** @brief Tests that a live count above capacity is rejected. */
TEST(Unit_LateCount, AboveCapacityIsRejected)
{
    LateCount lc;
    lc.capacity = 100;
    lc.host = []() { return std::size_t{101}; };
    EXPECT_THROW(resolve_late_count(lc), SushiException);
}

/** @brief Tests that a Residency::Host device slot resolves the value written there. */
TEST(Unit_LateCount, DeviceSlotResolvesFromHostResidentBuffer)
{
    auto rt = Runtime::create();
    auto counter = rt.buffer<std::uint32_t>(1, DeviceIndex{0}, Residency::Host);
    counter[0] = 700000;

    LateCount lc;
    lc.capacity = 1000000;
    lc.device = &counter;
    EXPECT_EQ(resolve_late_count(lc), 700000u);
}

/** @brief Tests that a device slot that is not Residency::Host is rejected at registration. */
TEST(Unit_LateCount, NonHostDeviceSlotIsRejectedAtRegistration)
{
    auto rt = Runtime::create();
    auto in  = rt.buffer<float>(16);
    auto out = rt.buffer<float>(1);
    auto counter = rt.buffer<std::uint32_t>(1); // default residency: Shared, not Host.

    LateCount lc;
    lc.capacity = 16;
    lc.device = &counter;

    auto g = rt.graph();
    EXPECT_THROW(g.add_reduce(in, out, lc, Sum<float>{}), SushiException);
}

/** @brief Tests the radix digit count at the 8-bit boundaries. */
TEST(Unit_LateCount, RadixDigitCountAtByteBoundaries)
{
    EXPECT_EQ(radix_digit_count(0), 1u);
    EXPECT_EQ(radix_digit_count(1), 1u);
    EXPECT_EQ(radix_digit_count(256), 1u);
    EXPECT_EQ(radix_digit_count(257), 2u);
    EXPECT_EQ(radix_digit_count(1u << 16), 2u);
    EXPECT_EQ(radix_digit_count((1u << 16) + 1), 3u);
    EXPECT_EQ(radix_digit_count(1u << 24), 3u);
    EXPECT_EQ(radix_digit_count((1u << 24) + 1), 4u);
}

/** @brief Tests that a three-digit plan runs only the digits the live key range needs. */
TEST(Unit_LateCount, RadixPlanSkipsDigitsAboveTheLiveKeyRange)
{
    constexpr std::size_t DIGITS = 3;
    std::vector<std::uint32_t> plan(DIGITS * detail::RADIX_PLAN_WORDS, 0xFFFFFFFFu);

    auto word = [&plan](std::size_t d, std::size_t offset)
    {
        return plan[d * detail::RADIX_PLAN_WORDS + offset];
    };

    for (std::size_t live_digits = 1; live_digits <= DIGITS; ++live_digits)
    {
        const std::size_t live_key_range = std::size_t{1} << (8 * live_digits);
        fill_radix_plan(DIGITS, 1000, 4, live_key_range, plan.data());
        for (std::size_t d = 0; d < DIGITS; ++d)
        {
            const bool active = d < live_digits;
            EXPECT_EQ(word(d, detail::RADIX_PLAN_TILES), active ? 4u : 0u)
                << "live digits " << live_digits << ", d " << d;
            EXPECT_EQ(word(d, detail::RADIX_PLAN_BINS), active ? 256u : 0u);
            EXPECT_EQ(word(d, detail::RADIX_PLAN_ONE), active ? 1u : 0u);
            EXPECT_EQ(word(d, detail::RADIX_PLAN_LIVE_N), 1000u);
            const std::uint32_t expected_into =
                (active && (live_digits - 1 - d) % 2 == 0) ? 1u : 0u;
            EXPECT_EQ(word(d, detail::RADIX_PLAN_INTO_INDICES), expected_into);
        }
        EXPECT_EQ(word(live_digits - 1, detail::RADIX_PLAN_INTO_INDICES), 1u)
            << "the last active digit must write the indices";
    }
}
