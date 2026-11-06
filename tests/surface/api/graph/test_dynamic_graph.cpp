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
 * @file test_dynamic_graph.cpp
 * @brief Tests the DynamicGraph: its regions, its node events and its agreement with a static Graph.
 * @see tests/surface/api/graph/README.md
 * @author Mustafa Garip
 */

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <gtest/gtest.h>
#include <sycl/sycl.hpp>

#include <SushiRuntime/sushiruntime.hpp>

#include "run_recorder.hpp"
#include "support/runtime_environment.hpp"

using namespace SushiRuntime;

/** @brief Tests the region-partitioned graph that Runtime::dynamic_graph() returns. */
namespace DynamicGraphTests
{
    /** @brief Fixture of the region tests, which run on the program's runtime with the rebalancer off. */
    class Medium_DynamicGraph : public TestSupport::RuntimeTest
    {
        protected:
            /** @brief Turns the background rebalancer off, so no thermal move reorders a region. */
            void SetUp() override { runtime().set_rebalancer_enabled(false); }
    };

    /** @brief Fixture of the rows over the number of regions a chain threads a value through. */
    class Medium_DynamicGraphChain : public Medium_DynamicGraph,
                                     public ::testing::WithParamInterface<std::size_t>
    {
    };

    /**
     * @brief Fixture of a world of 32 regions that each add one to their own buffer and have run three steps.
     */
    class Medium_DynamicGraphWorld : public Medium_DynamicGraph
    {
        protected:
            static constexpr std::size_t N       = 4;
            static constexpr std::size_t K_REGIONS = 32;

            /** @brief Records the 32 regions and runs three steps. */
            void SetUp() override
            {
                Medium_DynamicGraph::SetUp();
                bufs_.reserve(K_REGIONS);
                for (std::size_t r = 0; r < K_REGIONS; ++r)
                    bufs_.push_back(runtime().buffer<int>(N).value());
                for (auto& buf : bufs_)
                    for (std::size_t i = 0; i < N; ++i)
                        buf[i] = 0;

                world_.emplace(runtime().dynamic_graph());
                for (std::size_t r = 0; r < K_REGIONS; ++r)
                {
                    int* p = bufs_[r].data();
                    world_->region(r).add(N, Reads{}, Writes(bufs_[r]),
                                          [p](std::size_t i) { p[i] += 1; });
                }

                world_->run(3).value();
            }

            /** @brief Adds ten to every cell of region 7 and runs one step. */
            void mutate_region_seven_and_run()
            {
                int* p7 = bufs_[7].data();
                world_->region(7).add(N, Reads{}, Writes(bufs_[7]),
                                      [p7](std::size_t i) { p7[i] += 10; });
                world_->run(1).value();
            }

            std::vector<Buffer<int>> bufs_;    /**< One buffer per region. */
            std::optional<DynamicGraph> world_; /**< The graph of the 32 regions. */
    };

    /** @brief Tests that two independent regions advance their own buffers over a multi-step run. */
    TEST_F(Medium_DynamicGraph, IndependentRegionsAdvanceTheirOwnBuffers)
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
    TEST_F(Medium_DynamicGraph, AnUnmutatedMultiStepRunComposesOnce)
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
    TEST_P(Medium_DynamicGraphChain, ARegionObservesTheWritesOfTheRegionBeforeIt)
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

    INSTANTIATE_TEST_SUITE_P(Rows, Medium_DynamicGraphChain, ::testing::Values(2u, 3u));

    /**
     * @brief Tests that a dropped region's buffer freezes while the surviving region keeps advancing.
     */
    TEST_F(Medium_DynamicGraph, DropRegionStopsItsWork)
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
    TEST_F(Medium_DynamicGraph, AddRegionBetweenSteps)
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

    /** @brief Tests that building all regions composes once. */
    TEST_F(Medium_DynamicGraphWorld, BuildingAllRegionsComposesOnce)
    {
        EXPECT_EQ(world_->compose_count(), 1u) << "building all regions once composes once.";
    }

    /** @brief Tests that one mutation recomposes exactly once, not once per region. */
    TEST_F(Medium_DynamicGraphWorld, OneMutationRecomposesExactlyOnce)
    {
        mutate_region_seven_and_run();

        EXPECT_EQ(world_->compose_count(), 2u) << "one mutation recomposes exactly once.";
    }

    /** @brief Tests that after one mutation every region holds the count its runs give it. */
    TEST_F(Medium_DynamicGraphWorld, EveryRegionHoldsItsRunCountAfterAMutation)
    {
        mutate_region_seven_and_run();

        for (std::size_t r = 0; r < K_REGIONS; ++r)
            for (std::size_t i = 0; i < N; ++i)
            {
                const int expected = (r == 7) ? (4 + 10) : 4;  // 3 steps + 1 step (+10 on its 4th)
                ASSERT_EQ(bufs_[r][i], expected) << "region " << r << " at i=" << i;
            }
    }

    /** @brief Tests that find_region() is not a mutation: it recomposes nothing. */
    TEST_F(Medium_DynamicGraph, FindRegionRecomposesNothing)
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
    TEST_F(Medium_DynamicGraph, FindRegionDoesNotCreateAnAbsentKey)
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
    TEST_F(Medium_DynamicGraph, ReadReadSharingStaysConsistent)
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
    TEST_F(Medium_DynamicGraph, AStreamedInConsumerSeesTheProducersWrites)
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
    TEST_F(Medium_DynamicGraph, StreamingAConsumerInRecomposesOnce)
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
    TEST_F(Medium_DynamicGraph, DropProducerLeavesConsumerRunnable)
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

