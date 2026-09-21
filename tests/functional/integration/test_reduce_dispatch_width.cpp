/**************************************************************************/
/* test_reduce_dispatch_width.cpp                                         */
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

// DC0's dispatch-width coverage for Graph::add_reduce() and
// Graph::add_segmented_reduce(), in the style of test_prefix_scan.cpp's
// InternalNodeWidthsMatchTheDispatchTable.

#include <cstddef>
#include <cstdint>
#include <vector>

#include <gtest/gtest.h>

#include <SushiRuntime/SushiRuntime.h>

using namespace SushiRuntime;

// plan_reduction(600) at REDUCE_TILE = 256: level 0 folds 600 values into
// ceil(600 / 256) = 3 partials, level 1 folds those 3 into ceil(3 / 256) = 1.
// Each level is its own node, so the launch width per node is that level's
// out_count, in level order.
TEST(Integration_ReduceDispatchWidth, EagerReduceLevelWidthsMatchTheDispatchTable)
{
    constexpr std::size_t N = 600;
    const std::vector<std::uint32_t> expected_widths = {3u, 1u};

    auto rt     = Runtime::create();
    auto values = rt.buffer<float>(N);
    auto total  = rt.buffer<float>(1);
    for (std::size_t i = 0; i < N; ++i)
        values[i] = 1.0f;

    auto g = rt.graph();
    g.set_profiling_requested(true);
    g.add_reduce(values, total, N, Sum<float>{});

    const auto report = g.run();

    ASSERT_EQ(report.node_events.size(), expected_widths.size());
    for (std::size_t i = 0; i < expected_widths.size(); ++i)
    {
        EXPECT_EQ(report.node_events[i].launched_width, expected_widths[i]) << "level " << i;
        EXPECT_EQ(report.node_events[i].live_width, expected_widths[i]) << "level " << i;
    }
    EXPECT_FLOAT_EQ(total[0], static_cast<float>(N));
}

// n == 0 skips every level and emits one node that writes the identity; that
// node still has a real launch width of one, not WIDTH_UNKNOWN.
TEST(Integration_ReduceDispatchWidth, EagerReduceZeroCountStampsWidthOne)
{
    auto rt     = Runtime::create();
    auto values = rt.buffer<float>(1);
    auto total  = rt.buffer<float>(1);
    total[0] = -1.0f;

    auto g = rt.graph();
    g.set_profiling_requested(true);
    g.add_reduce(values, total, std::size_t{0}, Sum<float>{});

    const auto report = g.run();

    ASSERT_EQ(report.node_events.size(), 1u);
    EXPECT_EQ(report.node_events[0].launched_width, 1u);
    EXPECT_EQ(report.node_events[0].live_width, 1u);
    EXPECT_FLOAT_EQ(total[0], 0.0f);
}

// A LateCount left at its default (no host, no device) behaves like a fixed
// count equal to its capacity (detail::read_live_count()'s documented
// fallback), so add_segmented_reduce()'s late-bound overload emits its
// resolver (width one, a single_task) followed by its per-segment node
// (width capacity).
TEST(Integration_ReduceDispatchWidth, LateBoundSegmentedReduceWidthsMatchResolverAndCapacity)
{
    constexpr std::size_t SEGMENTS = 128;
    const std::vector<std::uint32_t> expected_widths = {1u, static_cast<std::uint32_t>(SEGMENTS)};

    auto rt      = Runtime::create();
    auto offsets = rt.buffer<std::uint32_t>(SEGMENTS + 1);
    for (std::size_t s = 0; s <= SEGMENTS; ++s)
        offsets[s] = static_cast<std::uint32_t>(s);
    auto values = rt.buffer<std::uint32_t>(SEGMENTS);
    for (std::size_t i = 0; i < SEGMENTS; ++i)
        values[i] = 1u;
    auto out = rt.buffer<std::uint32_t>(SEGMENTS);

    LateCount lc;
    lc.capacity = SEGMENTS;

    auto g = rt.graph();
    g.set_profiling_requested(true);
    g.add_segmented_reduce(values, offsets, out, lc, Sum<std::uint32_t>{});

    const auto report = g.run();

    ASSERT_EQ(report.node_events.size(), expected_widths.size());
    for (std::size_t i = 0; i < expected_widths.size(); ++i)
    {
        EXPECT_EQ(report.node_events[i].launched_width, expected_widths[i]) << "node " << i;
        EXPECT_EQ(report.node_events[i].live_width, expected_widths[i]) << "node " << i;
    }
    for (std::size_t s = 0; s < SEGMENTS; ++s)
        EXPECT_EQ(out[s], 1u) << "segment " << s;
}

// The fixed-count overload emits one node over exactly segment_count
// elements, so its launch width is segment_count itself.
TEST(Integration_ReduceDispatchWidth, EagerSegmentedReduceWidthMatchesSegmentCount)
{
    constexpr std::size_t SEGMENTS = 64;

    auto rt      = Runtime::create();
    auto offsets = rt.buffer<std::uint32_t>(SEGMENTS + 1);
    for (std::size_t s = 0; s <= SEGMENTS; ++s)
        offsets[s] = static_cast<std::uint32_t>(s);
    auto values = rt.buffer<std::uint32_t>(SEGMENTS);
    for (std::size_t i = 0; i < SEGMENTS; ++i)
        values[i] = 1u;
    auto out = rt.buffer<std::uint32_t>(SEGMENTS);

    auto g = rt.graph();
    g.set_profiling_requested(true);
    g.add_segmented_reduce(values, offsets, out, SEGMENTS, Sum<std::uint32_t>{});

    const auto report = g.run();

    ASSERT_EQ(report.node_events.size(), 1u);
    EXPECT_EQ(report.node_events[0].launched_width, static_cast<std::uint32_t>(SEGMENTS));
    EXPECT_EQ(report.node_events[0].live_width, static_cast<std::uint32_t>(SEGMENTS));
    for (std::size_t s = 0; s < SEGMENTS; ++s)
        EXPECT_EQ(out[s], 1u) << "segment " << s;
}
