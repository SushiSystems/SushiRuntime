/**************************************************************************/
/* test_dynamic_graph_regions.cpp                                         */
/**************************************************************************/
/*                          This file is part of:                         */
/*                              SushiRuntime                              */
/*              https://github.com/SushiSystems/SushiRuntime              */
/*                         https://sushisystems.io                        */
/**************************************************************************/
/* Copyright (c) 2026-present Mustafa Garip & Sushi Systems               */
/*                                                                        */
/* Licensed under the Apache License, Version 2.0 (the "License"); you    */
/* may not use this file except in compliance with the License. You may   */
/* obtain a copy of the License at                                        */
/*                                                                        */
/*     https://www.apache.org/licenses/LICENSE-2.0                        */
/*                                                                        */
/* Unless required by applicable law or agreed to in writing, software    */
/* distributed under the License is distributed on an "AS IS" BASIS,      */
/* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or        */
/* implied. See the License for the specific language governing           */
/* permissions and limitations under the License.                         */
/**************************************************************************/

/**
 * @file test_dynamic_graph_regions.cpp
 * @brief Tests the DynamicGraph: its regions, their composition and the order they keep.
 * @see tests/surface/api/graph/README.md
 * @author Mustafa Garip
 */

#include <atomic>
#include <chrono>
#include <cstddef>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include <SushiRuntime/sushiruntime.hpp>

#include "support/host_runtime.hpp"
#include "support/runtime_environment.hpp"

using namespace SushiRuntime;

/** @brief Tests the region-partitioned graph that Runtime::dynamic_graph() returns. */
namespace DynamicGraphTests
{
    /** @brief Fixture of the region tests that need no device, which run on a host runtime. */
    class Small_DynamicGraph : public TestSupport::HostRuntimeTest
    {
    };

    /** @brief Fixture of the region test of real concurrency, which runs on the program's runtime with the rebalancer off. */
    class Medium_DynamicGraph : public TestSupport::RuntimeTest
    {
        protected:
            /** @brief Turns the background rebalancer off, so no thermal move reorders a region. */
            void SetUp() override { runtime().set_rebalancer_enabled(false); }
    };

    /** @brief Fixture of the rows over the number of regions a chain threads a value through. */
    class Small_DynamicGraphChain : public Small_DynamicGraph,
                                     public ::testing::WithParamInterface<std::size_t>
    {
    };

    /** @brief Tests that two independent regions advance their own buffers over a multi-step run. */
    TEST_F(Small_DynamicGraph, IndependentRegionsAdvanceTheirOwnBuffers)
    {
        constexpr std::size_t N     = 16;
        constexpr std::size_t STEPS = 5;

        auto a = runtime().buffer<int>(N).value();
        auto b = runtime().buffer<int>(N).value();
        for (std::size_t i = 0; i < N; ++i) { a[i] = 0; b[i] = 0; }

        int* pa = a.data();
        int* pb = b.data();

        auto world = runtime().dynamic_graph();
        world.region(0).add(N, Reads{}, Writes(a), [pa](std::size_t i) { pa[i] += 1; });
        world.region(1).add(N, Reads{}, Writes(b), [pb](std::size_t i) { pb[i] += 2; });

        world.run(STEPS).value();

        EXPECT_EQ(world.region_count(), 2u);
        for (std::size_t i = 0; i < N; ++i)
        {
            ASSERT_EQ(a[i], static_cast<int>(STEPS));
            ASSERT_EQ(b[i], static_cast<int>(2 * STEPS));
        }
    }

    /** @brief Tests that an unmutated multi-step run composes the plan once. */
    TEST_F(Small_DynamicGraph, AnUnmutatedMultiStepRunComposesOnce)
    {
        constexpr std::size_t N     = 16;
        constexpr std::size_t STEPS = 5;

        auto a = runtime().buffer<int>(N).value();
        auto b = runtime().buffer<int>(N).value();
        int* pa = a.data();
        int* pb = b.data();

        auto world = runtime().dynamic_graph();
        world.region(0).add(N, Reads{}, Writes(a), [pa](std::size_t i) { pa[i] += 1; });
        world.region(1).add(N, Reads{}, Writes(b), [pb](std::size_t i) { pb[i] += 2; });

        world.run(STEPS).value();

        EXPECT_EQ(world.compose_count(), 1u) << "an unmutated multi-step run must compose once.";
    }