    /** @brief Tests that two regions writing the same range of one buffer are still ordered. */
    TEST_F(Medium_DynamicGraph, OverlappingRangeRegionsStillSerialize)
    {
        constexpr std::size_t N = 64;

        auto shared = runtime().buffer<int>(N).value();
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

        auto world = runtime().dynamic_graph();
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
    TEST_F(Medium_DynamicGraph, WholeBufferReaderOrdersAfterBothHalfWriters)
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

    /** @brief Fixture of the wide-graph tests, which run on the program's runtime with the rebalancer off. */
    class Medium_WideDynamicGraph : public TestSupport::RuntimeTest
    {
        protected:
            /** @brief Turns the background rebalancer off, so no thermal move reorders a region. */
            void SetUp() override { runtime().set_rebalancer_enabled(false); }
    };

    /** @brief Tests that each of several streamed mutations recomposes the world exactly once. */
    TEST_F(Medium_WideDynamicGraph, EachOfSeveralStreamedMutationsRecomposesOnce)
    {
        auto bufs = make_zeroed_buffers(runtime());
        auto world = runtime().dynamic_graph();
        compose_the_world_once(world, bufs);

        EXPECT_EQ(stream_mutations(world, bufs), 0u);
    }

    /** @brief Tests that after streamed mutations every region holds the count its runs give it. */
    TEST_F(Medium_WideDynamicGraph, EveryRegionKeepsItsRunCountAfterStreamedMutations)
    {
        auto bufs = make_zeroed_buffers(runtime());
        auto world = runtime().dynamic_graph();
        compose_the_world_once(world, bufs);
        stream_mutations(world, bufs);

        EXPECT_EQ(count_regions_off_their_run_count(bufs), 0u);
    }
} // namespace WideDynamicGraphTests

/** @brief Tests DynamicGraph's per-graph profiling switch and its dense registration index. */
namespace DynamicGraphNodeEventsTests
{
    namespace
    {
        constexpr std::size_t N = 16;

        /** @brief Records a region that fills one buffer with its index, under the given label. */
        void add_labelled_fill(DynamicGraph& world, DynamicGraph::RegionKey key, Buffer<int>& buffer,
                               int scale, const char* label)
        {
            int* p = buffer.data();
            world.region(key).add(N, Reads{}, Writes(buffer),
                                  [p, scale](std::size_t i) { p[i] = static_cast<int>(i) * scale; })
                .name(label);
        }
    } // namespace

    /** @brief Fixture of the node-event tests, which run on the program's runtime with the rebalancer off. */
    class Medium_DynamicGraphNodeEvents : public TestSupport::RuntimeTest
    {
        protected:
            /** @brief Turns the background rebalancer off, so no thermal move reorders a region. */
            void SetUp() override { runtime().set_rebalancer_enabled(false); }
    };

    /** @brief Tests that every record of an observed run names a node the table of the run describes. */
    TEST_F(Medium_DynamicGraphNodeEvents, RequestedGraphProducesRecordsMatchingRenumberedIndex)
    {
        Testing::RunRecorder observed(runtime());
        auto a = runtime().buffer<int>(N).value();
        auto b = runtime().buffer<int>(N).value();

        auto world = runtime().dynamic_graph();
        add_labelled_fill(world, 0, a, 1, "A");
        add_labelled_fill(world, 1, b, 1, "B");

        world.run().value();
        ASSERT_EQ(observed.count(), 1u);
        const RunObservation observation = observed.last();
        ASSERT_FALSE(observation.events.empty())
            << "an observed DynamicGraph must record device dispatches, not just stamp the flag";
        ASSERT_NE(observation.table, nullptr);

        for (const auto& rec : observation.events)
        {
            const NodeTableRow* row = observation.table->find(rec.node_index);
            ASSERT_NE(row, nullptr) << "record node_index " << rec.node_index
                                    << " names no row of the table";
            EXPECT_TRUE(row->name == "A" || row->name == "B") << row->name;
        }
    }

    /** @brief Tests that the table indices are dense and ordered by ascending region key, then add() order. */
    TEST_F(Medium_DynamicGraphNodeEvents, IndexDenseAndOrderedByRegionThenAddOrder)
    {
        Testing::RunRecorder observed(runtime());
        auto a = runtime().buffer<int>(N).value();
        auto b = runtime().buffer<int>(N).value();
        auto c = runtime().buffer<int>(N).value();

        auto world = runtime().dynamic_graph();

        // Region 5 is keyed above region 2, so its two calls land after region 2's one; see README.md.
        add_labelled_fill(world, 5, a, 1, "R5_first");
        add_labelled_fill(world, 5, b, 1, "R5_second");
        add_labelled_fill(world, 2, c, 1, "R2_only");

        world.run().value();
        ASSERT_EQ(observed.count(), 1u);
        const auto& rows = observed.last().table->rows();

        ASSERT_EQ(rows.size(), 3u);
        for (std::size_t k = 0; k < rows.size(); ++k)
            EXPECT_EQ(rows[k].index, k) << "the indices are not dense";
        EXPECT_EQ(rows[0].name, "R2_only") << "region 2 sorts before region 5 by key, whatever add() order";
        EXPECT_EQ(rows[1].name, "R5_first");
        EXPECT_EQ(rows[2].name, "R5_second");
    }

    /** @brief Tests that the index stays dense after a region is dropped and re-added. */
    TEST_F(Medium_DynamicGraphNodeEvents, IndexStaysDenseAfterDropAndReAdd)
    {
        Testing::RunRecorder observed(runtime());
        auto a = runtime().buffer<int>(N).value();
        auto b = runtime().buffer<int>(N).value();

        auto world = runtime().dynamic_graph();

        add_labelled_fill(world, 0, a, 1, "kept");
        add_labelled_fill(world, 1, b, 1, "dropped_v1");
        world.run().value();

        world.drop(1);
        add_labelled_fill(world, 1, b, 2, "dropped_v2");
        world.run().value();

        ASSERT_EQ(observed.count(), 2u);
        const auto& rows = observed.last().table->rows();

        EXPECT_EQ(world.region_count(), 2u);
        ASSERT_EQ(rows.size(), 2u);
        EXPECT_EQ(rows[0].name, "kept");
        EXPECT_EQ(rows[0].index, 0u);
        EXPECT_EQ(rows[1].name, "dropped_v2");
        EXPECT_EQ(rows[1].index, 1u) << "re-adding region 1 must not resume from a stale counter";
    }
} // namespace DynamicGraphNodeEventsTests

/** @brief Tests that graph shapes leave the bytes a host model of their work leaves, on the chain and on the scheduler. */
namespace LayerEquivalenceTests
{
    namespace
    {
        /** @brief Names the element type of every buffer here; unsigned, so overflow wraps. */
        using Word = std::uint32_t;

        /** @brief Names one output buffer copied to the host. */
        using Image = std::vector<Word>;

        constexpr std::size_t K_TILE_N = 512;
        constexpr std::size_t K_HOST_N = 256;
        constexpr std::size_t K_STEPS  = 5;
        constexpr std::size_t K_RUNS   = 3;

