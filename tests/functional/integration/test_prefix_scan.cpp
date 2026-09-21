/**************************************************************************/
/* test_prefix_scan.cpp                                                   */
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

// Wave 2 task 3 coverage: Graph::add_prefix_scan() (spec
// 2026-09-16-scan-and-counting-sort-design.md §A1, §B; plan task 3). Checks
// exact agreement with a host reference at the tile edges, the exclusive
// contract (offsets[n] == total), late-bound replay with one compile, and
// that every internal pass of one call shares one node_index across steps.

#include <cstddef>
#include <cstdint>
#include <vector>

#include <gtest/gtest.h>

#include <SushiRuntime/SushiRuntime.h>

using namespace SushiRuntime;

namespace
{
    // Exclusive prefix sum over uint32, mod 2^32, the scan's own contract:
    // offsets[i] is the sum of counts[0..i), offsets[n] and the return value
    // both carry the grand sum.
    std::uint32_t host_exclusive_scan(const std::vector<std::uint32_t>& counts,
                                       std::size_t n, std::vector<std::uint32_t>& offsets)
    {
        std::uint32_t acc = 0;
        for (std::size_t i = 0; i < n; ++i)
        {
            offsets[i] = acc;
            acc += counts[i];
        }
        offsets[n] = acc;
        return acc;
    }

    std::vector<std::uint32_t> deterministic_counts(std::size_t n)
    {
        std::vector<std::uint32_t> v(n);
        for (std::size_t i = 0; i < n; ++i)
            v[i] = static_cast<std::uint32_t>((i % 13) + 1);
        return v;
    }
}

namespace
{
    // Sentinel that would never arise from a real scan, so a slot that keeps
    // it was not written and a slot that loses it was written when it must
    // not have been.
    constexpr std::uint32_t CANARY = 0xDEADBEEFu;
}

// The tile edges (0, 1, one below/at/above a tile, one below/at/above the
// second level, and 10^6) are where an off-by-one in the level plan or the
// down-sweep's base would show up. offsets and total are filled with a
// canary before the run, and offsets carries one extra slot past
// capacity + 1 that must survive untouched, so neither a stale zero nor an
// out-of-bounds write past the declared window can pass by accident.
TEST(Integration_PrefixScan, ExactAgreementWithHostReferenceAtTileEdges)
{
    const std::vector<std::size_t> sizes = {0, 1, 255, 256, 257, 65536, 65537, 1000000};

    for (std::size_t n : sizes)
    {
        auto rt      = Runtime::create();
        auto counts  = rt.buffer<std::uint32_t>(n == 0 ? 1 : n);
        auto offsets = rt.buffer<std::uint32_t>(n + 2);
        auto total   = rt.buffer<std::uint32_t>(1);

        std::vector<std::uint32_t> host_counts = deterministic_counts(n);
        for (std::size_t i = 0; i < n; ++i)
            counts[i] = host_counts[i];
        for (std::size_t i = 0; i < offsets.size(); ++i)
            offsets[i] = CANARY;
        total[0] = CANARY;

        std::vector<std::uint32_t> expected_offsets(n + 1, 0);
        const std::uint32_t expected_total = host_exclusive_scan(host_counts, n, expected_offsets);

        LateCount lc;
        lc.capacity = n;

        auto g = rt.graph();
        g.add_prefix_scan(counts, offsets, total, lc);
        g.run();

        EXPECT_EQ(total[0], expected_total) << "n = " << n;
        for (std::size_t i = 0; i <= n; ++i)
            ASSERT_EQ(offsets[i], expected_offsets[i]) << "n = " << n << ", i = " << i;
        EXPECT_EQ(offsets[n + 1], CANARY)
            << "n = " << n << ": an out-of-bounds write reached past the declared window";
    }
}

// The exclusive contract's second half: offsets[n] is not merely close to the
// sum, it IS the sum add_prefix_scan() also publishes through total.
TEST(Integration_PrefixScan, OffsetsAtLiveCountEqualsTotal)
{
    constexpr std::size_t N = 257;

    auto rt      = Runtime::create();
    auto counts  = rt.buffer<std::uint32_t>(N);
    auto offsets = rt.buffer<std::uint32_t>(N + 1);
    auto total   = rt.buffer<std::uint32_t>(1);
    for (std::size_t i = 0; i < N; ++i)
        counts[i] = static_cast<std::uint32_t>(i + 1);

    LateCount lc;
    lc.capacity = N;

    auto g = rt.graph();
    g.add_prefix_scan(counts, offsets, total, lc);
    g.run();

    EXPECT_EQ(offsets[N], total[0]);
}