    /** @brief Tests that a chain of regions with ascending keys threads a value through every hop. */
    TEST_P(Small_DynamicGraphChain, ARegionObservesTheWritesOfTheRegionBeforeIt)
    {
        constexpr std::size_t N = 24;
        const std::size_t regions = GetParam();

        std::vector<Buffer<int>> hops;
        for (std::size_t r = 0; r < regions; ++r)
        {
            hops.push_back(runtime().buffer<int>(N).value());
            for (std::size_t i = 0; i < N; ++i)
                hops[r][i] = -1;
        }

        auto world = runtime().dynamic_graph();
        int* first = hops[0].data();
        world.region(10).add(N, Reads{}, Writes(hops[0]),
                             [first](std::size_t i) { first[i] = static_cast<int>(i) + 1; });
        for (std::size_t r = 1; r < regions; ++r)
        {
            const int* from = hops[r - 1].data();
            int* to = hops[r].data();
            world.region(10 + r).add(N, Reads(hops[r - 1]), Writes(hops[r]),
                                     [from, to](std::size_t i) { to[i] = from[i] * 2; });
        }

        world.run().value();

        const int factor = 1 << (regions - 1);
        for (std::size_t i = 0; i < N; ++i)
            ASSERT_EQ(hops[regions - 1][i], static_cast<int>(i + 1) * factor) << "at i=" << i;
    }

    INSTANTIATE_TEST_SUITE_P(Rows, Small_DynamicGraphChain, ::testing::Values(2u, 3u));

