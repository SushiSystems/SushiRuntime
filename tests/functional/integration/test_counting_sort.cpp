/**************************************************************************/
/* test_counting_sort.cpp                                                 */
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

// Wave 3 task 4 coverage: Graph::add_counting_sort() (spec
// 2026-09-16-scan-and-counting-sort-design.md §A3, §B; plan task 4). Checks
// stability against std::stable_sort, the digit-count boundaries a live key
// range crosses, late-bound replay with one compile, and that every internal
// pass of one call shares one node_index across steps.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <numeric>
#include <random>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include <SushiRuntime/SushiRuntime.h>

using namespace SushiRuntime;

namespace
{
    constexpr std::uint32_t CANARY = 0xDEADBEEFu;

    // The host reference for every case below: a stable sort of (key, index)
    // by key alone, plus the bucket offsets/counts that fall out of it. This
    // is the ground truth add_counting_sort() is checked against, not a copy
    // of its algorithm.
    struct HostReference
    {
        std::vector<std::uint32_t> indices;
        std::vector<std::uint32_t> offsets; // key_range + 1 entries
        std::vector<std::uint32_t> counts;  // key_range entries
    };

    HostReference host_counting_sort(const std::vector<std::uint32_t>& keys,
                                      std::size_t key_range)
    {
        HostReference ref;
        ref.indices.resize(keys.size());
        std::iota(ref.indices.begin(), ref.indices.end(), std::uint32_t{0});
        std::stable_sort(ref.indices.begin(), ref.indices.end(),
                          [&keys](std::uint32_t a, std::uint32_t b)
                          { return keys[a] < keys[b]; });

        ref.counts.assign(key_range, 0);
        for (std::uint32_t k : keys)
            ++ref.counts[k];

        ref.offsets.assign(key_range + 1, 0);
        std::uint32_t acc = 0;
        for (std::size_t k = 0; k < key_range; ++k)
        {
            ref.offsets[k] = acc;
            acc += ref.counts[k];
        }
        ref.offsets[key_range] = acc;
        return ref;
    }

    LateCount fixed(std::size_t capacity)
    {
        LateCount lc;
        lc.capacity = capacity;
        return lc;
    }
}

// Stability against std::stable_sort on seeded duplicate keys, with a
// std::sort-based comparison proving the case is not vacuous: a plain
// unstable sort must be able to disagree with the stable reference on this
// same input, or the test would pass whether or not add_counting_sort() were
// stable at all.
TEST(Integration_CountingSort, StableAgainstHostReferenceOnDuplicateKeys)
{
    constexpr std::size_t N = 4000;
    constexpr std::size_t KEY_RANGE = 32;

    std::mt19937 rng(20260916);
    std::uniform_int_distribution<std::uint32_t> dist(0, KEY_RANGE - 1);
    std::vector<std::uint32_t> host_keys(N);
    for (auto& k : host_keys)
        k = dist(rng);

    std::vector<std::uint32_t> unstable_order(N);
    std::iota(unstable_order.begin(), unstable_order.end(), std::uint32_t{0});
    std::sort(unstable_order.begin(), unstable_order.end(),
              [&host_keys](std::uint32_t a, std::uint32_t b)
              { return host_keys[a] < host_keys[b]; });
    const HostReference ref = host_counting_sort(host_keys, KEY_RANGE);
    ASSERT_NE(unstable_order, ref.indices)
        << "the input must exercise stability, or this test proves nothing";

    auto rt      = Runtime::create();
    auto keys    = rt.buffer<std::uint32_t>(N);
    auto indices = rt.buffer<std::uint32_t>(N + 1);
    auto offsets = rt.buffer<std::uint32_t>(KEY_RANGE + 2);
    auto total   = rt.buffer<std::uint32_t>(1);
    auto counts  = rt.buffer<std::uint32_t>(KEY_RANGE + 1);
    for (std::size_t i = 0; i < N; ++i)
        keys[i] = host_keys[i];
    indices[N] = CANARY;
    offsets[KEY_RANGE + 1] = CANARY;
    counts[KEY_RANGE] = CANARY;

    auto g = rt.graph();
    g.add_counting_sort(keys, indices, offsets, total, fixed(N), fixed(KEY_RANGE), &counts);
    g.run();

    EXPECT_EQ(total[0], N);
    for (std::size_t i = 0; i < N; ++i)
        ASSERT_EQ(indices[i], ref.indices[i]) << "i = " << i;
    for (std::size_t k = 0; k <= KEY_RANGE; ++k)
        ASSERT_EQ(offsets[k], ref.offsets[k]) << "k = " << k;
    for (std::size_t k = 0; k < KEY_RANGE; ++k)
        ASSERT_EQ(counts[k], ref.counts[k]) << "k = " << k;
    EXPECT_EQ(indices[N], CANARY);
    EXPECT_EQ(offsets[KEY_RANGE + 1], CANARY);
    EXPECT_EQ(counts[KEY_RANGE], CANARY);
}