        constexpr std::size_t K_GRID_ROWS = 12;
        constexpr std::size_t K_GRID_COLS = 16;
        constexpr std::size_t K_GRID_N    = K_GRID_ROWS * K_GRID_COLS;
        constexpr std::size_t K_BOX_ROWS  = 6;
        constexpr std::size_t K_BOX_COLS  = 8;
        constexpr std::size_t K_BOX_N     = K_BOX_ROWS * K_BOX_COLS;
        constexpr std::size_t K_BOX_ROW0  = 3;
        constexpr std::size_t K_BOX_COL0  = 5;
        constexpr std::size_t K_TILE_ROWS = 3;
        constexpr std::size_t K_TILE_COLS = 4;

        constexpr DynamicGraph::RegionKey K_KEY_SCALE = 10;
        constexpr DynamicGraph::RegionKey K_KEY_MIX   = 20;
        constexpr DynamicGraph::RegionKey K_KEY_FOLD  = 30;
        constexpr DynamicGraph::RegionKey K_KEY_FIELD = 40;

        /** @brief Holds every output of one workload run, copied out, in a fixed order. */
        struct Snapshot
        {
            std::vector<const char*> names; /**< One label per image, for failure messages. */
            std::vector<Image> images;      /**< The output buffers, in the order they were taken. */
        };

        /** @brief Appends one named output to a snapshot. */
        void take(Snapshot& shot, const char* name, Image image)
        {
            shot.names.push_back(name);
            shot.images.push_back(std::move(image));
        }

        /** @brief Returns the index of the first element whose bytes differ, or the common size. */
        std::size_t first_difference(const Image& a, const Image& b)
        {
            const std::size_t n = (a.size() < b.size()) ? a.size() : b.size();
            for (std::size_t i = 0; i < n; ++i)
                if (std::memcmp(&a[i], &b[i], sizeof(Word)) != 0)
                    return i;
            return n;
        }

        /**
         * @brief Asserts two snapshots hold the same bytes, image by image.
         * @param pairing Names the two runs compared, for the failure message.
         */
        void expect_identical(const Snapshot& expected, const Snapshot& actual, const char* pairing)
        {
            ASSERT_EQ(expected.images.size(), actual.images.size()) << pairing;
            for (std::size_t k = 0; k < expected.images.size(); ++k)
            {
                const Image& a = expected.images[k];
                const Image& b = actual.images[k];
                ASSERT_EQ(a.size(), b.size()) << pairing << ": " << expected.names[k];
                EXPECT_EQ(std::memcmp(a.data(), b.data(), a.size() * sizeof(Word)), 0)
                    << pairing << ": " << expected.names[k] << " differs first at element "
                    << first_difference(a, b);
            }
        }

        /** @brief Says how a graph is stepped. */
        enum class Drive : std::uint8_t
        {
            Blocking,     /**< One run(steps) call. */
            Asynchronous, /**< One run_async() and wait() per step. */
        };

        /**
         * @brief Steps a graph the given way.
         * @tparam Runnable Graph or DynamicGraph.
         */
        template <class Runnable>
        void drive(Runnable& graph, Drive how, std::size_t steps)
        {
            if (how == Drive::Blocking)
            {
                const RunReport report = graph.run(steps).value();
                EXPECT_TRUE(report.is_successful);
                return;
            }
            for (std::size_t s = 0; s < steps; ++s)
                graph.run_async().value().wait().value();
        }

        /** @brief Owns the data of the tile workload: three chained buffers and one State. */
        struct Tiles
        {
            /** @brief Allocates the data on @p rt and writes the starting values. */
            explicit Tiles(const Runtime& rt)
                : scale(rt.buffer<Word>(K_TILE_N).value()),
                  mix(rt.buffer<Word>(K_TILE_N).value()),
                  fold(rt.buffer<Word>(K_TILE_N).value()),
                  field(rt.state<Word>(K_TILE_N).value())
            {
                for (std::size_t i = 0; i < K_TILE_N; ++i)
                {
                    scale[i] = static_cast<Word>(i) * 3u + 1u;
                    mix[i]   = static_cast<Word>(i) ^ 0x9E3779B9u;
                    fold[i]  = 0u;
                    field[i] = static_cast<Word>(i) + 11u;
                }
            }

            Buffer<Word> scale; /**< Rewritten from itself every step. */
            Buffer<Word> mix;   /**< Reads scale, so it orders after it. */
            Buffer<Word> fold;  /**< Reads scale and mix, so it orders last. */
            State<Word> field;  /**< Evolves on its own, independent of the buffers. */
        };

        /** @brief Copies every tile output into a snapshot. */
        void take_tiles(Snapshot& shot, const Tiles& tiles)
        {
            take(shot, "scale", Image(tiles.scale.data(), tiles.scale.data() + K_TILE_N));
            take(shot, "mix", Image(tiles.mix.data(), tiles.mix.data() + K_TILE_N));
            take(shot, "fold", Image(tiles.fold.data(), tiles.fold.data() + K_TILE_N));
            take(shot, "field", Image(tiles.field.current(), tiles.field.current() + K_TILE_N));
        }

        /**
         * @brief Records the kernel that rewrites the scale buffer.
         * @tparam Builder Graph or Region.
         */
        template <class Builder>
        void record_scale(Builder& builder, Tiles& tiles)
        {
            Word* scale = tiles.scale.data();
            builder.add(K_TILE_N, Reads(tiles.scale), Writes(tiles.scale),
                        [scale](std::size_t i)
                        {
                            scale[i] = scale[i] * 31u + static_cast<Word>(i) + 1u;
                        });
        }

        /**
         * @brief Records the kernel that mixes scale into the mix buffer.
         * @tparam Builder Graph or Region.
         */
        template <class Builder>
        void record_mix(Builder& builder, Tiles& tiles)
        {
            const Word* scale = tiles.scale.data();
            Word* mix = tiles.mix.data();
            builder.add(K_TILE_N, Reads(tiles.scale), Writes(tiles.mix),
                        [scale, mix](std::size_t i)
                        {
                            mix[i] = (mix[i] ^ (scale[i] * 2654435761u)) + 7u;
                        });
        }