// A live count that changes between runs must not force a recompile: the
// topology (levels, scratch) is fixed at the capacity, only the live count
// and the counts it scans vary per run.
TEST(Integration_PrefixScan, LateBoundChangingCountKeepsOneCompile)
{
    constexpr std::size_t CAPACITY = 1000003;

    auto rt      = Runtime::create();
    auto counts  = rt.buffer<std::uint32_t>(CAPACITY);
    auto offsets = rt.buffer<std::uint32_t>(CAPACITY + 1);
    auto total   = rt.buffer<std::uint32_t>(1);
    for (std::size_t i = 0; i < CAPACITY; ++i)
        counts[i] = 1;

    std::size_t live_n = 1000;
    LateCount lc;
    lc.capacity = CAPACITY;
    lc.host = [&live_n]() { return live_n; };

    auto g = rt.graph();
    g.add_prefix_scan(counts, offsets, total, lc);

    g.run();
    EXPECT_EQ(total[0], 1000u);
    EXPECT_EQ(offsets[1000], 1000u);

    live_n = 700000;
    g.run();
    EXPECT_EQ(total[0], 700000u);
    EXPECT_EQ(offsets[700000], 700000u);

    live_n = 3;
    g.run();
    EXPECT_EQ(total[0], 3u);
    EXPECT_EQ(offsets[3], 3u);

    EXPECT_EQ(g.compile_count(), 1u)
        << "a live count change between runs must not trigger a recompile";
}

// A run whose counts genuinely change (not just the live count) between calls
// must scan whatever is in the buffer at the time it runs, not a stale copy.
TEST(Integration_PrefixScan, EachRunScansItsOwnCounts)
{
    constexpr std::size_t CAPACITY = 300;

    auto rt      = Runtime::create();
    auto counts  = rt.buffer<std::uint32_t>(CAPACITY);
    auto offsets = rt.buffer<std::uint32_t>(CAPACITY + 1);
    auto total   = rt.buffer<std::uint32_t>(1);

    LateCount lc;
    lc.capacity = CAPACITY;

    auto g = rt.graph();
    g.add_prefix_scan(counts, offsets, total, lc);

    for (std::uint32_t step = 0; step < 4; ++step)
    {
        std::vector<std::uint32_t> host_counts(CAPACITY);
        for (std::size_t i = 0; i < CAPACITY; ++i)
            host_counts[i] = static_cast<std::uint32_t>((i + step) % 5);
        for (std::size_t i = 0; i < CAPACITY; ++i)
            counts[i] = host_counts[i];

        std::vector<std::uint32_t> expected_offsets(CAPACITY + 1, 0);
        const std::uint32_t expected_total =
            host_exclusive_scan(host_counts, CAPACITY, expected_offsets);

        g.run();

        EXPECT_EQ(total[0], expected_total) << "step " << step;
        for (std::size_t i = 0; i <= CAPACITY; ++i)
            ASSERT_EQ(offsets[i], expected_offsets[i]) << "step " << step << ", i = " << i;
    }

    EXPECT_EQ(g.compile_count(), 1u);
}

