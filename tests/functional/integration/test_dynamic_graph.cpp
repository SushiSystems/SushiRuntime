/**************************************************************************/
/* test_dynamic_graph.cpp                                                 */
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

// Coverage for the dynamic, region-partitioned graph (Runtime::dynamic_graph()).
// Each test asserts both the numerical outcome and the composition behaviour:
// regions run together, cross-region data dependencies order correctly, dropping
// a region removes its work, and an unmutated run never recomposes.

#include <cstddef>
#include <gtest/gtest.h>
#include <SushiRuntime/SushiRuntime.h>

using namespace SushiRuntime;

// Two independent regions each own a buffer and increment it once per step. Over
// several steps both buffers advance, and a run that mutates nothing composes the
// combined plan exactly once.
TEST(Integration_DynamicGraph, IndependentRegionsRunAndComposeOnce)
{
    constexpr std::size_t N     = 16;
    constexpr std::size_t STEPS = 5;

    auto rt = Runtime::create();
    rt.rebalancer(false);

    auto a = rt.buffer<int>(N);
    auto b = rt.buffer<int>(N);
    for (std::size_t i = 0; i < N; ++i) { a[i] = 0; b[i] = 0; }

    int* pa = a.data();
    int* pb = b.data();

    auto world = rt.dynamic_graph();
    world.region(0).add(API::Reads{}, API::Writes(a), N, [pa](std::size_t i) { pa[i] += 1; });
    world.region(1).add(API::Reads{}, API::Writes(b), N, [pb](std::size_t i) { pb[i] += 2; });

    world.run(STEPS);

    EXPECT_EQ(world.region_count(), 2u);
    EXPECT_EQ(world.compile_count(), 1u) << "an unmutated multi-step run must compose once.";
    for (std::size_t i = 0; i < N; ++i)
    {
        ASSERT_EQ(a[i], static_cast<int>(STEPS));
        ASSERT_EQ(b[i], static_cast<int>(2 * STEPS));
    }
}

// A producer region writes x; a consumer region in a *different* region reads x
// and writes y. Because both touch x, the composer must wire a cross-region edge
// so the consumer observes the producer's writes — y[i] == 2*(i+1). Cross-region
// ordering follows ascending region key, so the producer is keyed (10) below the
// consumer (11), matching the documented contract.
TEST(Integration_DynamicGraph, CrossRegionDependencyOrders)
{
    constexpr std::size_t N = 32;

    auto rt = Runtime::create();
    rt.rebalancer(false);

    auto x = rt.buffer<int>(N);
    auto y = rt.buffer<int>(N);
    for (std::size_t i = 0; i < N; ++i) { x[i] = 0; y[i] = -1; }

    int* px = x.data();
    int* py = y.data();

    auto world = rt.dynamic_graph();
    world.region(10).add(API::Reads{}, API::Writes(x), N,
                         [px](std::size_t i) { px[i] = static_cast<int>(i) + 1; });
    world.region(11).add(API::Reads(x), API::Writes(y), N,
                         [px, py](std::size_t i) { py[i] = px[i] * 2; });

    world.run();

    for (std::size_t i = 0; i < N; ++i)
        ASSERT_EQ(y[i], static_cast<int>(2 * (i + 1))) << "at i=" << i;
}

// Dropping a region removes its work at the next step boundary: after the drop
// the surviving region keeps advancing while the dropped region's buffer freezes.
TEST(Integration_DynamicGraph, DropRegionStopsItsWork)
{
    constexpr std::size_t N = 8;

    auto rt = Runtime::create();
    rt.rebalancer(false);

    auto a = rt.buffer<int>(N);
    auto b = rt.buffer<int>(N);
    for (std::size_t i = 0; i < N; ++i) { a[i] = 0; b[i] = 0; }

    int* pa = a.data();
    int* pb = b.data();

    auto world = rt.dynamic_graph();
    world.region(0).add(API::Reads{}, API::Writes(a), N, [pa](std::size_t i) { pa[i] += 1; });
    world.region(1).add(API::Reads{}, API::Writes(b), N, [pb](std::size_t i) { pb[i] += 1; });

    world.run(3);                 // both advance to 3

    world.drop(1);
    EXPECT_FALSE(world.has_region(1));
    world.run(2);                 // only region 0 advances

    for (std::size_t i = 0; i < N; ++i)
    {
        ASSERT_EQ(a[i], 5) << "surviving region kept running";
        ASSERT_EQ(b[i], 3) << "dropped region froze at the drop";
    }
    EXPECT_EQ(world.region_count(), 1u);
}