// Multi-digit stability: every key shares the same low byte, so the first
// (least significant) digit pass ties every element, and only the second
// digit pass tells them apart. Same non-vacuous std::sort check as the
// single-digit case, so the tie itself is proven to matter.
TEST(Integration_CountingSort, StableAcrossMultipleDigitsWhenTheLowDigitTies)
{
    constexpr std::size_t N = 4000;
    constexpr std::size_t KEY_CAPACITY = 1u << 16;
    const std::vector<std::uint32_t> alphabet = {0x0100u, 0x0200u, 0x0300u, 0x0400u};

    std::mt19937 rng(20260917);
    std::uniform_int_distribution<std::size_t> pick(0, alphabet.size() - 1);
    std::vector<std::uint32_t> host_keys(N);
    for (auto& k : host_keys)
        k = alphabet[pick(rng)];

    std::vector<std::uint32_t> unstable_order(N);
    std::iota(unstable_order.begin(), unstable_order.end(), std::uint32_t{0});
    std::sort(unstable_order.begin(), unstable_order.end(),
              [&host_keys](std::uint32_t a, std::uint32_t b)
              { return host_keys[a] < host_keys[b]; });
    const HostReference ref = host_counting_sort(host_keys, KEY_CAPACITY);
    ASSERT_NE(unstable_order, ref.indices)
        << "the input must exercise stability, or this test proves nothing";

    auto rt      = Runtime::create();
    auto keys    = rt.buffer<std::uint32_t>(N);
    auto indices = rt.buffer<std::uint32_t>(N);
    auto offsets = rt.buffer<std::uint32_t>(KEY_CAPACITY + 1);
    auto total   = rt.buffer<std::uint32_t>(1);
    for (std::size_t i = 0; i < N; ++i)
        keys[i] = host_keys[i];

    auto g = rt.graph();
    g.add_counting_sort(keys, indices, offsets, total, fixed(N), fixed(KEY_CAPACITY));
    g.run();

    EXPECT_EQ(total[0], N);
    for (std::size_t i = 0; i < N; ++i)
        ASSERT_EQ(indices[i], ref.indices[i]) << "i = " << i;
    for (const std::uint32_t k : alphabet)
        ASSERT_EQ(offsets[k], ref.offsets[k]) << "k = " << k;
}

// Large case, seeded random: n = 10^6 with a 2^20 key range needs more than
// 256 tiles (10^6 / 256 ~= 3907) and three digit passes, the first size and
// range this suite exercises both boundaries at once.
TEST(Integration_CountingSort, LargeRandomCaseAgainstHostReference)
{
    constexpr std::size_t N = 1000000;
    constexpr std::size_t KEY_RANGE = 1u << 20;

    std::mt19937 rng(20260918);
    std::uniform_int_distribution<std::uint32_t> dist(0, KEY_RANGE - 1);
    std::vector<std::uint32_t> host_keys(N);
    for (auto& k : host_keys)
        k = dist(rng);

    auto rt      = Runtime::create();
    auto keys    = rt.buffer<std::uint32_t>(N);
    auto indices = rt.buffer<std::uint32_t>(N);
    auto offsets = rt.buffer<std::uint32_t>(KEY_RANGE + 1);
    auto total   = rt.buffer<std::uint32_t>(1);
    for (std::size_t i = 0; i < N; ++i)
        keys[i] = host_keys[i];

    const HostReference ref = host_counting_sort(host_keys, KEY_RANGE);

    auto g = rt.graph();
    g.add_counting_sort(keys, indices, offsets, total, fixed(N), fixed(KEY_RANGE));
    g.run();

    EXPECT_EQ(total[0], N);
    for (std::size_t i = 0; i < N; ++i)
        ASSERT_EQ(indices[i], ref.indices[i]) << "i = " << i;
    for (std::size_t k = 0; k <= KEY_RANGE; ++k)
        ASSERT_EQ(offsets[k], ref.offsets[k]) << "k = " << k;
}

