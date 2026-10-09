/**************************************************************************/
/* test_region_emitter.cpp                                                */
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
 * @file test_region_emitter.cpp
 * @brief Tests add_emitter(), the recording form that hands the caller the backing graph.
 * @see tests/integration/README.md
 * @author Mustafa Garip
 */

#include <cstddef>
#include <memory>
#include <vector>

#include <gtest/gtest.h>

#include <SushiRuntime/SushiRuntime.h>

using namespace SushiRuntime;

namespace
{
    /** @brief Records one device task on @p bg running @p kernel over [0, n), ordered by regions. */
    template <class Kernel>
    void record_device_task(Graph::TaskGraph& bg, const char* name,
                            std::vector<Core::ResourceRegion> reads,
                            std::vector<Core::ResourceRegion> writes, std::size_t n, Kernel kernel)
    {
        Graph::TaskMetadata meta{};
        meta.name = name;
        bg.add_task(meta, reads, writes,
            [kernel, n](sycl::queue& q, const std::vector<sycl::event>& deps) -> sycl::event
            {
                return q.submit([kernel, n, &deps](sycl::handler& h)
                {
                    h.depends_on(deps);
                    h.parallel_for(sycl::range<1>(n), [kernel](sycl::id<1> i) { kernel(i[0]); });
                });
            });
    }
} // namespace

/** @brief Tests that the nodes an emitter records run in order and precede a consumer region. */
TEST(Integration_RegionEmitter, EmittedNodesOrderAgainstANeighbourRegion)
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
    world.region(10).add_emitter([px](Graph::TaskGraph& bg)
    {
        record_device_task(bg, "fill", {}, {px}, N,
                           [px](std::size_t i) { px[i] = static_cast<int>(i) + 1; });
        record_device_task(bg, "triple", {px}, {px}, N, [px](std::size_t i) { px[i] *= 3; });
    });
    world.region(11).add(API::Reads(x), API::Writes(y), N,
                         [px, py](std::size_t i) { py[i] = px[i] * 2; });

    world.run();

    for (std::size_t i = 0; i < N; ++i)
        ASSERT_EQ(y[i], static_cast<int>(6 * (i + 1))) << "at i=" << i;
}

/** @brief Tests that an emitter counts as one rule however many nodes it records. */
TEST(Integration_RegionEmitter, AnEmitterIsOneRule)
{
    constexpr std::size_t N = 4;

    auto rt = Runtime::create();
    rt.rebalancer(false);

    auto x = rt.buffer<int>(N);
    int* px = x.data();
    for (std::size_t i = 0; i < N; ++i)
        x[i] = 0;

    auto world = rt.dynamic_graph();
    world.region(0).add_emitter([px](Graph::TaskGraph& bg)
    {
        record_device_task(bg, "first", {}, {px}, N, [px](std::size_t i) { px[i] += 1; });
        record_device_task(bg, "second", {px}, {px}, N, [px](std::size_t i) { px[i] += 1; });
    });
    world.run();

    const API::Region* found = world.find_region(0);
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->size(), 1u);
    EXPECT_EQ(world.size(), 1u);
    EXPECT_EQ(x[0], 2);
}

/** @brief Tests that an emitter is called again only when its own region is rebuilt. */
TEST(Integration_RegionEmitter, AnEmitterReplaysOnlyWhenItsRegionIsRebuilt)
{
    constexpr std::size_t N = 8;

    auto rt = Runtime::create();
    rt.rebalancer(false);

    auto a = rt.buffer<int>(N);
    auto b = rt.buffer<int>(N);
    for (std::size_t i = 0; i < N; ++i) { a[i] = 0; b[i] = 0; }

    int* pa = a.data();
    int* pb = b.data();

    const auto emitted = std::make_shared<int>(0);
    const auto emitter = [pa, emitted](Graph::TaskGraph& bg)
    {
        ++*emitted;
        record_device_task(bg, "advance_a", {pa}, {pa}, N, [pa](std::size_t i) { pa[i] += 1; });
    };

    auto world = rt.dynamic_graph();
    world.region(0).add_emitter(emitter);
    world.region(1).add(API::Reads{}, API::Writes(b), N, [pb](std::size_t i) { pb[i] += 1; });

    world.run(3);
    EXPECT_EQ(*emitted, 1) << "a replayed step does not call the emitter";
    EXPECT_EQ(world.compile_count(), 1u);

    world.region(1).add(API::Reads{}, API::Writes(b), N, [pb](std::size_t i) { pb[i] += 1; });
    world.run();
    EXPECT_EQ(*emitted, 1) << "rebuilding a neighbour leaves this region's nodes alone";

    world.drop(0);
    world.region(0).add_emitter(emitter);
    world.run();
    EXPECT_EQ(*emitted, 2) << "a region recorded again emits again";

    for (std::size_t i = 0; i < N; ++i)
    {
        ASSERT_EQ(a[i], 5) << "at i=" << i;
        ASSERT_EQ(b[i], 3 + 2 + 2) << "at i=" << i;
    }
}

/** @brief Tests that a static graph accepts an emitter beside an ordinary add(). */
TEST(Integration_RegionEmitter, AStaticGraphTakesAnEmitter)
{
    constexpr std::size_t N = 16;

    auto rt = Runtime::create();
    rt.rebalancer(false);

    auto x = rt.buffer<int>(N);
    auto y = rt.buffer<int>(N);
    for (std::size_t i = 0; i < N; ++i) { x[i] = 0; y[i] = 0; }

    int* px = x.data();
    int* py = y.data();

    auto graph = rt.graph();
    graph.add_emitter([px](Graph::TaskGraph& bg)
    {
        record_device_task(bg, "count_up", {px}, {px}, N, [px](std::size_t i) { px[i] += 1; });
    });
    graph.add(API::Reads(x), API::Writes(y), N, [px, py](std::size_t i) { py[i] = px[i] * 10; });

    graph.run(4);

    EXPECT_EQ(graph.size(), 2u);
    for (std::size_t i = 0; i < N; ++i)
    {
        ASSERT_EQ(x[i], 4) << "at i=" << i;
        ASSERT_EQ(y[i], 40) << "at i=" << i;
    }
}
