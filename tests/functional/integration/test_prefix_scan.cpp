/****************************************************************/
/* test_prefix_scan.cpp                                         */
/* SushiRuntime - https://github.com/SushiSystems/SushiRuntime  */
/* Copyright (c) 2026 Sushi Systems                             */
/* Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.    */
/* Commercial use requires a licence from Sushi Systems.        */
/****************************************************************/

/**
 * @file test_prefix_scan.cpp
 * @brief Tests add_prefix_scan() against a host reference, with late-bound replay and indexing.
 * @see tests/functional/integration/README.md
 * @author Mustafa Garip
 */

#include <cstddef>
#include <cstdint>
#include <vector>

#include <gtest/gtest.h>

#include <SushiRuntime/SushiRuntime.h>

using namespace SushiRuntime;

namespace
{
    /**
     * @brief Computes the exclusive prefix sum over uint32, mod 2^32, and returns the grand sum.
     */
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
    /** @brief Holds a sentinel that no real scan produces. */
    constexpr std::uint32_t CANARY = 0xDEADBEEFu;
}

/** @brief Tests exact agreement with the host reference at the tile edges. */
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

/** @brief Tests that offsets[n] equals the total add_prefix_scan() publishes. */
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

/** @brief Tests that a live count that changes between runs keeps one compile. */
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

/** @brief Tests that each run scans the counts in the buffer at the time it runs. */
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

/** @brief Tests that every internal pass of one add_prefix_scan() call shares one node_index. */
TEST(Integration_PrefixScan, SharesOneNodeIndexAcrossStepsAndPasses)
{
    // A capacity of 600 gives two levels, so this call emits 5 nodes; see README.md.
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

/** @brief Tests that each internal pass's launched_width is its own row of the width table. */
TEST(Integration_PrefixScan, InternalNodeWidthsMatchTheDispatchTable)
{
    // The node order is resolver, up0, down-top, down0, closing; see README.md.
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

/**
 * @brief Tests that a capacity of zero writes a zero total and a zero offsets[0] over the canary.
 */
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

/** @brief Tests that a live count above its capacity fails the run. */
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
