/**************************************************************************/
/* test_dynamic_graph.cpp                                                 */
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
 * @file test_dynamic_graph.cpp
 * @brief Tests the region-partitioned graph that Runtime::dynamic_graph() returns.
 * @see modules/surface/api/tests/graph/regions/README.md
 * @author Mustafa Garip
 */

#include <atomic>
#include <chrono>
#include <cstddef>
#include <thread>
#include <gtest/gtest.h>
#include <SushiRuntime/sushiruntime.hpp>

using namespace SushiRuntime;

/** @brief Tests that two independent regions advance their buffers and compose the plan once. */
TEST(Integration_DynamicGraph, IndependentRegionsRunAndComposeOnce)
{
    constexpr std::size_t N     = 16;
    constexpr std::size_t STEPS = 5;

    auto rt = Runtime::create().value();
    rt.set_rebalancer_enabled(false);

    auto a = rt.buffer<int>(N).value();
    auto b = rt.buffer<int>(N).value();
    for (std::size_t i = 0; i < N; ++i) { a[i] = 0; b[i] = 0; }

    int* pa = a.data();
    int* pb = b.data();

    auto world = rt.dynamic_graph();
    world.region(0).add(N, Reads{}, Writes(a), [pa](std::size_t i) { pa[i] += 1; });
    world.region(1).add(N, Reads{}, Writes(b), [pb](std::size_t i) { pb[i] += 2; });

    world.run(STEPS).value();

    EXPECT_EQ(world.region_count(), 2u);
    EXPECT_EQ(world.compose_count(), 1u) << "an unmutated multi-step run must compose once.";
    for (std::size_t i = 0; i < N; ++i)
    {
        ASSERT_EQ(a[i], static_cast<int>(STEPS));
        ASSERT_EQ(b[i], static_cast<int>(2 * STEPS));
    }
}

/**
 * @brief Tests that a consumer region observes the writes of a producer region with a lower key.
 */
TEST(Integration_DynamicGraph, CrossRegionDependencyOrders)
{
    constexpr std::size_t N = 32;

    auto rt = Runtime::create().value();
    rt.set_rebalancer_enabled(false);

    auto x = rt.buffer<int>(N).value();
    auto y = rt.buffer<int>(N).value();
    for (std::size_t i = 0; i < N; ++i) { x[i] = 0; y[i] = -1; }

    int* px = x.data();
    int* py = y.data();

    auto world = rt.dynamic_graph();
    world.region(10).add(N, Reads{}, Writes(x),
                         [px](std::size_t i) { px[i] = static_cast<int>(i) + 1; });
    world.region(11).add(N, Reads(x), Writes(y),
                         [px, py](std::size_t i) { py[i] = px[i] * 2; });

    world.run().value();

    for (std::size_t i = 0; i < N; ++i)
        ASSERT_EQ(y[i], static_cast<int>(2 * (i + 1))) << "at i=" << i;
}

/**
 * @brief Tests that a dropped region's buffer freezes while the surviving region keeps advancing.
 */
TEST(Integration_DynamicGraph, DropRegionStopsItsWork)
{
    constexpr std::size_t N = 8;

    auto rt = Runtime::create().value();
    rt.set_rebalancer_enabled(false);

    auto a = rt.buffer<int>(N).value();
    auto b = rt.buffer<int>(N).value();
    for (std::size_t i = 0; i < N; ++i) { a[i] = 0; b[i] = 0; }

    int* pa = a.data();
    int* pb = b.data();

    auto world = rt.dynamic_graph();
    world.region(0).add(N, Reads{}, Writes(a), [pa](std::size_t i) { pa[i] += 1; });
    world.region(1).add(N, Reads{}, Writes(b), [pb](std::size_t i) { pb[i] += 1; });

    world.run(3).value();                 // both advance to 3

    world.drop(1);
    EXPECT_FALSE(world.has_region(1));
    world.run(2).value();                 // only region 0 advances

    for (std::size_t i = 0; i < N; ++i)
    {
        ASSERT_EQ(a[i], 5) << "surviving region kept running";
        ASSERT_EQ(b[i], 3) << "dropped region froze at the drop";
    }
    EXPECT_EQ(world.region_count(), 1u);
}

/**
 * @brief Tests that adding a region between steps recomposes once and takes effect on the next run.
 */
