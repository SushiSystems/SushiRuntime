/**************************************************************************/
/* test_late_count.cpp                                                   */
/**************************************************************************/
/*                          This file is part of:                         */
/*                              SushiRuntime                              */
/*              https://github.com/SushiSystems/SushiRuntime              */
/*                        https://sushisystems.io                         */
/**************************************************************************/
/* Copyright (c) 2026-present Mustafa Garip & Sushi Systems               */
/*                                                                        */
/* Licensed under the Apache License, Version 2.0 (the "License");        */
/* you may not use this file except in compliance with the License.       */
/* You may obtain a copy of the License at                                */
/*                                                                        */
/*     http://www.apache.org/licenses/LICENSE-2.0                         */
/*                                                                        */
/* Unless required by applicable law or agreed to in writing, software    */
/* distributed under the License is distributed on an "AS IS" BASIS,      */
/* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or        */
/* implied. See the License for the specific language governing           */
/* permissions and limitations under the License.                         */
/**************************************************************************/

// Coverage for the tiled level plan (detail::plan_tiled_levels / live_tile_levels,
// api/detail/fixed_order_scan.hpp) and for LateCount's one resolution point
// (detail::resolve_late_count, api/graph/graph.hpp). Both are pure host functions,
// so this suite needs no device and no Runtime.

#include <cstddef>
#include <vector>

#include <gtest/gtest.h>

#include <SushiRuntime/SushiRuntime.h>

using namespace SushiRuntime;
using SushiRuntime::API::detail::live_tile_levels;
using SushiRuntime::API::detail::plan_reduction;
using SushiRuntime::API::detail::plan_tiled_levels;
using SushiRuntime::API::detail::resolve_late_count;

// The level count for a capacity is the same tree add_reduce already plans,
// exercised at the boundaries the tile size creates.
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

// A live count below capacity must fold into the same tile boundaries that count
// would get if it were the capacity itself: the levels below the point where the
// live chain reaches one value line up index for index with plan_reduction(n).
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
        // Every level past what plan_reduction(n) needed degenerates to a
        // single-element pass-through: folding it changes nothing.
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

// resolve_late_count is the one resolution point: with neither a host provider
// nor a device slot, the live count is the capacity, matching a fixed call.
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

// A live count above capacity fails the run, the same way an out-of-window
// based_at() does, rather than silently clamping or reading past the end.
TEST(Unit_LateCount, AboveCapacityIsRejected)
{
    LateCount lc;
    lc.capacity = 100;
    lc.host = []() { return std::size_t{101}; };
    EXPECT_THROW(resolve_late_count(lc), SushiException);
}

// A Residency::Host device slot resolves the value a predecessor wrote there,
// exactly as sized_from_device() reads its own device_count slot.
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

// A device slot that is not Residency::Host is rejected at registration, the
// way require_host_device_slots() rejects a non-host sized_from_device() slot,
// rather than only failing the first time the graph runs.
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