// All keys equal: every element ties, so the stable order is exactly iota.
TEST(Integration_CountingSort, AllKeysEqualKeepsInputOrder)
{
    constexpr std::size_t N = 777;
    constexpr std::size_t KEY_RANGE = 4;

    auto rt      = Runtime::create();
    auto keys    = rt.buffer<std::uint32_t>(N);
    auto indices = rt.buffer<std::uint32_t>(N);
    auto offsets = rt.buffer<std::uint32_t>(KEY_RANGE + 1);
    auto total   = rt.buffer<std::uint32_t>(1);
    for (std::size_t i = 0; i < N; ++i)
        keys[i] = 2;

    auto g = rt.graph();
    g.add_counting_sort(keys, indices, offsets, total, fixed(N), fixed(KEY_RANGE));
    g.run();

    for (std::size_t i = 0; i < N; ++i)
        ASSERT_EQ(indices[i], i) << "i = " << i;
    EXPECT_EQ(offsets[2], 0u);
    EXPECT_EQ(offsets[3], N);
    EXPECT_EQ(total[0], N);
}

// One element: the smallest non-empty case, still exercises every pass.
TEST(Integration_CountingSort, OneElement)
{
    auto rt      = Runtime::create();
    auto keys    = rt.buffer<std::uint32_t>(1);
    auto indices = rt.buffer<std::uint32_t>(1);
    auto offsets = rt.buffer<std::uint32_t>(9);
    auto total   = rt.buffer<std::uint32_t>(1);
    keys[0] = 5;

    auto g = rt.graph();
    g.add_counting_sort(keys, indices, offsets, total, fixed(1), fixed(8));
    g.run();

    EXPECT_EQ(indices[0], 0u);
    EXPECT_EQ(offsets[5], 0u);
    EXPECT_EQ(offsets[6], 1u);
    EXPECT_EQ(total[0], 1u);
}

// Zero elements: every bucket starts and stays at the live count, which is
// zero, and nothing beyond the declared window is touched.
TEST(Integration_CountingSort, ZeroElementsWritesZeroEverywhere)
{
    constexpr std::size_t KEY_RANGE = 16;

    auto rt      = Runtime::create();
    auto keys    = rt.buffer<std::uint32_t>(1);
    auto indices = rt.buffer<std::uint32_t>(1);
    auto offsets = rt.buffer<std::uint32_t>(KEY_RANGE + 2);
    auto total   = rt.buffer<std::uint32_t>(1);
    auto counts  = rt.buffer<std::uint32_t>(KEY_RANGE + 1);
    offsets[KEY_RANGE + 1] = CANARY;
    counts[KEY_RANGE] = CANARY;

    auto g = rt.graph();
    g.add_counting_sort(keys, indices, offsets, total, fixed(0), fixed(KEY_RANGE), &counts);
    g.run();

    EXPECT_EQ(total[0], 0u);
    for (std::size_t k = 0; k <= KEY_RANGE; ++k)
        EXPECT_EQ(offsets[k], 0u) << "k = " << k;
    for (std::size_t k = 0; k < KEY_RANGE; ++k)
        EXPECT_EQ(counts[k], 0u) << "k = " << k;
    EXPECT_EQ(offsets[KEY_RANGE + 1], CANARY);
    EXPECT_EQ(counts[KEY_RANGE], CANARY);
}