        /**
         * @brief Records the kernel that folds scale and mix into the fold buffer.
         * @tparam Builder Graph or Region.
         */
        template <class Builder>
        void record_fold(Builder& builder, Tiles& tiles)
        {
            const Word* scale = tiles.scale.data();
            const Word* mix = tiles.mix.data();
            Word* fold = tiles.fold.data();
            builder.add(K_TILE_N, Reads(tiles.scale, tiles.mix), Writes(tiles.fold),
                        [scale, mix, fold](std::size_t i)
                        {
                            fold[i] = fold[i] * 17u + scale[i] + mix[i];
                        });
        }

        /**
         * @brief Records the per-step update rule of the State.
         * @tparam Builder Graph or Region.
         */
        template <class Builder>
        void record_field(Builder& builder, Tiles& tiles)
        {
            builder.add_rule(tiles.field, [](std::size_t i, const Word* cur, Word* next)
            {
                next[i] = cur[i] * 5u + static_cast<Word>(i % 7);
            });
        }

        /** @brief Describes one stretch of a tile run with unchanged recorded work. */
        struct Segment
        {
            bool mix_live;    /**< Whether the mix kernel is part of the work. */
            std::size_t runs; /**< Number of drive() calls, each of K_STEPS steps. */
        };

        /** @brief Describes a whole tile run. */
        struct Script
        {
            std::vector<Segment> segments; /**< The stretches, in order, on the same data. */
            bool evolve_field;             /**< Whether the State rule is recorded. */
        };

        /** @brief Runs one segment as a static graph built for exactly that segment's work. */
        void run_static_segment(const Runtime& rt, Tiles& tiles, const Segment& segment,
                                bool evolve_field, Drive how)
        {
            auto g = rt.graph();
            record_scale(g, tiles);
            if (segment.mix_live)
                record_mix(g, tiles);
            record_fold(g, tiles);
            if (evolve_field)
                record_field(g, tiles);

            for (std::size_t r = 0; r < segment.runs; ++r)
                drive(g, how, K_STEPS);
        }

        /** @brief Runs a script with one static graph per segment and copies the results out. */
        Snapshot run_static_tiles(const Runtime& rt, const Script& script, Drive how)
        {
            Tiles tiles(rt);
            Snapshot shot;
            for (const Segment& segment : script.segments)
                run_static_segment(rt, tiles, segment, script.evolve_field, how);
            take_tiles(shot, tiles);
            return shot;
        }

        /**
         * @brief Runs a script on one DynamicGraph, one region per kernel, and copies the results out.
         *
         * The mix region is dropped or recorded again where a segment changes it.
         */
        Snapshot run_dynamic_tiles(const Runtime& rt, const Script& script, Drive how)
        {
            Tiles tiles(rt);
            Snapshot shot;

            // Region keys ascend in the order the static graph adds the same kernels.
            auto world = rt.dynamic_graph();
            record_scale(world.region(K_KEY_SCALE), tiles);
            record_fold(world.region(K_KEY_FOLD), tiles);
            if (script.evolve_field)
                record_field(world.region(K_KEY_FIELD), tiles);

            for (const Segment& segment : script.segments)
            {
                if (segment.mix_live && !world.has_region(K_KEY_MIX))
                    record_mix(world.region(K_KEY_MIX), tiles);
                if (!segment.mix_live)
                    world.drop(K_KEY_MIX);

                for (std::size_t r = 0; r < segment.runs; ++r)
                    drive(world, how, K_STEPS);
            }

            take_tiles(shot, tiles);
            return shot;
        }

        /** @brief Folds one buffer into another, element by element. */
        struct MixPass
        {
            const Word* from; /**< K_HOST_N elements read. */
            Word* into;       /**< K_HOST_N elements rewritten. */

            /** @brief Runs the whole pass on the calling thread or work-item. */
            void apply() const noexcept
            {
                for (std::size_t i = 0; i < K_HOST_N; ++i)
                    into[i] = (into[i] ^ (from[i] * 2654435761u)) + 7u;
            }
        };

        /** @brief Folds a running value over one buffer into another, in index order. */
        struct ScanPass
        {
            const Word* from; /**< K_HOST_N elements read in index order. */
            Word* into;       /**< K_HOST_N elements rewritten. */

            /** @brief Runs the whole pass on the calling thread or work-item. */
            void apply() const noexcept
            {
                Word running = 0u;
                for (std::size_t i = 0; i < K_HOST_N; ++i)
                {
                    running = running * 33u + from[i];
                    into[i] ^= running;
                }
            }
        };

        /** @brief Says where a whole-buffer pass runs. */
        enum class PassSite : std::uint8_t
        {
            Host,   /**< As a host task, through add_host(). */
            Device, /**< As a one-item device kernel, through add(). */
        };

        /**
         * @brief Records one whole-buffer pass as a host task or as a one-item kernel.
         * @tparam Pass A trivially copyable type with a const apply().
         */
        template <class Pass>
        void record_pass(Graph& g, const Reads& reads, const Writes& writes,
                         Pass pass, PassSite site)
        {
            if (site == PassSite::Host)
            {
                g.add_host(reads, writes, [pass] { pass.apply(); });
                return;
            }
            g.add(std::size_t{1}, reads, writes, [pass](std::size_t) { pass.apply(); });
        }

        /**
         * @brief Runs a five-task dependency chain in which two passes sit between device kernels.
         * @param site Where the two passes run; the three kernels always run on the device.
         */
        Snapshot run_pass_chain(const Runtime& rt, PassSite site)
        {
            auto a = rt.buffer<Word>(K_HOST_N).value();
            auto b = rt.buffer<Word>(K_HOST_N).value();
            auto c = rt.buffer<Word>(K_HOST_N).value();
            auto d = rt.buffer<Word>(K_HOST_N).value();
            for (std::size_t i = 0; i < K_HOST_N; ++i)
            {
                a[i] = static_cast<Word>(i) * 7u + 3u;
                b[i] = static_cast<Word>(i) ^ 0x85EBCA6Bu;
                c[i] = 1u;
                d[i] = 0u;
            }

            Word* pa = a.data();
            Word* pb = b.data();
            Word* pc = c.data();
            Word* pd = d.data();

            auto g = rt.graph();
            g.add(K_HOST_N, Reads(a), Writes(a),
                  [pa](std::size_t i) { pa[i] = pa[i] * 31u + static_cast<Word>(i) + 1u; });
            record_pass(g, Reads(a), Writes(b), MixPass{pa, pb}, site);
            g.add(K_HOST_N, Reads(b), Writes(c),
                  [pb, pc](std::size_t i) { pc[i] = pc[i] * 17u + pb[i]; });
            record_pass(g, Reads(c), Writes(d), ScanPass{pc, pd}, site);
            g.add(K_HOST_N, Reads(a, d), Writes(a),
                  [pa, pd](std::size_t i) { pa[i] += pd[i]; });

            for (std::size_t r = 0; r < K_RUNS; ++r)
                drive(g, Drive::Blocking, K_STEPS);

            Snapshot shot;
            take(shot, "a", Image(pa, pa + K_HOST_N));
            take(shot, "b", Image(pb, pb + K_HOST_N));
            take(shot, "c", Image(pc, pc + K_HOST_N));
            take(shot, "d", Image(pd, pd + K_HOST_N));
            return shot;
        }