    /**
     * @brief Tests that a dropped region's buffer freezes while the surviving region keeps advancing.
     */
    TEST_F(Small_DynamicGraph, DropRegionStopsItsWork)
    {
        constexpr std::size_t N = 8;

        auto a = runtime().buffer<int>(N).value();
        auto b = runtime().buffer<int>(N).value();
        for (std::size_t i = 0; i < N; ++i) { a[i] = 0; b[i] = 0; }

        int* pa = a.data();
        int* pb = b.data();

        auto world = runtime().dynamic_graph();
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
    TEST_F(Small_DynamicGraph, AddRegionBetweenSteps)
    {
        constexpr std::size_t N = 8;

        auto a = runtime().buffer<int>(N).value();
        auto b = runtime().buffer<int>(N).value();
        for (std::size_t i = 0; i < N; ++i) { a[i] = 0; b[i] = 0; }

        int* pa = a.data();
        int* pb = b.data();

        auto world = runtime().dynamic_graph();
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

    /** @brief Tests that find_region() is not a mutation: it recomposes nothing. */
    TEST_F(Small_DynamicGraph, FindRegionRecomposesNothing)
    {
        constexpr std::size_t N = 4;

        auto a = runtime().buffer<int>(N).value();
        int* pa = a.data();
        for (std::size_t i = 0; i < N; ++i)
            a[i] = 0;

        auto world = runtime().dynamic_graph();
        world.region(0).add(N, Reads{}, Writes(a), [pa](std::size_t i) { pa[i] += 1; });
        world.run().value();
        const std::size_t composed = world.compose_count();

        const Region* found = world.find_region(0);
        ASSERT_NE(found, nullptr);
        EXPECT_EQ(found->size(), 1u);

        world.run().value();
        EXPECT_EQ(world.compose_count(), composed) << "a lookup is not a mutation";
        for (std::size_t i = 0; i < N; ++i)
            ASSERT_EQ(a[i], 2) << "at i=" << i;
    }

    /** @brief Tests that find_region() returns nullptr for an absent key and does not create it. */
    TEST_F(Small_DynamicGraph, FindRegionDoesNotCreateAnAbsentKey)
    {
        constexpr std::size_t N = 4;

        auto a = runtime().buffer<int>(N).value();
        int* pa = a.data();

        auto world = runtime().dynamic_graph();
        world.region(0).add(N, Reads{}, Writes(a), [pa](std::size_t i) { pa[i] += 1; });

        EXPECT_EQ(world.find_region(9), nullptr) << "an absent key is not created by a lookup";
        EXPECT_FALSE(world.has_region(9));
        EXPECT_EQ(world.region_count(), 1u);
    }

    /** @brief Tests that two regions reading one producer's buffer both see its writes. */
    TEST_F(Small_DynamicGraph, ReadReadSharingStaysConsistent)
    {
        constexpr std::size_t N = 24;

        auto base = runtime().buffer<int>(N).value();
        auto o1   = runtime().buffer<int>(N).value();
        auto o2   = runtime().buffer<int>(N).value();
        for (std::size_t i = 0; i < N; ++i) { base[i] = 0; o1[i] = 0; o2[i] = 0; }

        int* pb = base.data();
        int* p1 = o1.data();
        int* p2 = o2.data();

        auto world = runtime().dynamic_graph();
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

    /** @brief Tests that a consumer streamed in beside an existing producer sees its writes. */
    TEST_F(Small_DynamicGraph, AStreamedInConsumerSeesTheProducersWrites)
    {
        constexpr std::size_t N = 16;

        auto x = runtime().buffer<int>(N).value();
        auto y = runtime().buffer<int>(N).value();
        for (std::size_t i = 0; i < N; ++i) { x[i] = 0; y[i] = -1; }

        int* px = x.data();
        int* py = y.data();

        auto world = runtime().dynamic_graph();
        world.region(0).add(N, Reads{}, Writes(x),
                            [px](std::size_t i) { px[i] = static_cast<int>(i) + 1; });
        world.run().value();

        world.region(1).add(N, Reads(x), Writes(y),
                            [px, py](std::size_t i) { py[i] = px[i] * 3; });
        world.run().value();

        for (std::size_t i = 0; i < N; ++i)
            ASSERT_EQ(y[i], static_cast<int>(i + 1) * 3) << "at i=" << i;
    }

    /** @brief Tests that streaming a sharer in recomposes once. */
    TEST_F(Small_DynamicGraph, StreamingAConsumerInRecomposesOnce)
    {
        constexpr std::size_t N = 16;

        auto x = runtime().buffer<int>(N).value();
        auto y = runtime().buffer<int>(N).value();
        int* px = x.data();
        int* py = y.data();

        auto world = runtime().dynamic_graph();
        world.region(0).add(N, Reads{}, Writes(x),
                            [px](std::size_t i) { px[i] = static_cast<int>(i) + 1; });
        world.run().value();
        ASSERT_EQ(world.compose_count(), 1u);

        world.region(1).add(N, Reads(x), Writes(y),
                            [px, py](std::size_t i) { py[i] = px[i] * 3; });
        world.run().value();
        EXPECT_EQ(world.compose_count(), 2u) << "streaming a sharer in recomposes once.";
    }

    /** @brief Tests that dropping the producer leaves the consumer runnable with the values it had. */
    TEST_F(Small_DynamicGraph, DropProducerLeavesConsumerRunnable)
    {
        constexpr std::size_t N = 16;

        auto x = runtime().buffer<int>(N).value();
        auto y = runtime().buffer<int>(N).value();
        for (std::size_t i = 0; i < N; ++i) { x[i] = 7; y[i] = 0; }

        int* px = x.data();
        int* py = y.data();

        auto world = runtime().dynamic_graph();
        world.region(0).add(N, Reads{}, Writes(x),
                            [px](std::size_t i) { px[i] = static_cast<int>(i); });
        world.region(1).add(N, Reads(x), Writes(y),
                            [px, py](std::size_t i) { py[i] += px[i]; });
        world.run().value();

        world.drop(0);
        EXPECT_FALSE(world.has_region(0));
        world.run().value();                 // consumer alone; must not deadlock

        EXPECT_EQ(world.region_count(), 1u);
        for (std::size_t i = 0; i < N; ++i)
            ASSERT_EQ(y[i], static_cast<int>(i) * 2) << "at i=" << i;
    }

    /**
     * @brief Tests that two regions writing disjoint ranges of one buffer are not ordered against each
     *        other.
     */
    TEST_F(Medium_DynamicGraph, DisjointRangeRegionsAreNotOrdered)
    {
        if (std::thread::hardware_concurrency() < 2)
            GTEST_SKIP() << "needs at least two workers to observe concurrency";

        constexpr std::size_t N = 64;
        constexpr std::size_t HALF = N / 2;

        auto shared = runtime().buffer<int>(N).value();
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

        auto world = runtime().dynamic_graph();
        world.region(0).add_host(Reads{},
                                 Writes(shared.region({0, HALF})), rendezvous);
        world.region(1).add_host(Reads{},
                                 Writes(shared.region({HALF, HALF})), rendezvous);
        world.run().value();

        EXPECT_EQ(saw_both.load(), 2)
            << "the two regions were serialized through the allocation they share, "
               "although their element ranges are disjoint";
    }

    /**
     * @brief Tests that two regions writing the same range of one buffer are still ordered: starting
     *        the step posts only the first region's node, and the second sees what the first wrote.
     */
    TEST_F(Small_DynamicGraph, OverlappingRangeRegionsStillSerialize)
    {
        constexpr std::size_t N = 64;

        auto shared = runtime().buffer<int>(N).value();
        for (std::size_t i = 0; i < N; ++i) shared[i] = 0;
        int* p = shared.data();

        auto world = runtime().dynamic_graph();
        world.region(0).add_host(Reads{}, Writes(shared),
                                 [p]() { for (std::size_t i = 0; i < N; ++i) p[i] = 1; });
        world.region(1).add_host(Reads(shared), Writes(shared),
                                 [p]() { for (std::size_t i = 0; i < N; ++i) p[i] = p[i] * 2 + 1; });

        auto step = world.run_async().value();
        EXPECT_EQ(executor().pending(), 1u)
            << "the second region's node was a root: the cross-region edge was lost";
        step.wait().value();

        for (std::size_t i = 0; i < N; ++i)
            ASSERT_EQ(shared[i], 3) << "at i=" << i;
    }

    /** @brief Tests that a whole-buffer reader runs after both half writers. */
    TEST_F(Small_DynamicGraph, WholeBufferReaderOrdersAfterBothHalfWriters)
    {
        constexpr std::size_t N = 128;
        constexpr std::size_t HALF = N / 2;

        auto shared = runtime().buffer<int>(N).value();
        auto total  = runtime().buffer<int>(1).value();
        for (std::size_t i = 0; i < N; ++i) shared[i] = -1;
        total[0] = 0;

        int* p = shared.data();
        int* t = total.data();

        auto world = runtime().dynamic_graph();
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
} // namespace DynamicGraphTests

/** @brief Tests that a wide dynamic graph recomposes once per streamed mutation and keeps its counts. */
namespace WideDynamicGraphTests
{
    namespace
    {
        constexpr std::size_t K_CELLS     = 4;   // elements per region
        constexpr std::size_t K_REGIONS   = 32;  // regions in the world
        constexpr std::size_t K_MUTATIONS = 8;   // runs, each after one more task streamed in

        /**
         * @brief Names the region that mutation @p m streams a second task into.
         * @param m Index of the mutation.
         * @return Index of the region the mutation touches.
         */
        std::size_t mutated_region(std::size_t m)
        {
            return (m * 7 + 3) % K_REGIONS;
        }

        /**
         * @brief Makes one zeroed buffer per region.
         * @param rt Runtime the buffers belong to.
         * @return The buffers, indexed by region.
         */
        std::vector<Buffer<int>> make_zeroed_buffers(const Runtime& rt)
        {
            std::vector<Buffer<int>> bufs;
            bufs.reserve(K_REGIONS);
            for (std::size_t r = 0; r < K_REGIONS; ++r)
                bufs.push_back(rt.buffer<int>(K_CELLS).value());
            for (auto& buf : bufs)
                for (std::size_t i = 0; i < K_CELLS; ++i)
                    buf[i] = 0;
            return bufs;
        }

        /**
         * @brief Adds to region @p r a task that increments every cell of its buffer.
         * @param world Graph that owns the region.
         * @param bufs Buffers indexed by region.
         * @param r Region to add the task to.
         */
        void add_increment(DynamicGraph& world, std::vector<Buffer<int>>& bufs, std::size_t r)
        {
            int* p = bufs[r].data();
            world.region(r).add(K_CELLS, Reads{}, Writes(bufs[r]),
                                [p](std::size_t i) { p[i] += 1; });
        }

        /**
         * @brief Gives every region its first task and runs the world once.
         * @param world Graph to fill.
         * @param bufs Buffers indexed by region.
         */
        void compose_the_world_once(DynamicGraph& world, std::vector<Buffer<int>>& bufs)
        {
            for (std::size_t r = 0; r < K_REGIONS; ++r)
                add_increment(world, bufs, r);
            world.run(1).value();
        }

        /**
         * @brief Streams one task into a region and runs one step, K_MUTATIONS times.
         * @param world Graph that has composed once.
         * @param bufs Buffers indexed by region.
         * @return The number of runs after which the compose count was not one above the last.
         */
        std::size_t stream_mutations(DynamicGraph& world, std::vector<Buffer<int>>& bufs)
        {
            std::size_t wrong = 0;
            for (std::size_t m = 0; m < K_MUTATIONS; ++m)
            {
                add_increment(world, bufs, mutated_region(m));
                world.run(1).value();
                if (world.compose_count() != 2u + m)
                    ++wrong;
            }
            return wrong;
        }

        /**
         * @brief Counts the regions whose cells differ from the count their runs give them.
         * @param bufs Buffers indexed by region.
         * @return The number of regions with a wrong cell.
         */
        std::size_t count_regions_off_their_run_count(std::vector<Buffer<int>>& bufs)
        {
            std::size_t off = 0;
            for (std::size_t r = 0; r < K_REGIONS; ++r)
            {
                std::size_t extra = 0;
                for (std::size_t m = 0; m < K_MUTATIONS; ++m)
                    if (mutated_region(m) == r)
                        extra += K_MUTATIONS - m;
                const int expected = static_cast<int>(1 + K_MUTATIONS + extra);
                for (std::size_t i = 0; i < K_CELLS; ++i)
                    if (bufs[r][i] != expected)
                    {
                        ++off;
                        break;
                    }
            }
            return off;
        }
    } // namespace

    /** @brief Fixture of the wide-graph tests, which run on a host runtime. */
    class Small_WideDynamicGraph : public TestSupport::HostRuntimeTest
    {
    };

    /** @brief Tests that each of several streamed mutations recomposes the world exactly once. */
    TEST_F(Small_WideDynamicGraph, EachOfSeveralStreamedMutationsRecomposesOnce)
    {
        auto bufs = make_zeroed_buffers(runtime());
        auto world = runtime().dynamic_graph();
        compose_the_world_once(world, bufs);

        EXPECT_EQ(stream_mutations(world, bufs), 0u);
    }

    /** @brief Tests that after streamed mutations every region holds the count its runs give it. */
    TEST_F(Small_WideDynamicGraph, EveryRegionKeepsItsRunCountAfterStreamedMutations)
    {
        auto bufs = make_zeroed_buffers(runtime());
        auto world = runtime().dynamic_graph();
        compose_the_world_once(world, bufs);
        stream_mutations(world, bufs);

        EXPECT_EQ(count_regions_off_their_run_count(bufs), 0u);
    }
} // namespace WideDynamicGraphTests