// The maximum key of a 2^20 range: catches a digit-count or mask error at
// the top bit, since key_range.capacity - 1 sets bit 19 exactly.
TEST(Integration_CountingSort, MaximumKeyOfA20BitRange)
{
    constexpr std::size_t KEY_RANGE = 1u << 20;
    constexpr std::size_t N = 5;

    auto rt      = Runtime::create();
    auto keys    = rt.buffer<std::uint32_t>(N);
    auto indices = rt.buffer<std::uint32_t>(N);
    auto offsets = rt.buffer<std::uint32_t>(KEY_RANGE + 1);
    auto total   = rt.buffer<std::uint32_t>(1);
    keys[0] = static_cast<std::uint32_t>(KEY_RANGE - 1);
    keys[1] = 0;
    keys[2] = static_cast<std::uint32_t>(KEY_RANGE - 1);
    keys[3] = 1;
    keys[4] = 0;

    const std::vector<std::uint32_t> host_keys = {keys[0], keys[1], keys[2], keys[3], keys[4]};

    auto g = rt.graph();
    g.add_counting_sort(keys, indices, offsets, total, fixed(N), fixed(KEY_RANGE));
    g.run();

    const HostReference ref = host_counting_sort(host_keys, KEY_RANGE);
    for (std::size_t i = 0; i < N; ++i)
        ASSERT_EQ(indices[i], ref.indices[i]) << "i = " << i;
    EXPECT_EQ(offsets[KEY_RANGE - 1], 3u);
    EXPECT_EQ(offsets[KEY_RANGE], 5u);
}

// A live key range at or below 2^16 must take two digit passes, one above it
// three: each digit block is histogram + column scan + bin base + placement,
// so the node count this call emits grows by exactly four per extra digit.
TEST(Integration_CountingSort, DigitCountFollowsKeyRangeCapacity)
{
    auto passes_for = [](std::size_t key_range_capacity) -> std::size_t
    {
        constexpr std::size_t N = 10;
        auto rt      = Runtime::create();
        auto keys    = rt.buffer<std::uint32_t>(N);
        auto indices = rt.buffer<std::uint32_t>(N);
        auto offsets = rt.buffer<std::uint32_t>(key_range_capacity + 1);
        auto total   = rt.buffer<std::uint32_t>(1);
        for (std::size_t i = 0; i < N; ++i)
            keys[i] = 0;

        auto g = rt.graph();
        g.set_profiling_requested(true);
        g.add_counting_sort(keys, indices, offsets, total, fixed(N), fixed(key_range_capacity));
        auto report = g.run();
        return report.node_events.size();
    };

    const std::size_t passes_16 = passes_for(1u << 16);
    const std::size_t passes_20 = passes_for(1u << 20);
    EXPECT_EQ(passes_20 - passes_16, 4u * (3u - 2u))
        << "2^20 needs three digit passes, 2^16 needs two";
}

// A late-bound element count and key range that both change between runs
// must not force a recompile: capacity fixes the topology, only the live
// values and the buffers they describe vary.
TEST(Integration_CountingSort, LateBoundChangingCountAndKeyRangeKeepsOneCompile)
{
    constexpr std::size_t N_CAPACITY = 2000;
    constexpr std::size_t KEY_CAPACITY = 1u << 16;

    auto rt      = Runtime::create();
    auto keys    = rt.buffer<std::uint32_t>(N_CAPACITY);
    auto indices = rt.buffer<std::uint32_t>(N_CAPACITY);
    auto offsets = rt.buffer<std::uint32_t>(KEY_CAPACITY + 1);
    auto total   = rt.buffer<std::uint32_t>(1);

    std::size_t live_n = 10;
    std::size_t live_key_range = 4;
    LateCount n_lc;
    n_lc.capacity = N_CAPACITY;
    n_lc.host = [&live_n]() { return live_n; };
    LateCount kr_lc;
    kr_lc.capacity = KEY_CAPACITY;
    kr_lc.host = [&live_key_range]() { return live_key_range; };

    auto g = rt.graph();
    g.add_counting_sort(keys, indices, offsets, total, n_lc, kr_lc);

    auto run_and_check = [&](std::size_t n, std::size_t key_range)
    {
        live_n = n;
        live_key_range = key_range;
        std::mt19937 rng(static_cast<unsigned>(n * 31 + key_range));
        std::uniform_int_distribution<std::uint32_t> dist(
            0, static_cast<std::uint32_t>(key_range - 1));
        std::vector<std::uint32_t> host_keys(n);
        for (auto& k : host_keys)
            k = dist(rng);
        for (std::size_t i = 0; i < n; ++i)
            keys[i] = host_keys[i];

        g.run();

        const HostReference ref = host_counting_sort(host_keys, key_range);
        for (std::size_t i = 0; i < n; ++i)
            ASSERT_EQ(indices[i], ref.indices[i])
                << "n = " << n << ", key_range = " << key_range << ", i = " << i;
        EXPECT_EQ(total[0], n);
        for (std::size_t k = 0; k <= key_range; ++k)
            ASSERT_EQ(offsets[k], ref.offsets[k])
                << "n = " << n << ", key_range = " << key_range << ", k = " << k;
    };

    run_and_check(10, 4);
    run_and_check(1500, 60000);
    run_and_check(3, 2);

    EXPECT_EQ(g.compile_count(), 1u)
        << "a live count or key range change between runs must not trigger a recompile";
}