// Adding a region between steps recomposes exactly once and the new region's work
// takes effect immediately on the following run.
TEST(Integration_DynamicGraph, AddRegionBetweenSteps)
{
    constexpr std::size_t N = 8;

    auto rt = Runtime::create();
    rt.rebalancer(false);

    auto a = rt.buffer<int>(N);
    auto b = rt.buffer<int>(N);
    for (std::size_t i = 0; i < N; ++i) { a[i] = 0; b[i] = 0; }

    int* pa = a.data();
    int* pb = b.data();

    auto world = rt.dynamic_graph();
    world.region(0).add(API::Reads{}, API::Writes(a), N, [pa](std::size_t i) { pa[i] += 1; });
    world.run(2);
    EXPECT_EQ(world.compile_count(), 1u);

    world.region(1).add(API::Reads{}, API::Writes(b), N, [pb](std::size_t i) { pb[i] += 1; });
    world.run(2);
    EXPECT_EQ(world.compile_count(), 2u) << "adding a region recomposes once.";

    for (std::size_t i = 0; i < N; ++i)
    {
        ASSERT_EQ(a[i], 4) << "region 0 ran all four steps";
        ASSERT_EQ(b[i], 2) << "region 1 ran only the two steps after it was added";
    }
}

// Mutating one region out of many recomposes a single time; the unchanged regions
// are not rebuilt (proven indirectly: results stay correct and compile_count
// advances by exactly one per mutation, not per region).
TEST(Integration_DynamicGraph, SingleRecomposePerMutation)
{
    constexpr std::size_t N        = 4;
    constexpr std::size_t REGIONS  = 32;

    auto rt = Runtime::create();
    rt.rebalancer(false);

    std::vector<Buffer<int>> bufs;
    bufs.reserve(REGIONS);
    for (std::size_t r = 0; r < REGIONS; ++r)
        bufs.push_back(rt.buffer<int>(N));
    for (auto& buf : bufs)
        for (std::size_t i = 0; i < N; ++i)
            buf[i] = 0;

    auto world = rt.dynamic_graph();
    for (std::size_t r = 0; r < REGIONS; ++r)
    {
        int* p = bufs[r].data();
        world.region(r).add(API::Reads{}, API::Writes(bufs[r]), N,
                            [p](std::size_t i) { p[i] += 1; });
    }

    world.run(3);
    EXPECT_EQ(world.compile_count(), 1u) << "building all regions once composes once.";

    // Mutate exactly one region; one recompose, not REGIONS recomposes.
    int* p7 = bufs[7].data();
    world.region(7).add(API::Reads{}, API::Writes(bufs[7]), N,
                        [p7](std::size_t i) { p7[i] += 10; });
    world.run(1);
    EXPECT_EQ(world.compile_count(), 2u) << "one mutation recomposes exactly once.";

    for (std::size_t r = 0; r < REGIONS; ++r)
        for (std::size_t i = 0; i < N; ++i)
        {
            const int expected = (r == 7) ? (4 + 10) : 4;  // 3 steps + 1 step (+10 on its 4th)
            ASSERT_EQ(bufs[r][i], expected) << "region " << r << " at i=" << i;
        }
}

// A three-hop cross-region chain over three buffers: region 0 writes x, region 1
// reads x and writes y, region 2 reads y and writes z. Each hop is a separate
// boundary; the composer must order 0 -> 1 -> 2 so z carries the value threaded
// through both hops: z[i] == 2*(i+1) + 1.
TEST(Integration_DynamicGraph, ThreeRegionChainThreadsValue)
{
    constexpr std::size_t N = 24;

    auto rt = Runtime::create();
    rt.rebalancer(false);

    auto x = rt.buffer<int>(N);
    auto y = rt.buffer<int>(N);
    auto z = rt.buffer<int>(N);
    for (std::size_t i = 0; i < N; ++i) { x[i] = 0; y[i] = 0; z[i] = 0; }

    int* px = x.data();
    int* py = y.data();
    int* pz = z.data();

    auto world = rt.dynamic_graph();
    world.region(0).add(API::Reads{}, API::Writes(x), N,
                        [px](std::size_t i) { px[i] = static_cast<int>(i) + 1; });
    world.region(1).add(API::Reads(x), API::Writes(y), N,
                        [px, py](std::size_t i) { py[i] = px[i] * 2; });
    world.region(2).add(API::Reads(y), API::Writes(z), N,
                        [py, pz](std::size_t i) { pz[i] = py[i] + 1; });

    world.run();

    for (std::size_t i = 0; i < N; ++i)
        ASSERT_EQ(z[i], static_cast<int>(2 * (i + 1) + 1)) << "at i=" << i;
}