        /** @brief Returns the bytes the tile workload leaves, computed on the host in the order the kernels are recorded. */
        Snapshot tile_reference(const Script& script)
        {
            Image scale(K_TILE_N);
            Image mix(K_TILE_N);
            Image fold(K_TILE_N);
            Image field(K_TILE_N);
            for (std::size_t i = 0; i < K_TILE_N; ++i)
            {
                scale[i] = static_cast<Word>(i) * 3u + 1u;
                mix[i]   = static_cast<Word>(i) ^ 0x9E3779B9u;
                fold[i]  = 0u;
                field[i] = static_cast<Word>(i) + 11u;
            }

            for (const Segment& segment : script.segments)
                for (std::size_t step = 0; step < segment.runs * K_STEPS; ++step)
                {
                    for (std::size_t i = 0; i < K_TILE_N; ++i)
                        scale[i] = scale[i] * 31u + static_cast<Word>(i) + 1u;
                    if (segment.mix_live)
                        for (std::size_t i = 0; i < K_TILE_N; ++i)
                            mix[i] = (mix[i] ^ (scale[i] * 2654435761u)) + 7u;
                    for (std::size_t i = 0; i < K_TILE_N; ++i)
                        fold[i] = fold[i] * 17u + scale[i] + mix[i];
                    if (script.evolve_field)
                        for (std::size_t i = 0; i < K_TILE_N; ++i)
                            field[i] = field[i] * 5u + static_cast<Word>(i % 7);
                }

            Snapshot shot;
            take(shot, "scale", scale);
            take(shot, "mix", mix);
            take(shot, "fold", fold);
            take(shot, "field", field);
            return shot;
        }

        /** @brief Returns the bytes the pass chain leaves after K_RUNS runs of K_STEPS steps, computed on the host. */
        Snapshot pass_chain_reference()
        {
            Image a(K_HOST_N);
            Image b(K_HOST_N);
            Image c(K_HOST_N);
            Image d(K_HOST_N);
            for (std::size_t i = 0; i < K_HOST_N; ++i)
            {
                a[i] = static_cast<Word>(i) * 7u + 3u;
                b[i] = static_cast<Word>(i) ^ 0x85EBCA6Bu;
                c[i] = 1u;
                d[i] = 0u;
            }

            for (std::size_t step = 0; step < K_RUNS * K_STEPS; ++step)
            {
                for (std::size_t i = 0; i < K_HOST_N; ++i)
                    a[i] = a[i] * 31u + static_cast<Word>(i) + 1u;
                MixPass{a.data(), b.data()}.apply();
                for (std::size_t i = 0; i < K_HOST_N; ++i)
                    c[i] = c[i] * 17u + b[i];
                ScanPass{c.data(), d.data()}.apply();
                for (std::size_t i = 0; i < K_HOST_N; ++i)
                    a[i] += d[i];
            }

            Snapshot shot;
            take(shot, "a", a);
            take(shot, "b", b);
            take(shot, "c", c);
            take(shot, "d", d);
            return shot;
        }

        /** @brief Returns the read-only input word of a grid cell. */
        constexpr Word source_word(std::size_t cell) noexcept
        {
            return static_cast<Word>(cell) * 2246822519u + 5u;
        }

        /** @brief Returns the starting word of a grid cell in every output. */
        constexpr Word seed_word(std::size_t cell) noexcept
        {
            return static_cast<Word>(cell) ^ 0xC2B2AE35u;
        }

        /** @brief Returns the next value of a cell from its previous value, its input and its index. */
        constexpr Word stamp(Word previous, Word input, std::size_t cell) noexcept
        {
            return previous * 1664525u + (input ^ static_cast<Word>(cell)) + 1013904223u;
        }

        /** @brief Advances one grid cell in place, addressed by absolute row and column. */
        inline void stamp_cell(const Word* in, Word* out, std::size_t row, std::size_t col) noexcept
        {
            const std::size_t cell = row * K_GRID_COLS + col;
            out[cell] = stamp(out[cell], in[cell], cell);
        }

        /** @brief Returns the launch box given by a range and an origin. */
        Extent<2> box_by_origin()
        {
            return Extent<2>(sycl::range<2>(K_BOX_ROWS, K_BOX_COLS), sycl::id<2>(K_BOX_ROW0, K_BOX_COL0));
        }

        /** @brief Returns the same launch box given by sizes and rebased with at(). */
        Extent<2> box_by_rebase()
        {
            return Extent{K_BOX_ROWS, K_BOX_COLS}.at(sycl::id<2>(K_BOX_ROW0, K_BOX_COL0));
        }

        /** @brief Returns the box-sized tiled launch; NdRange carries no origin. */
        NdRange<2> box_tiles()
        {
            return NdRange{sycl::range<2>(K_BOX_ROWS, K_BOX_COLS), sycl::range<2>(K_TILE_ROWS, K_TILE_COLS)};
        }

        /** @brief Returns the grid a host loop reaches after @p steps steps over the box. */
        Image reference_grid(std::size_t steps)
        {
            Image source(K_GRID_N);
            Image grid(K_GRID_N);
            for (std::size_t k = 0; k < K_GRID_N; ++k)
            {
                source[k] = source_word(k);
                grid[k]   = seed_word(k);
            }
            for (std::size_t s = 0; s < steps; ++s)
                for (std::size_t row = K_BOX_ROW0; row < K_BOX_ROW0 + K_BOX_ROWS; ++row)
                    for (std::size_t col = K_BOX_COL0; col < K_BOX_COL0 + K_BOX_COLS; ++col)
                        stamp_cell(source.data(), grid.data(), row, col);
            return grid;
        }