TEST(Integration_DynamicGraph, AddRegionBetweenSteps)
{
    constexpr std::size_t N = 8;

    auto rt = Runtime::create().value();
    rt.set_rebalancer_enabled(false);

    auto a = rt.buffer<int>(N).value();
    auto b = rt.buffer<int>(N).value();
    for (std::size_t i = 0; i < N; ++i) { a[i] = 0; b[i] = 0; }

    int* pa = a.data();
    int* pb = b.data();

    auto world = rt.dynamic_graph();
    world.region(0).add(N, Reads{}, Writes(a), [pa](std::size_t i) { pa[i] += 1; });
    world.run(2).value();
    EXPECT_EQ(world.compose_count(), 1u);

    world.region(1).add(N, Reads{}, Writes(b), [pb](std::size_t i) { pb[i] += 1; });
    world.run(2).value();
    EXPECT_EQ(world.compose_count(), 2u) << "adding a region recomposes once.";

    for (std::size_t i = 0; i < N; ++i)
    {
        ASSERT_EQ(a[i], 4) << "region 0 ran all four steps";
        ASSERT_EQ(b[i], 2) << "region 1 ran only the two steps after it was added";
    }
}

/** @brief Tests that mutating one region recomposes exactly once. */
TEST(Integration_DynamicGraph, SingleRecomposePerMutation)
{
    constexpr std::size_t N        = 4;
    constexpr std::size_t REGIONS  = 32;

    auto rt = Runtime::create().value();
    rt.set_rebalancer_enabled(false);

    std::vector<Buffer<int>> bufs;
    bufs.reserve(REGIONS);
    for (std::size_t r = 0; r < REGIONS; ++r)
        bufs.push_back(rt.buffer<int>(N).value());
    for (auto& buf : bufs)
        for (std::size_t i = 0; i < N; ++i)
            buf[i] = 0;

    auto world = rt.dynamic_graph();
    for (std::size_t r = 0; r < REGIONS; ++r)
    {
        int* p = bufs[r].data();
        world.region(r).add(N, Reads{}, Writes(bufs[r]),
                            [p](std::size_t i) { p[i] += 1; });
    }

    world.run(3).value();
    EXPECT_EQ(world.compose_count(), 1u) << "building all regions once composes once.";

    // Mutate exactly one region; one recompose, not REGIONS recomposes.
    int* p7 = bufs[7].data();
    world.region(7).add(N, Reads{}, Writes(bufs[7]),
                        [p7](std::size_t i) { p7[i] += 10; });
    world.run(1).value();
    EXPECT_EQ(world.compose_count(), 2u) << "one mutation recomposes exactly once.";

    for (std::size_t r = 0; r < REGIONS; ++r)
        for (std::size_t i = 0; i < N; ++i)
        {
            const int expected = (r == 7) ? (4 + 10) : 4;  // 3 steps + 1 step (+10 on its 4th)
            ASSERT_EQ(bufs[r][i], expected) << "region " << r << " at i=" << i;
        }
}

/**
 * @brief Tests that find_region() reads without recomposing and returns nullptr for an absent key.
 */
TEST(Integration_DynamicGraph, FindRegionReadsWithoutRecomposing)
{
    constexpr std::size_t N = 4;

    auto rt = Runtime::create().value();
    rt.set_rebalancer_enabled(false);

    auto a = rt.buffer<int>(N).value();
    int* pa = a.data();
    for (std::size_t i = 0; i < N; ++i)
        a[i] = 0;

    auto world = rt.dynamic_graph();
    world.region(0).add(N, Reads{}, Writes(a), [pa](std::size_t i) { pa[i] += 1; });
    world.run().value();
    const std::size_t composed = world.compose_count();

    const Region* found = world.find_region(0);
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->size(), 1u);
    EXPECT_EQ(world.find_region(9), nullptr) << "an absent key is not created by a lookup";
    EXPECT_FALSE(world.has_region(9));

    world.run().value();
    EXPECT_EQ(world.compose_count(), composed) << "a lookup is not a mutation";
    for (std::size_t i = 0; i < N; ++i)
        ASSERT_EQ(a[i], 2) << "at i=" << i;
}

/** @brief Tests that a three-region chain threads a value through both hops. */
TEST(Integration_DynamicGraph, ThreeRegionChainThreadsValue)
{
    constexpr std::size_t N = 24;

    auto rt = Runtime::create().value();
    rt.set_rebalancer_enabled(false);

    auto x = rt.buffer<int>(N).value();
    auto y = rt.buffer<int>(N).value();
    auto z = rt.buffer<int>(N).value();
    for (std::size_t i = 0; i < N; ++i) { x[i] = 0; y[i] = 0; z[i] = 0; }

    int* px = x.data();
    int* py = y.data();
    int* pz = z.data();

    auto world = rt.dynamic_graph();
    world.region(0).add(N, Reads{}, Writes(x),
                        [px](std::size_t i) { px[i] = static_cast<int>(i) + 1; });
    world.region(1).add(N, Reads(x), Writes(y),
                        [px, py](std::size_t i) { py[i] = px[i] * 2; });
    world.region(2).add(N, Reads(y), Writes(z),
                        [py, pz](std::size_t i) { pz[i] = py[i] + 1; });

    world.run().value();

    for (std::size_t i = 0; i < N; ++i)
        ASSERT_EQ(z[i], static_cast<int>(2 * (i + 1) + 1)) << "at i=" << i;
}

