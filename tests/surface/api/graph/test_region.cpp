/**************************************************************************/
/* test_region.cpp                                                        */
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
 * @file test_region.cpp
 * @brief Tests the extensions a Region records.
 * @see tests/surface/api/graph/README.md
 * @author Mustafa Garip
 */

#include <cstddef>
#include <string_view>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include <SushiRuntime/sushiruntime.hpp>

using namespace SushiRuntime;

namespace
{
    /** @brief Records one named task that runs kernel over [0, n), ordered by its access. */
    template <class Kernel>
    void record_device_task(Recorder& recorder, std::string_view name, Access access,
                            std::size_t n, Kernel kernel)
    {
        NodeOptions options;
        options.width = n;
        options.name = name;
        recorder.task(std::move(access),
            [kernel, n](sycl::queue& queue, const std::vector<sycl::event>& after) -> sycl::event
            {
                return queue.submit([kernel, n, &after](sycl::handler& h)
                {
                    h.depends_on(after);
                    h.parallel_for(sycl::range<1>(n), [kernel](sycl::id<1> i) { kernel(i[0]); });
                });
            }, options);
    }
} // namespace

/** @brief Tests that the tasks an extension records run in order and precede a consumer region. */
TEST(Integration_RegionEmitter, ExtensionTasksOrderAgainstANeighbourRegion)
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
    world.region(10).extend([&x, px](Recorder& recorder)
    {
        record_device_task(recorder, "fill", Access(Reads(), Writes(x)), N,
                           [px](std::size_t i) { px[i] = static_cast<int>(i) + 1; });
        record_device_task(recorder, "triple", Access(Reads(x), Writes(x)), N,
                           [px](std::size_t i) { px[i] *= 3; });
    });
    world.region(11).add(N, Reads(x), Writes(y),
                         [px, py](std::size_t i) { py[i] = px[i] * 2; });

    world.run().value();

    for (std::size_t i = 0; i < N; ++i)
        ASSERT_EQ(y[i], static_cast<int>(6 * (i + 1))) << "at i=" << i;
}

/** @brief Tests that an extension counts as one rule however many tasks it records. */
TEST(Integration_RegionEmitter, AnExtensionIsOneRule)
{
    constexpr std::size_t N = 4;

    auto rt = Runtime::create().value();
    rt.set_rebalancer_enabled(false);

    auto x = rt.buffer<int>(N).value();
    int* px = x.data();
    for (std::size_t i = 0; i < N; ++i)
        x[i] = 0;

    auto world = rt.dynamic_graph();
    world.region(0).extend([&x, px](Recorder& recorder)
    {
        record_device_task(recorder, "first", Access(Reads(), Writes(x)), N,
                           [px](std::size_t i) { px[i] += 1; });
        record_device_task(recorder, "second", Access(Reads(x), Writes(x)), N,
                           [px](std::size_t i) { px[i] += 1; });
    });
    world.run().value();

    const Region* found = world.find_region(0);
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->size(), 1u);
    EXPECT_EQ(found->node_count(), 2u);
    EXPECT_EQ(world.size(), 1u);
    EXPECT_EQ(x[0], 2);
}

/** @brief Tests that an extension runs once, when extend() is called, and never at a build. */
TEST(Integration_RegionEmitter, AnExtensionRunsOnceWhenItIsCalled)
{
    constexpr std::size_t N = 8;

    auto rt = Runtime::create().value();
    rt.set_rebalancer_enabled(false);

    auto a = rt.buffer<int>(N).value();
    auto b = rt.buffer<int>(N).value();
    for (std::size_t i = 0; i < N; ++i) { a[i] = 0; b[i] = 0; }

    int* pa = a.data();
    int* pb = b.data();

    int extended = 0;
    const auto extension = [&a, pa, &extended](Recorder& recorder)
    {
        ++extended;
        record_device_task(recorder, "advance_a", Access(Reads(a), Writes(a)), N,
                           [pa](std::size_t i) { pa[i] += 1; });
    };

    auto world = rt.dynamic_graph();
    world.region(0).extend(extension);
    EXPECT_EQ(extended, 1) << "extend() calls the extension before it returns";
    world.region(1).add(N, Reads{}, Writes(b), [pb](std::size_t i) { pb[i] += 1; });

    world.run(3).value();
    EXPECT_EQ(extended, 1) << "a build and a replayed step do not call the extension";
    EXPECT_EQ(world.compose_count(), 1u);

    world.region(1).add(N, Reads{}, Writes(b), [pb](std::size_t i) { pb[i] += 1; });
    world.run().value();
    EXPECT_EQ(extended, 1) << "rebuilding a neighbour leaves this region's nodes alone";

    world.drop(0);
    world.region(0).extend(extension);
    world.run().value();
    EXPECT_EQ(extended, 2) << "a region recorded again is extended again";

    for (std::size_t i = 0; i < N; ++i)
    {
        ASSERT_EQ(a[i], 5) << "at i=" << i;
        ASSERT_EQ(b[i], 3 + 2 + 2) << "at i=" << i;
    }
}

/** @brief Tests that a static graph accepts an extension beside an ordinary add(). */
TEST(Integration_RegionEmitter, AStaticGraphTakesAnExtension)
{
    constexpr std::size_t N = 16;

    auto rt = Runtime::create().value();
    rt.set_rebalancer_enabled(false);

    auto x = rt.buffer<int>(N).value();
    auto y = rt.buffer<int>(N).value();
    for (std::size_t i = 0; i < N; ++i) { x[i] = 0; y[i] = 0; }

    int* px = x.data();
    int* py = y.data();

    auto graph = rt.graph();
    graph.extend([&x, px](Recorder& recorder)
    {
        record_device_task(recorder, "count_up", Access(Reads(x), Writes(x)), N,
                           [px](std::size_t i) { px[i] += 1; });
    });
    graph.add(N, Reads(x), Writes(y), [px, py](std::size_t i) { py[i] = px[i] * 10; });

    graph.run(4).value();

    EXPECT_EQ(graph.size(), 2u);
    for (std::size_t i = 0; i < N; ++i)
    {
        ASSERT_EQ(x[i], 4) << "at i=" << i;
        ASSERT_EQ(y[i], 40) << "at i=" << i;
    }
}