        /** @brief Returns the box cells of a grid, packed row by row. */
        Image box_of(const Image& grid)
        {
            Image box(K_BOX_N);
            for (std::size_t row = 0; row < K_BOX_ROWS; ++row)
                for (std::size_t col = 0; col < K_BOX_COLS; ++col)
                    box[row * K_BOX_COLS + col] = grid[(row + K_BOX_ROW0) * K_GRID_COLS + col + K_BOX_COL0];
            return box;
        }

        /** @brief Allocates a grid-sized buffer on a device of @p rt holding the starting words. */
        Buffer<Word> seeded_grid(const Runtime& rt, DeviceIndex device = DeviceIndex{0})
        {
            auto grid = rt.buffer<Word>(K_GRID_N, device).value();
            for (std::size_t k = 0; k < K_GRID_N; ++k)
                grid[k] = seed_word(k);
            return grid;
        }

        /** @brief Allocates the read-only input grid on a device of @p rt. */
        Buffer<Word> source_grid(const Runtime& rt, DeviceIndex device = DeviceIndex{0})
        {
            auto grid = rt.buffer<Word>(K_GRID_N, device).value();
            for (std::size_t k = 0; k < K_GRID_N; ++k)
                grid[k] = source_word(k);
            return grid;
        }

        /**
         * @brief Runs the offset box through every tracked launch form, each writing its own output.
         *
         * The State is box-sized: its slot is the cell's position inside the box.
         */
        Snapshot run_tracked_launches(const Runtime& rt)
        {
            auto source     = source_grid(rt);
            auto by_origin  = seeded_grid(rt);
            auto by_rebase  = seeded_grid(rt);
            auto by_capture = seeded_grid(rt);
            auto by_tiles   = seeded_grid(rt);
            auto field      = rt.state<Word>(K_BOX_N).value();

            const Image seeded_box = box_of(reference_grid(0));
            for (std::size_t slot = 0; slot < K_BOX_N; ++slot)
                field[slot] = seeded_box[slot];

            const Word* in = source.data();
            Word* origin_out = by_origin.data();
            Word* rebase_out = by_rebase.data();
            Word* tiles_out  = by_tiles.data();

            auto g = rt.graph();
            g.add(box_by_origin(), Reads(source, by_origin), Writes(by_origin),
                  [in, origin_out](sycl::id<2> idx) { stamp_cell(in, origin_out, idx[0], idx[1]); });
            g.add(box_by_rebase(), Reads(source, by_rebase), Writes(by_rebase),
                  [in, rebase_out](sycl::id<2> idx) { stamp_cell(in, rebase_out, idx[0], idx[1]); });
            g.add(box_by_origin(), In(source), InOut(by_capture),
                  [](sycl::id<2> idx, const Word* captured_in, Word* captured_out)
                  {
                      stamp_cell(captured_in, captured_out, idx[0], idx[1]);
                  });
            g.add_rule(box_by_origin(), field, [in](sycl::id<2> idx, const Word* cur, Word* next)
            {
                const std::size_t cell = idx[0] * K_GRID_COLS + idx[1];
                const std::size_t slot = (idx[0] - K_BOX_ROW0) * K_BOX_COLS + (idx[1] - K_BOX_COL0);
                next[slot] = stamp(cur[slot], in[cell], cell);
            });
            g.add(box_tiles(), Reads(source, by_tiles), Writes(by_tiles),
                  [in, tiles_out](sycl::nd_item<2> item)
                  {
                      stamp_cell(in, tiles_out, item.get_global_id(0) + K_BOX_ROW0,
                                 item.get_global_id(1) + K_BOX_COL0);
                  });

            drive(g, Drive::Blocking, K_STEPS);

            Snapshot shot;
            take(shot, "Extent(range, origin)", Image(origin_out, origin_out + K_GRID_N));
            take(shot, "Extent{sizes}.at(origin)", Image(rebase_out, rebase_out + K_GRID_N));
            take(shot, "Extent with In/InOut", Image(by_capture.data(), by_capture.data() + K_GRID_N));
            take(shot, "NdRange", Image(tiles_out, tiles_out + K_GRID_N));
            take(shot, "State over Extent", Image(field.current(), field.current() + K_BOX_N));
            return shot;
        }

        /** @brief Returns what run_tracked_launches() must produce, computed on the host. */
        Snapshot tracked_launch_reference()
        {
            const Image grid = reference_grid(K_STEPS);
            Snapshot shot;
            take(shot, "Extent(range, origin)", grid);
            take(shot, "Extent{sizes}.at(origin)", grid);
            take(shot, "Extent with In/InOut", grid);
            take(shot, "NdRange", grid);
            take(shot, "State over Extent", box_of(grid));
            return shot;
        }

        /** @brief Runs the offset box through both untracked launch forms, one output each. */
        Snapshot run_untracked_launches(const Runtime& rt)
        {
            auto source    = source_grid(rt);
            auto by_extent = seeded_grid(rt);
            auto by_tiles  = seeded_grid(rt);

            const Word* in = source.data();
            Word* extent_out = by_extent.data();
            Word* tiles_out  = by_tiles.data();

            // The two kernels are unordered; each is the only writer of its own buffer.
            auto g = rt.graph();
            g.add_untracked(box_by_origin(), [in, extent_out](sycl::id<2> idx)
            {
                stamp_cell(in, extent_out, idx[0], idx[1]);
            });
            g.add_untracked(box_tiles(), [in, tiles_out](sycl::nd_item<2> item)
            {
                stamp_cell(in, tiles_out, item.get_global_id(0) + K_BOX_ROW0,
                           item.get_global_id(1) + K_BOX_COL0);
            });

            drive(g, Drive::Blocking, K_STEPS);

            Snapshot shot;
            take(shot, "untracked Extent", Image(extent_out, extent_out + K_GRID_N));
            take(shot, "untracked NdRange", Image(tiles_out, tiles_out + K_GRID_N));
            return shot;
        }