// G1 coverage: every internal pass one add_prefix_scan() call emits (the
// late-count resolver, the up-sweep levels, the down-sweep levels and the
// closing write) shares one node_index, and run(steps) records one
// invocation per step for each of them.
TEST(Integration_PrefixScan, SharesOneNodeIndexAcrossStepsAndPasses)
{
    // plan_tiled_levels(600): level0 600 -> 3, level1 3 -> 1; two levels, so
    // this call emits 1 (resolver) + 1 (up, all but the top level) +
    // 2 (down, every level) + 1 (closing) = 5 nodes.
    constexpr std::size_t CAPACITY = 600;
    constexpr std::size_t EXPECTED_PASSES = 5;
    constexpr std::size_t STEPS = 4;

    auto rt      = Runtime::create();
    auto counts  = rt.buffer<std::uint32_t>(CAPACITY);
    auto offsets = rt.buffer<std::uint32_t>(CAPACITY + 1);
    auto total   = rt.buffer<std::uint32_t>(1);
    for (std::size_t i = 0; i < CAPACITY; ++i)
        counts[i] = static_cast<std::uint32_t>((i % 7) + 1);

    LateCount lc;
    lc.capacity = CAPACITY;

    auto g = rt.graph();
    g.set_profiling_requested(true);
    g.add_prefix_scan(counts, offsets, total, lc);

    auto report = g.run(STEPS);

    ASSERT_EQ(report.node_events.size(), EXPECTED_PASSES * STEPS);

    const std::uint32_t node_index = report.node_events.front().node_index;
    std::vector<std::size_t> per_invocation(STEPS, 0);
    for (const auto& record : report.node_events)
    {
        EXPECT_EQ(record.node_index, node_index)
            << "every pass of one add_prefix_scan() call must share one node_index";
        ASSERT_LT(record.invocation, STEPS);
        ++per_invocation[record.invocation];
    }
    for (std::size_t step = 0; step < STEPS; ++step)
        EXPECT_EQ(per_invocation[step], EXPECTED_PASSES) << "step " << step;
}

// Device-solver-instrument coverage: each internal pass's launched_width must
// be its own row of the width table, not merely "some width was stamped".
// Every pass here reads a region the one before it wrote (state_region, then
// up_scratch, then down_scratch, then the whole-buffer WAW on offsets), so
// report.node_events lands in that same dependency order regardless of which
// execution path ran it.
TEST(Integration_PrefixScan, InternalNodeWidthsMatchTheDispatchTable)
{
    // Same shape as SharesOneNodeIndexAcrossStepsAndPasses: plan_tiled_levels(600)
    // gives level0 600 -> 3, level1 3 -> 1; order is resolver, up0, down-top,
    // down0, closing.
    constexpr std::size_t CAPACITY = 600;
    const std::vector<std::uint32_t> expected_widths = {1u, 3u, 1u, 3u, 1u};

    auto rt      = Runtime::create();
    auto counts  = rt.buffer<std::uint32_t>(CAPACITY);
    auto offsets = rt.buffer<std::uint32_t>(CAPACITY + 1);
    auto total   = rt.buffer<std::uint32_t>(1);
    for (std::size_t i = 0; i < CAPACITY; ++i)
        counts[i] = static_cast<std::uint32_t>((i % 7) + 1);

    LateCount lc;
    lc.capacity = CAPACITY;

    auto g = rt.graph();
    g.set_profiling_requested(true);
    g.add_prefix_scan(counts, offsets, total, lc);

    auto report = g.run();

    ASSERT_EQ(report.node_events.size(), expected_widths.size());
    for (std::size_t i = 0; i < expected_widths.size(); ++i)
    {
        EXPECT_EQ(report.node_events[i].launched_width, expected_widths[i]) << "pass " << i;
        EXPECT_EQ(report.node_events[i].live_width, expected_widths[i]) << "pass " << i;
    }
}

// Capacity zero: the live count is always zero, so total and offsets[0] are
// always zero, and the buffers start from a canary rather than a lucky zero.
TEST(Integration_PrefixScan, ZeroCapacityWritesZeroTotal)
{
    auto rt      = Runtime::create();
    auto counts  = rt.buffer<std::uint32_t>(1);
    auto offsets = rt.buffer<std::uint32_t>(1);
    auto total   = rt.buffer<std::uint32_t>(1);
    offsets[0] = CANARY;
    total[0] = CANARY;

    auto g = rt.graph();
    g.add_prefix_scan(counts, offsets, total, LateCount{});
    g.run();

    EXPECT_EQ(offsets[0], 0u);
    EXPECT_EQ(total[0], 0u);
}

// A live count above its capacity fails the run rather than reading or
// writing past the buffers it was planned for.
TEST(Integration_PrefixScan, LateBoundAboveCapacityFailsTheRun)
{
    auto rt      = Runtime::create();
    auto counts  = rt.buffer<std::uint32_t>(100);
    auto offsets = rt.buffer<std::uint32_t>(101);
    auto total   = rt.buffer<std::uint32_t>(1);

    LateCount lc;
    lc.capacity = 100;
    lc.host = []() { return std::size_t{101}; };

    auto g = rt.graph();
    g.add_prefix_scan(counts, offsets, total, lc);
    EXPECT_THROW(g.run(), SushiException);
}