// A key range capacity of 2^24 plans three digits; live key ranges needing
// one, two and three digits must each leave the stable order in the indices,
// whichever digit turns out to be the last one that runs.
TEST(Integration_CountingSort, LiveKeyRangeBelowCapacityLandsInIndices)
{
    constexpr std::size_t N = 3000;
    constexpr std::size_t KEY_CAPACITY = 1u << 24;

    auto rt      = Runtime::create();
    auto keys    = rt.buffer<std::uint32_t>(N);
    auto indices = rt.buffer<std::uint32_t>(N);
    auto offsets = rt.buffer<std::uint32_t>(KEY_CAPACITY + 1);
    auto total   = rt.buffer<std::uint32_t>(1);

    std::size_t live_key_range = 0;
    LateCount kr_lc;
    kr_lc.capacity = KEY_CAPACITY;
    kr_lc.host = [&live_key_range]() { return live_key_range; };

    auto g = rt.graph();
    g.add_counting_sort(keys, indices, offsets, total, fixed(N), kr_lc);

    for (std::size_t key_range : {std::size_t{200}, std::size_t{40000}, std::size_t{5000000},
                                  std::size_t{3}})
    {
        live_key_range = key_range;
        std::mt19937 rng(static_cast<unsigned>(key_range));
        std::uniform_int_distribution<std::uint32_t> dist(
            0, static_cast<std::uint32_t>(key_range - 1));
        std::vector<std::uint32_t> host_keys(N);
        for (auto& k : host_keys)
            k = dist(rng);
        for (std::size_t i = 0; i < N; ++i)
            keys[i] = host_keys[i];

        g.run();

        const HostReference ref = host_counting_sort(host_keys, key_range);
        for (std::size_t i = 0; i < N; ++i)
            ASSERT_EQ(indices[i], ref.indices[i]) << "key_range = " << key_range << ", i = " << i;
        for (std::size_t k = 0; k <= key_range; ++k)
            ASSERT_EQ(offsets[k], ref.offsets[k]) << "key_range = " << key_range << ", k = " << k;
    }
    EXPECT_EQ(g.compile_count(), 1u);
}

// The upper digits really are skipped: a key that breaks the live key range's
// precondition but fits the capacity is sorted by its low byte alone, which it
// could not be if the digit above still ran. White-box by design; a caller must
// never pass such a key.
TEST(Integration_CountingSort, DigitsAboveTheLiveKeyRangeDoNotRun)
{
    constexpr std::size_t N = 4;
    constexpr std::size_t KEY_CAPACITY = 1u << 16;

    auto rt      = Runtime::create();
    auto keys    = rt.buffer<std::uint32_t>(N);
    auto indices = rt.buffer<std::uint32_t>(N);
    auto offsets = rt.buffer<std::uint32_t>(KEY_CAPACITY + 1);
    auto total   = rt.buffer<std::uint32_t>(1);

    // 0x0101 has low byte 1, so one digit puts it between 0 and 2; two digits
    // would put it last.
    keys[0] = 0x0101;
    keys[1] = 2;
    keys[2] = 0;
    keys[3] = 3;

    LateCount kr_lc;
    kr_lc.capacity = KEY_CAPACITY;
    kr_lc.host = []() { return std::size_t{4}; };

    auto g = rt.graph();
    g.add_counting_sort(keys, indices, offsets, total, fixed(N), kr_lc);
    g.run();

    const std::vector<std::uint32_t> one_digit_order = {2, 0, 1, 3};
    for (std::size_t i = 0; i < N; ++i)
        EXPECT_EQ(indices[i], one_digit_order[i]) << "i = " << i;
}