/** @brief Tests that two regions reading one producer's buffer both see its writes. */
TEST(Integration_DynamicGraph, ReadReadSharingStaysConsistent)
{
    constexpr std::size_t N = 24;

    auto rt = Runtime::create().value();
    rt.set_rebalancer_enabled(false);

    auto base = rt.buffer<int>(N).value();
    auto o1   = rt.buffer<int>(N).value();
    auto o2   = rt.buffer<int>(N).value();
    for (std::size_t i = 0; i < N; ++i) { base[i] = 0; o1[i] = 0; o2[i] = 0; }

    int* pb = base.data();
    int* p1 = o1.data();
    int* p2 = o2.data();

    auto world = rt.dynamic_graph();
    world.region(0).add(N, Reads{}, Writes(base),
                        [pb](std::size_t i) { pb[i] = static_cast<int>(i) + 1; });
    world.region(1).add(N, Reads(base), Writes(o1),
                        [pb, p1](std::size_t i) { p1[i] = pb[i] + 10; });
    world.region(2).add(N, Reads(base), Writes(o2),
                        [pb, p2](std::size_t i) { p2[i] = pb[i] + 100; });

    world.run().value();

    for (std::size_t i = 0; i < N; ++i)
    {
        ASSERT_EQ(o1[i], static_cast<int>(i) + 1 + 10)  << "reader 1 at i=" << i;
        ASSERT_EQ(o2[i], static_cast<int>(i) + 1 + 100) << "reader 2 at i=" << i;
    }
}

/** @brief Tests that streaming a consumer in rewires the existing producer's boundary. */
TEST(Integration_DynamicGraph, StreamInConsumerRewiresProducer)
{
    constexpr std::size_t N = 16;

    auto rt = Runtime::create().value();
    rt.set_rebalancer_enabled(false);

    auto x = rt.buffer<int>(N).value();
    auto y = rt.buffer<int>(N).value();
    for (std::size_t i = 0; i < N; ++i) { x[i] = 0; y[i] = -1; }

    int* px = x.data();
    int* py = y.data();

    auto world = rt.dynamic_graph();
    world.region(0).add(N, Reads{}, Writes(x),
                        [px](std::size_t i) { px[i] = static_cast<int>(i) + 1; });
    world.run().value();
    EXPECT_EQ(world.compose_count(), 1u);

    world.region(1).add(N, Reads(x), Writes(y),
                        [px, py](std::size_t i) { py[i] = px[i] * 3; });
    world.run().value();
    EXPECT_EQ(world.compose_count(), 2u) << "streaming a sharer in recomposes once.";

    for (std::size_t i = 0; i < N; ++i)
        ASSERT_EQ(y[i], static_cast<int>(i + 1) * 3) << "at i=" << i;
}

/** @brief Tests that dropping the producer leaves the consumer runnable. */
TEST(Integration_DynamicGraph, DropProducerLeavesConsumerRunnable)
{
    constexpr std::size_t N = 16;

    auto rt = Runtime::create().value();
    rt.set_rebalancer_enabled(false);

    auto x = rt.buffer<int>(N).value();
    auto y = rt.buffer<int>(N).value();
    for (std::size_t i = 0; i < N; ++i) { x[i] = 7; y[i] = 0; }

    int* px = x.data();
    int* py = y.data();

    auto world = rt.dynamic_graph();
    world.region(0).add(N, Reads{}, Writes(x),
                        [px](std::size_t i) { px[i] = static_cast<int>(i); });
    world.region(1).add(N, Reads(x), Writes(y),
                        [px, py](std::size_t i) { py[i] += px[i]; });
    world.run().value();

    world.drop(0);
    EXPECT_FALSE(world.has_region(0));
    world.run().value();                 // consumer alone; must not deadlock

    EXPECT_EQ(world.region_count(), 1u);
    // The exact value of y is not asserted, only that the consumer kept executing; see README.md.
    for (std::size_t i = 0; i < N; ++i)
        ASSERT_EQ(y[i], static_cast<int>(i) * 2) << "at i=" << i;
}

/**
 * @brief Tests that two regions writing disjoint ranges of one buffer are not ordered against each
 *        other.
 */
