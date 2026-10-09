/**************************************************************************/
/* test_reduce_dispatch_width.cpp                                         */
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
 * @file test_reduce_dispatch_width.cpp
 * @brief Tests the dispatch widths that add_reduce() and add_segmented_reduce() stamp.
 * @see tests/integration/README.md
 * @author Mustafa Garip
 */

#include <cstddef>
#include <cstdint>
#include <vector>

#include <gtest/gtest.h>

#include <SushiRuntime/SushiRuntime.h>

using namespace SushiRuntime;

/**
 * @brief Tests that each level node of an eager reduce over 600 values launches at that level's
 *        out_count.
 */
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

/** @brief Tests that a zero-count eager reduce stamps a width of one. */
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

/**
 * @brief Tests that a late-bound segmented reduce emits a resolver of width one and a node of width
 *        capacity.
 */
TEST(Integration_ReduceDispatchWidth, LateBoundSegmentedReduceWidthsMatchResolverAndCapacity)
{
    constexpr std::size_t SEGMENTS = 128;
    const std::vector<std::uint32_t> expected_widths = {1u, static_cast<std::uint32_t>(SEGMENTS)};

    // segment_offsets and output are oversized differently to expose a buffer width; see README.md.
    auto rt      = Runtime::create();
    auto offsets = rt.buffer<std::uint32_t>(SEGMENTS + 9);
    for (std::size_t s = 0; s <= SEGMENTS; ++s)
        offsets[s] = static_cast<std::uint32_t>(s);
    auto values = rt.buffer<std::uint32_t>(SEGMENTS);
    for (std::size_t i = 0; i < SEGMENTS; ++i)
        values[i] = 1u;
    auto out = rt.buffer<std::uint32_t>(SEGMENTS + 8);

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

/** @brief Tests that the fixed-count segmented reduce launches at segment_count. */
TEST(Integration_ReduceDispatchWidth, EagerSegmentedReduceWidthMatchesSegmentCount)
{
    constexpr std::size_t SEGMENTS = 64;

    // segment_offsets and output carry distinct padding past segment_count; see README.md.
    auto rt      = Runtime::create();
    auto offsets = rt.buffer<std::uint32_t>(SEGMENTS + 9);
    for (std::size_t s = 0; s <= SEGMENTS; ++s)
        offsets[s] = static_cast<std::uint32_t>(s);
    auto values = rt.buffer<std::uint32_t>(SEGMENTS);
    for (std::size_t i = 0; i < SEGMENTS; ++i)
        values[i] = 1u;
    auto out = rt.buffer<std::uint32_t>(SEGMENTS + 8);

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