// A DynamicGraph region records the prefix scan and the counting sort exactly
// as a Graph does, each in its own region, with live counts changing between runs.
TEST(Integration_CountingSort, RegionsRecordScanAndCountingSort)
{
    constexpr std::size_t N_CAPACITY = 700;
    constexpr std::size_t KEY_CAPACITY = 1000;

    auto rt           = Runtime::create();
    auto keys         = rt.buffer<std::uint32_t>(N_CAPACITY);
    auto indices      = rt.buffer<std::uint32_t>(N_CAPACITY);
    auto offsets      = rt.buffer<std::uint32_t>(KEY_CAPACITY + 1);
    auto total        = rt.buffer<std::uint32_t>(1);
    auto scan_offsets = rt.buffer<std::uint32_t>(N_CAPACITY + 1);
    auto scan_total   = rt.buffer<std::uint32_t>(1);

    std::size_t live_n = 0;
    std::size_t live_key_range = 0;
    LateCount n_lc;
    n_lc.capacity = N_CAPACITY;
    n_lc.host = [&live_n]() { return live_n; };
    LateCount kr_lc;
    kr_lc.capacity = KEY_CAPACITY;
    kr_lc.host = [&live_key_range]() { return live_key_range; };

    auto world = rt.dynamic_graph();
    world.region(1).add_counting_sort(keys, indices, offsets, total, n_lc, kr_lc);
    world.region(2).add_prefix_scan(keys, scan_offsets, scan_total, n_lc);

    for (const auto& live : {std::pair<std::size_t, std::size_t>{300, 200},
                             std::pair<std::size_t, std::size_t>{700, 1000}})
    {
        live_n = live.first;
        live_key_range = live.second;
        std::mt19937 rng(static_cast<unsigned>(live_n));
        std::uniform_int_distribution<std::uint32_t> dist(
            0, static_cast<std::uint32_t>(live_key_range - 1));
        std::vector<std::uint32_t> host_keys(live_n);
        for (auto& k : host_keys)
            k = dist(rng);
        for (std::size_t i = 0; i < live_n; ++i)
            keys[i] = host_keys[i];

        world.run();

        const HostReference ref = host_counting_sort(host_keys, live_key_range);
        for (std::size_t i = 0; i < live_n; ++i)
            ASSERT_EQ(indices[i], ref.indices[i]) << "n = " << live_n << ", i = " << i;
        for (std::size_t k = 0; k <= live_key_range; ++k)
            ASSERT_EQ(offsets[k], ref.offsets[k]) << "n = " << live_n << ", k = " << k;
        std::uint32_t running = 0;
        for (std::size_t i = 0; i < live_n; ++i)
        {
            ASSERT_EQ(scan_offsets[i], running) << "n = " << live_n << ", i = " << i;
            running += host_keys[i];
        }
        EXPECT_EQ(scan_offsets[live_n], running);
        EXPECT_EQ(scan_total[0], running);
    }
}

// Per-substep coverage: run(4) with keys that change every step, so each
// step's sort must reflect the buffer as it stood when that step ran.
TEST(Integration_CountingSort, EachRunSortsItsOwnKeys)
{
    constexpr std::size_t N = 500;
    constexpr std::size_t KEY_RANGE = 20;
    constexpr std::size_t STEPS = 4;

    auto rt      = Runtime::create();
    auto keys    = rt.buffer<std::uint32_t>(N);
    auto indices = rt.buffer<std::uint32_t>(N);
    auto offsets = rt.buffer<std::uint32_t>(KEY_RANGE + 1);
    auto total   = rt.buffer<std::uint32_t>(1);

    auto g = rt.graph();
    g.add_counting_sort(keys, indices, offsets, total, fixed(N), fixed(KEY_RANGE));

    for (std::uint32_t step = 0; step < STEPS; ++step)
    {
        std::vector<std::uint32_t> host_keys(N);
        for (std::size_t i = 0; i < N; ++i)
            host_keys[i] = static_cast<std::uint32_t>((i * 7 + step * 3) % KEY_RANGE);
        for (std::size_t i = 0; i < N; ++i)
            keys[i] = host_keys[i];

        g.run();

        const HostReference ref = host_counting_sort(host_keys, KEY_RANGE);
        for (std::size_t i = 0; i < N; ++i)
            ASSERT_EQ(indices[i], ref.indices[i]) << "step " << step << ", i = " << i;
        for (std::size_t k = 0; k <= KEY_RANGE; ++k)
            ASSERT_EQ(offsets[k], ref.offsets[k]) << "step " << step << ", k = " << k;
    }
}