        /**
         * @brief Runs both untracked launch forms on the runtime's last device, placed with On().
         *
         * Every buffer lives on that device, so a kernel routed anywhere else faults or misses.
         */
        Snapshot run_placed_untracked_launches(const Runtime& rt)
        {
            const DeviceIndex device{rt.advanced().device_count() - 1};
            auto source    = source_grid(rt, device);
            auto by_extent = seeded_grid(rt, device);
            auto by_tiles  = seeded_grid(rt, device);

            const Word* in = source.data();
            Word* extent_out = by_extent.data();
            Word* tiles_out  = by_tiles.data();

            // The two kernels are unordered; each is the only writer of its own buffer.
            auto g = rt.graph();
            g.add_untracked(box_by_origin(), On(by_extent), [in, extent_out](sycl::id<2> idx)
            {
                stamp_cell(in, extent_out, idx[0], idx[1]);
            });
            g.add_untracked(box_tiles(), On(device), [in, tiles_out](sycl::nd_item<2> item)
            {
                stamp_cell(in, tiles_out, item.get_global_id(0) + K_BOX_ROW0,
                           item.get_global_id(1) + K_BOX_COL0);
            });

            drive(g, Drive::Blocking, K_STEPS);

            Snapshot shot;
            take(shot, "untracked Extent", Image(extent_out, extent_out + K_GRID_N));
            take(shot, "untracked NdRange", Image(tiles_out, tiles_out + K_GRID_N));
            return shot;
        }

        /** @brief Returns what run_untracked_launches() must produce, computed on the host. */
        Snapshot untracked_launch_reference()
        {
            const Image grid = reference_grid(K_STEPS);
            Snapshot shot;
            take(shot, "untracked Extent", grid);
            take(shot, "untracked NdRange", grid);
            return shot;
        }
    } // namespace


    /** @brief Holds the runtime of a row: the program's for the in-order chain, one forced onto the scheduler otherwise. */
    class LayerRuntime
    {
        protected:
            /** @brief Makes the runtime of the row. */
            void make_runtime(bool force_scheduler)
            {
                if (force_scheduler)
                {
                    RuntimeConfig config;
                    config.force_scheduler = true;
                    scheduled_.emplace(Runtime::create(config).value());
                }
            }

            /** @brief Returns the runtime of the row. */
            [[nodiscard]] const Runtime& runtime() const
            {
                return scheduled_ ? *scheduled_ : TestSupport::RuntimeEnvironment::runtime();
            }

        private:
            std::optional<Runtime> scheduled_; /**< The forced-scheduler runtime of the true row. */
    };

    /** @brief The pass chain of one row: the layer it runs on and where its two passes run. */
    struct PassRow
    {
        bool force_scheduler; /**< Whether the runtime is forced onto the scheduler. */
        PassSite site;        /**< Where the two whole-buffer passes run. */
    };
} // namespace

/** @brief Fixture of the rows that run a workload on the in-order chain (false) and on the scheduler (true). */
class Medium_LayerEquivalence : public ::testing::TestWithParam<bool>, protected LayerEquivalenceTests::LayerRuntime
{
    protected:
        /** @brief Makes the runtime of the row. */
        void SetUp() override { make_runtime(GetParam()); }
};

/** @brief Fixture of the rows of the pass chain. */
class Medium_LayerEquivalencePasses : public ::testing::TestWithParam<LayerEquivalenceTests::PassRow>,
                                      protected LayerEquivalenceTests::LayerRuntime
{
    protected:
        /** @brief Makes the runtime of the row. */
        void SetUp() override { make_runtime(GetParam().force_scheduler); }
};

namespace LayerEquivalenceTests
{
    /** @brief Tests that a static graph and a DynamicGraph of four regions both leave the bytes of the host model. */
    TEST_P(Medium_LayerEquivalence, DynamicRegionsMatchTheHostModel)
    {
        const Script script{{{true, K_RUNS}}, true};
        const Snapshot reference = tile_reference(script);

        expect_identical(reference, run_static_tiles(runtime(), script, Drive::Blocking),
                         "host model vs static graph");
        expect_identical(reference, run_dynamic_tiles(runtime(), script, Drive::Blocking),
                         "host model vs dynamic graph");
    }

    /** @brief Tests that a region dropped and recorded again leaves the bytes of the host model. */
    TEST_P(Medium_LayerEquivalence, DroppedAndReaddedRegionMatchesTheHostModel)
    {
        const Script script{{{true, 1}, {false, 1}, {true, 1}}, false};

        expect_identical(tile_reference(script), run_dynamic_tiles(runtime(), script, Drive::Blocking),
                         "host model vs dynamic graph");
    }

    /** @brief Tests that the pass chain leaves the bytes of the host model, with the passes on the host or on the device. */
    TEST_P(Medium_LayerEquivalencePasses, PassChainMatchesTheHostModel)
    {
        expect_identical(pass_chain_reference(), run_pass_chain(runtime(), GetParam().site),
                         "host model vs pass chain");
    }

    /** @brief Tests that N run_async() steps of a static graph leave the bytes of the host model. */
    TEST_P(Medium_LayerEquivalence, AsyncStepsMatchTheHostModel)
    {
        const Script script{{{true, K_RUNS}}, true};

        expect_identical(tile_reference(script), run_static_tiles(runtime(), script, Drive::Asynchronous),
                         "host model vs run_async");
    }

    /** @brief Tests that a DynamicGraph stepped through run_async() leaves the bytes of the host model. */
    TEST_P(Medium_LayerEquivalence, AsyncStepsOfADynamicGraphMatchTheHostModel)
    {
        const Script script{{{true, 1}, {false, 1}, {true, 1}}, true};

        expect_identical(tile_reference(script), run_dynamic_tiles(runtime(), script, Drive::Asynchronous),
                         "host model vs dynamic run_async");
    }

    /** @brief Tests that every tracked form of an offset 2-D launch matches the host reference. */
    TEST_P(Medium_LayerEquivalence, OffsetLaunchFormsMatchTheHostReference)
    {
        expect_identical(tracked_launch_reference(), run_tracked_launches(runtime()),
                         "host reference vs tracked launches");
    }

    /** @brief Tests that the untracked Extent and NdRange launches match the host reference. */
    TEST_P(Medium_LayerEquivalence, UntrackedOffsetLaunchesMatchTheHostReference)
    {
        expect_identical(untracked_launch_reference(), run_untracked_launches(runtime()),
                         "host reference vs untracked launches");
    }