TEST(Integration_DynamicGraph, DisjointRangeRegionsAreNotOrdered)
{
    if (std::thread::hardware_concurrency() < 2)
        GTEST_SKIP() << "needs at least two workers to observe concurrency";

    constexpr std::size_t N = 64;
    constexpr std::size_t HALF = N / 2;

    auto rt = Runtime::create().value();
    rt.set_rebalancer_enabled(false);

    auto shared = rt.buffer<int>(N).value();
    for (std::size_t i = 0; i < N; ++i) shared[i] = 0;

    std::atomic<int> arrived{0};
    std::atomic<int> saw_both{0};

    auto rendezvous = [&arrived, &saw_both]()
    {
        arrived.fetch_add(1, std::memory_order_acq_rel);
        const auto deadline = std::chrono::steady_clock::now() +
                              std::chrono::milliseconds(500);
        while (arrived.load(std::memory_order_acquire) < 2 &&
               std::chrono::steady_clock::now() < deadline)
            std::this_thread::yield();
        if (arrived.load(std::memory_order_acquire) >= 2)
            saw_both.fetch_add(1, std::memory_order_acq_rel);
    };

    auto world = rt.dynamic_graph();
    world.region(0).add_host(Reads{},
                             Writes(shared.region({0, HALF})), rendezvous);
    world.region(1).add_host(Reads{},
                             Writes(shared.region({HALF, HALF})), rendezvous);
    world.run().value();

    EXPECT_EQ(saw_both.load(), 2)
        << "the two regions were serialized through the allocation they share, "
           "although their element ranges are disjoint";
}

/** @brief Tests that two regions writing the same range of one buffer are still ordered. */
TEST(Integration_DynamicGraph, OverlappingRangeRegionsStillSerialize)
{
    constexpr std::size_t N = 64;

    auto rt = Runtime::create().value();
    rt.set_rebalancer_enabled(false);

    auto shared = rt.buffer<int>(N).value();
    for (std::size_t i = 0; i < N; ++i) shared[i] = 0;
    int* p = shared.data();

    std::atomic<int> arrived{0};
    std::atomic<int> saw_both{0};

    auto rendezvous = [&arrived, &saw_both]()
    {
        arrived.fetch_add(1, std::memory_order_acq_rel);
        const auto deadline = std::chrono::steady_clock::now() +
                              std::chrono::milliseconds(200);
        while (arrived.load(std::memory_order_acquire) < 2 &&
               std::chrono::steady_clock::now() < deadline)
            std::this_thread::yield();
        if (arrived.load(std::memory_order_acquire) >= 2)
            saw_both.fetch_add(1, std::memory_order_acq_rel);
    };

    auto world = rt.dynamic_graph();
    world.region(0).add_host(Reads{}, Writes(shared),
                             [p, rendezvous]()
                             {
                                 for (std::size_t i = 0; i < N; ++i) p[i] = 1;
                                 rendezvous();
                             });
    world.region(1).add_host(Reads(shared), Writes(shared),
                             [p, rendezvous]()
                             {
                                 rendezvous();
                                 for (std::size_t i = 0; i < N; ++i) p[i] = p[i] * 2 + 1;
                             });
    world.run().value();

    EXPECT_EQ(saw_both.load(), 1)
        << "overlapping ranges ran concurrently: the cross-region edge was lost";
    for (std::size_t i = 0; i < N; ++i)
        ASSERT_EQ(shared[i], 3) << "at i=" << i;
}

/** @brief Tests that a whole-buffer reader runs after both half writers. */
TEST(Integration_DynamicGraph, WholeBufferReaderOrdersAfterBothHalfWriters)
{
    constexpr std::size_t N = 128;
    constexpr std::size_t HALF = N / 2;

    auto rt = Runtime::create().value();
    rt.set_rebalancer_enabled(false);

    auto shared = rt.buffer<int>(N).value();
    auto total  = rt.buffer<int>(1).value();
    for (std::size_t i = 0; i < N; ++i) shared[i] = -1;
    total[0] = 0;

    int* p = shared.data();
    int* t = total.data();

    auto world = rt.dynamic_graph();
    world.region(0).add(HALF, Reads{}, Writes(shared.region({0, HALF})),
                        [p](std::size_t i) { p[i] = 2; });
    world.region(1).add(HALF, Reads{}, Writes(shared.region({HALF, HALF})),
                        [p](std::size_t i) { p[HALF + i] = 3; });
    world.region(2).add(std::size_t{1}, Reads(shared), Writes(total),
                        [p, t](std::size_t)
                        {
                            int sum = 0;
                            for (std::size_t i = 0; i < N; ++i) sum += p[i];
                            t[0] = sum;
                        });
    world.run().value();

    EXPECT_EQ(total[0], static_cast<int>(HALF) * 2 + static_cast<int>(HALF) * 3)
        << "the reader observed a half that had not been written yet";
}