// One producer writes base; two consumer regions both READ base (and write their
// own outputs). The two readers share no hazard, so the composer must not order
// them against each other — both simply see the producer's writes. Correctness is
// the observable here: each reader's output reflects base.
TEST(Integration_DynamicGraph, ReadReadSharingStaysConsistent)
{
    constexpr std::size_t N = 24;

    auto rt = Runtime::create();
    rt.rebalancer(false);

    auto base = rt.buffer<int>(N);
    auto o1   = rt.buffer<int>(N);
    auto o2   = rt.buffer<int>(N);
    for (std::size_t i = 0; i < N; ++i) { base[i] = 0; o1[i] = 0; o2[i] = 0; }

    int* pb = base.data();
    int* p1 = o1.data();
    int* p2 = o2.data();

    auto world = rt.dynamic_graph();
    world.region(0).add(API::Reads{}, API::Writes(base), N,
                        [pb](std::size_t i) { pb[i] = static_cast<int>(i) + 1; });
    world.region(1).add(API::Reads(base), API::Writes(o1), N,
                        [pb, p1](std::size_t i) { p1[i] = pb[i] + 10; });
    world.region(2).add(API::Reads(base), API::Writes(o2), N,
                        [pb, p2](std::size_t i) { p2[i] = pb[i] + 100; });

    world.run();

    for (std::size_t i = 0; i < N; ++i)
    {
        ASSERT_EQ(o1[i], static_cast<int>(i) + 1 + 10)  << "reader 1 at i=" << i;
        ASSERT_EQ(o2[i], static_cast<int>(i) + 1 + 100) << "reader 2 at i=" << i;
    }
}

// Streaming a consumer in next to an existing producer must re-stitch the
// producer's boundary: region 0 (writes x) runs alone first, then region 1
// (reads x) streams in and on the next run observes x. Exercises the neighbour
// re-wire path — an unchanged region's exit pin gaining a cross-region successor.
TEST(Integration_DynamicGraph, StreamInConsumerRewiresProducer)
{
    constexpr std::size_t N = 16;

    auto rt = Runtime::create();
    rt.rebalancer(false);

    auto x = rt.buffer<int>(N);
    auto y = rt.buffer<int>(N);
    for (std::size_t i = 0; i < N; ++i) { x[i] = 0; y[i] = -1; }

    int* px = x.data();
    int* py = y.data();

    auto world = rt.dynamic_graph();
    world.region(0).add(API::Reads{}, API::Writes(x), N,
                        [px](std::size_t i) { px[i] = static_cast<int>(i) + 1; });
    world.run();
    EXPECT_EQ(world.compile_count(), 1u);

    world.region(1).add(API::Reads(x), API::Writes(y), N,
                        [px, py](std::size_t i) { py[i] = px[i] * 3; });
    world.run();
    EXPECT_EQ(world.compile_count(), 2u) << "streaming a sharer in recomposes once.";

    for (std::size_t i = 0; i < N; ++i)
        ASSERT_EQ(y[i], static_cast<int>(i + 1) * 3) << "at i=" << i;
}

// Dropping the producer must cleanly un-wire the boundary and leave the consumer
// runnable (now without an upstream edge). Region 1 keeps running after region 0
// is dropped; the run does not deadlock or fault.
TEST(Integration_DynamicGraph, DropProducerLeavesConsumerRunnable)
{
    constexpr std::size_t N = 16;

    auto rt = Runtime::create();
    rt.rebalancer(false);

    auto x = rt.buffer<int>(N);
    auto y = rt.buffer<int>(N);
    for (std::size_t i = 0; i < N; ++i) { x[i] = 7; y[i] = 0; }

    int* px = x.data();
    int* py = y.data();

    auto world = rt.dynamic_graph();
    world.region(0).add(API::Reads{}, API::Writes(x), N,
                        [px](std::size_t i) { px[i] = static_cast<int>(i); });
    world.region(1).add(API::Reads(x), API::Writes(y), N,
                        [px, py](std::size_t i) { py[i] += px[i]; });
    world.run();

    world.drop(0);
    EXPECT_FALSE(world.has_region(0));
    world.run();                 // consumer alone; must not deadlock

    EXPECT_EQ(world.region_count(), 1u);
    // y accumulated px across two steps; the exact value is not the point — the
    // point is the run completed and the consumer kept executing.
    for (std::size_t i = 0; i < N; ++i)
        ASSERT_EQ(y[i], static_cast<int>(i) * 2) << "at i=" << i;
}