    /** @brief Tests that an untracked kernel placed with On() runs on the device it names. */
    TEST_P(Medium_LayerEquivalence, PlacedUntrackedLaunchesMatchTheHostReference)
    {
        expect_identical(untracked_launch_reference(), run_placed_untracked_launches(runtime()),
                         "host reference vs placed untracked launches");
    }
} // namespace LayerEquivalenceTests

INSTANTIATE_TEST_SUITE_P(Rows, Medium_LayerEquivalence, ::testing::Values(false, true));

INSTANTIATE_TEST_SUITE_P(
    Rows, Medium_LayerEquivalencePasses,
    ::testing::Values(LayerEquivalenceTests::PassRow{false, LayerEquivalenceTests::PassSite::Host},
                      LayerEquivalenceTests::PassRow{true, LayerEquivalenceTests::PassSite::Host},
                      LayerEquivalenceTests::PassRow{false, LayerEquivalenceTests::PassSite::Device},
                      LayerEquivalenceTests::PassRow{true, LayerEquivalenceTests::PassSite::Device}));

/** @brief Tests that the placement rules place the nodes of a DynamicGraph's regions. */
namespace DynamicGraphPlacementTests
{
    namespace
    {
        /** @brief Returns a rule set whose one rule sends class "dynamic.ruled" to a device. */
        PlacementRules rule_to_device(std::size_t device, AbsentAction if_absent)
        {
            PlacementRule rule;
            rule.name = "ruled";
            rule.match.placement_class = "dynamic.ruled";
            rule.target.device_index = device;
            rule.if_absent = AbsentPolicy{if_absent, std::chrono::milliseconds{0}};
            PlacementRules rules;
            EXPECT_TRUE(rules.add(std::move(rule)));
            return rules;
        }

        /** @brief Records in region 0, in the rule's scope, a task that notes its device. */
        void record_ruled_task(const Runtime& rt, DynamicGraph& world,
                               std::optional<sycl::device>& ran_on)
        {
            world.region(0).recorder().within(
                rt.placement_scope("dynamic.ruled"),
                [&ran_on](Recorder& in_scope)
                {
                    in_scope.task(Access(),
                                  [&ran_on](sycl::queue& queue, const std::vector<sycl::event>&)
                                  {
                                      ran_on = queue.get_device();
                                      return sycl::event{};
                                  });
                });
        }
    } // namespace

    /**
     * @brief Fixture of the placement tests; it hands the program's runtime back with no rules.
     */
    class Medium_DynamicGraphPlacement : public TestSupport::RuntimeTest
    {
        protected:
            /** @brief Returns the number of devices the runtime drives, which is the first index it lacks. */
            [[nodiscard]] std::size_t absent_device() const { return runtime().advanced().device_count(); }

            /** @brief Returns the device the runtime submits the work of device index @p index on. */
            [[nodiscard]] sycl::device device_of(std::size_t index) const
            {
                return runtime().advanced().queue(DeviceIndex{index}).get_device();
            }

            /** @brief Clears the rules, so no later test places a node by this test's rule. */
            void TearDown() override { (void)runtime().set_placement_rules(PlacementRules{}); }
    };

    /** @brief Tests that a Fail rule naming an absent device refuses the run of a region's node. */
    TEST_F(Medium_DynamicGraphPlacement, ARuleToAnAbsentDeviceRefusesTheRun)
    {
        std::optional<sycl::device> noted;
        auto world = runtime().dynamic_graph();
        record_ruled_task(runtime(), world, noted);

        ASSERT_TRUE(runtime().set_placement_rules(rule_to_device(absent_device(), AbsentAction::Fail)));
        const auto refused = world.run();

        ASSERT_FALSE(refused) << "the region's node is placed by the Fail rule";
        EXPECT_EQ(refused.error().code, ErrorCode::InvalidArgument);
        EXPECT_NE(refused.error().message.find("\"ruled\""), std::string::npos)
            << refused.error().message;
    }

    /** @brief Tests that a rule change between two runs re-places a region's node without a re-record. */
    TEST_F(Medium_DynamicGraphPlacement, ARuleChangeReplacesARegionsNodeWithoutARecord)
    {
        std::optional<sycl::device> noted;
        auto world = runtime().dynamic_graph();
        record_ruled_task(runtime(), world, noted);

        ASSERT_TRUE(runtime().set_placement_rules(rule_to_device(absent_device(), AbsentAction::Fail)));
        ASSERT_FALSE(world.run());

        ASSERT_TRUE(runtime().set_placement_rules(rule_to_device(absent_device(), AbsentAction::Local)));
        ASSERT_TRUE(world.run()) << "the new set reaches the region without a re-record";
        ASSERT_TRUE(noted.has_value());
        EXPECT_EQ(*noted, device_of(0));
    }

    /** @brief Tests that a rule set that has not changed composes nothing at the next run. */
    TEST_F(Medium_DynamicGraphPlacement, AnUnchangedRuleSetComposesNothing)
    {
        std::optional<sycl::device> noted;
        auto world = runtime().dynamic_graph();
        record_ruled_task(runtime(), world, noted);

        ASSERT_TRUE(runtime().set_placement_rules(rule_to_device(absent_device(), AbsentAction::Local)));
        ASSERT_TRUE(world.run());
        const std::size_t composed = world.compose_count();

        ASSERT_TRUE(world.run());
        EXPECT_EQ(world.compose_count(), composed) << "an unchanged set composes nothing";
    }

    /** @brief Tests that a rule change to device one re-places the node there and composes once. */
    TEST_F(Medium_DynamicGraphPlacement, ARuleChangeToDeviceOneRemapsTheNode)
    {
        if (absent_device() < 2)
            GTEST_SKIP() << "needs a second device to remap the node to";

        std::optional<sycl::device> noted;
        auto world = runtime().dynamic_graph();
        record_ruled_task(runtime(), world, noted);

        ASSERT_TRUE(runtime().set_placement_rules(rule_to_device(absent_device(), AbsentAction::Local)));
        ASSERT_TRUE(world.run());
        const std::size_t composed = world.compose_count();

        ASSERT_TRUE(runtime().set_placement_rules(rule_to_device(1, AbsentAction::Fail)));
        ASSERT_TRUE(world.run());
        EXPECT_EQ(world.compose_count(), composed + 1);
        ASSERT_TRUE(noted.has_value());
        EXPECT_EQ(*noted, device_of(1));
    }
} // namespace DynamicGraphPlacementTests