// G1 coverage: every internal pass one add_counting_sort() call emits shares
// one node_index, and run(steps) records one invocation per step for each.
TEST(Integration_CountingSort, SharesOneNodeIndexAcrossStepsAndPasses)
{
    // key_range.capacity = 50 needs one digit (50 - 1 fits in 6 bits), so
    // this call emits 1 (resolver, total and digit plan) + 1 * 4 (one digit
    // block) + 1 (offsets) + 1 (counts) = 7 nodes.
    constexpr std::size_t N_CAPACITY = 300;
    constexpr std::size_t KEY_CAPACITY = 50;
    constexpr std::size_t EXPECTED_PASSES = 7;
    constexpr std::size_t STEPS = 4;

    auto rt      = Runtime::create();
    auto keys    = rt.buffer<std::uint32_t>(N_CAPACITY);
    auto indices = rt.buffer<std::uint32_t>(N_CAPACITY);
    auto offsets = rt.buffer<std::uint32_t>(KEY_CAPACITY + 1);
    auto total   = rt.buffer<std::uint32_t>(1);
    auto counts  = rt.buffer<std::uint32_t>(KEY_CAPACITY);
    for (std::size_t i = 0; i < N_CAPACITY; ++i)
        keys[i] = static_cast<std::uint32_t>(i % KEY_CAPACITY);

    auto g = rt.graph();
    g.set_profiling_requested(true);
    g.add_counting_sort(keys, indices, offsets, total, fixed(N_CAPACITY), fixed(KEY_CAPACITY),
                        &counts);

    auto report = g.run(STEPS);

    ASSERT_EQ(report.node_events.size(), EXPECTED_PASSES * STEPS);

    const std::uint32_t node_index = report.node_events.front().node_index;
    std::vector<std::size_t> per_invocation(STEPS, 0);
    for (const auto& record : report.node_events)
    {
        EXPECT_EQ(record.node_index, node_index)
            << "every pass of one add_counting_sort() call must share one node_index";
        ASSERT_LT(record.invocation, STEPS);
        ++per_invocation[record.invocation];
    }
    for (std::size_t step = 0; step < STEPS; ++step)
        EXPECT_EQ(per_invocation[step], EXPECTED_PASSES) << "step " << step;
}

// A live element count above its capacity fails the run rather than reading
// or writing past the buffers it was planned for.
TEST(Integration_CountingSort, LateBoundCountAboveCapacityFailsTheRun)
{
    auto rt      = Runtime::create();
    auto keys    = rt.buffer<std::uint32_t>(100);
    auto indices = rt.buffer<std::uint32_t>(100);
    auto offsets = rt.buffer<std::uint32_t>(17);
    auto total   = rt.buffer<std::uint32_t>(1);

    LateCount n_lc;
    n_lc.capacity = 100;
    n_lc.host = []() { return std::size_t{101}; };

    auto g = rt.graph();
    g.add_counting_sort(keys, indices, offsets, total, n_lc, fixed(16));
    EXPECT_THROW(g.run(), SushiException);
}

// A live bucket count above its capacity fails the run too, even though the
// offsets pass itself never reads the live key range (design doc §A3): the
// key_range resolver still validates it every run.
TEST(Integration_CountingSort, LateBoundKeyRangeAboveCapacityFailsTheRun)
{
    auto rt      = Runtime::create();
    auto keys    = rt.buffer<std::uint32_t>(100);
    auto indices = rt.buffer<std::uint32_t>(100);
    auto offsets = rt.buffer<std::uint32_t>(17);
    auto total   = rt.buffer<std::uint32_t>(1);

    LateCount kr_lc;
    kr_lc.capacity = 16;
    kr_lc.host = []() { return std::size_t{17}; };

    auto g = rt.graph();
    g.add_counting_sort(keys, indices, offsets, total, fixed(100), kr_lc);
    EXPECT_THROW(g.run(), SushiException);
}
