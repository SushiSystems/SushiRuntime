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
#include <thread>
#include <utility>
#include <vector>

#include <gtest/gtest.h>
#include <sycl/sycl.hpp>

#include <SushiRuntime/sushiruntime.hpp>
#include <SushiRuntime/context_access/context_access.hpp>
#include <SushiRuntime/device/device_queue_set.hpp>
#include <SushiRuntime/execution/runtime_context.hpp>
#include <SushiRuntime/graph/compiled_graph.hpp>
#include <SushiRuntime/task/node.hpp>

#include "dynamic_graph_plan_access.hpp"
#include "run_recorder.hpp"

using namespace SushiRuntime;

/** @brief Tests the region-partitioned graph that Runtime::dynamic_graph() returns. */
namespace DynamicGraphTests
{
    /** @brief Tests that two independent regions advance their buffers and compose the plan once. */
    TEST(Medium_DynamicGraph, IndependentRegionsRunAndComposeOnce)
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
    TEST(Medium_DynamicGraph, CrossRegionDependencyOrders)
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
    TEST(Medium_DynamicGraph, DropRegionStopsItsWork)
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
    TEST(Medium_DynamicGraph, AddRegionBetweenSteps)
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
    TEST(Medium_DynamicGraph, SingleRecomposePerMutation)
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
    TEST(Medium_DynamicGraph, FindRegionReadsWithoutRecomposing)
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
    TEST(Medium_DynamicGraph, ThreeRegionChainThreadsValue)
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
    TEST(Medium_DynamicGraph, ReadReadSharingStaysConsistent)
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
    TEST(Medium_DynamicGraph, StreamInConsumerRewiresProducer)
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
    TEST(Medium_DynamicGraph, DropProducerLeavesConsumerRunnable)
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
    TEST(Medium_DynamicGraph, DisjointRangeRegionsAreNotOrdered)
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
    TEST(Medium_DynamicGraph, OverlappingRangeRegionsStillSerialize)
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
    TEST(Medium_DynamicGraph, WholeBufferReaderOrdersAfterBothHalfWriters)
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
} // namespace DynamicGraphTests

/** @brief Tests DynamicGraph's per-graph profiling switch and its dense registration index. */
namespace DynamicGraphNodeEventsTests
{
    namespace
    {
        /** @brief Returns the registration index of the one compiled node with the given name. */
        std::uint32_t registration_index_of(const Internal::Graph::CompiledGraph& plan, const char* name)
        {
            const Internal::Graph::Node* found = Testing::find_plan_node(plan, name);
            return found ? found->registration_index : 0u;
        }
    } // namespace

    /** @brief Tests that a graph of a runtime with no observer records nothing. */
    TEST(Medium_DynamicGraphNodeEvents, NotRequestedRecordsNothing)
    {
        constexpr std::size_t N = 32;

        auto rt = Runtime::create().value();
        rt.set_rebalancer_enabled(false);
        Testing::RunRecorder observed(rt);
        observed.stop();
        auto buf = rt.buffer<int>(N).value();
        int* p = buf.data();

        auto world = rt.dynamic_graph();
        world.region(0).add(N, Reads{}, Writes(buf),
                            [p](std::size_t i) { p[i] = static_cast<int>(i); });

        auto report = world.run().value();
        EXPECT_TRUE(report.is_successful);
        EXPECT_EQ(observed.count(), 0u);
    }

    /** @brief Tests that an observed DynamicGraph produces records that match the renumbered index. */
    TEST(Medium_DynamicGraphNodeEvents, RequestedGraphProducesRecordsMatchingRenumberedIndex)
    {
        constexpr std::size_t N = 32;

        auto rt = Runtime::create().value();
        rt.set_rebalancer_enabled(false);
        Testing::RunRecorder observed(rt);
        auto a = rt.buffer<int>(N).value();
        auto b = rt.buffer<int>(N).value();
        int* pa = a.data();
        int* pb = b.data();

        auto world = rt.dynamic_graph();
        world.region(0).add(N, Reads{}, Writes(a),
                            [pa](std::size_t i) { pa[i] = static_cast<int>(i); }).name("A");
        world.region(1).add(N, Reads{}, Writes(b),
                            [pb](std::size_t i) { pb[i] = static_cast<int>(i); }).name("B");

        world.run().value();
        ASSERT_EQ(observed.count(), 1u);
        const RunObservation observation = observed.last();
        ASSERT_FALSE(observation.events.empty())
            << "an observed DynamicGraph must record device dispatches, not just stamp the flag";

        const auto& plan = Detail::DynamicGraphNodeEventsTestAccess::plan(world);
        const std::uint32_t idx_a = registration_index_of(plan, "A");
        const std::uint32_t idx_b = registration_index_of(plan, "B");

        for (const auto& rec : observation.events)
            EXPECT_TRUE(rec.node_index == idx_a || rec.node_index == idx_b)
                << "record node_index " << rec.node_index << " matches neither renumbered index";
    }

    /** @brief Tests that node_index is dense and ordered by ascending region key, then add() order. */
    TEST(Medium_DynamicGraphNodeEvents, IndexDenseAndOrderedByRegionThenAddOrder)
    {
        constexpr std::size_t N = 16;

        auto rt = Runtime::create().value();
        rt.set_rebalancer_enabled(false);
        Testing::RunRecorder observed(rt);
        auto a = rt.buffer<int>(N).value();
        auto b = rt.buffer<int>(N).value();
        auto c = rt.buffer<int>(N).value();
        int* pa = a.data();
        int* pb = b.data();
        int* pc = c.data();

        auto world = rt.dynamic_graph();

        // Region 5 is keyed above region 2, so its two calls land after region 2's one; see README.md.
        world.region(5).add(N, Reads{}, Writes(a),
                            [pa](std::size_t i) { pa[i] = static_cast<int>(i); }).name("R5_first");
        world.region(5).add(N, Reads{}, Writes(b),
                            [pb](std::size_t i) { pb[i] = static_cast<int>(i); }).name("R5_second");
        world.region(2).add(N, Reads{}, Writes(c),
                            [pc](std::size_t i) { pc[i] = static_cast<int>(i); }).name("R2_only");

        world.run().value();

        const auto& plan = Detail::DynamicGraphNodeEventsTestAccess::plan(world);
        const std::uint32_t idx_r2 = registration_index_of(plan, "R2_only");
        const std::uint32_t idx_r5_first = registration_index_of(plan, "R5_first");
        const std::uint32_t idx_r5_second = registration_index_of(plan, "R5_second");

        EXPECT_EQ(idx_r2, 0u) << "region 2 sorts before region 5 by key, whatever add() order";
        EXPECT_EQ(idx_r5_first, 1u);
        EXPECT_EQ(idx_r5_second, 2u);
    }

    /** @brief Tests that the index stays dense after a region is dropped and re-added. */
    TEST(Medium_DynamicGraphNodeEvents, IndexStaysDenseAfterDropAndReAdd)
    {
        constexpr std::size_t N = 16;

        auto rt = Runtime::create().value();
        rt.set_rebalancer_enabled(false);
        Testing::RunRecorder observed(rt);
        auto a = rt.buffer<int>(N).value();
        auto b = rt.buffer<int>(N).value();
        int* pa = a.data();
        int* pb = b.data();

        auto world = rt.dynamic_graph();

        world.region(0).add(N, Reads{}, Writes(a),
                            [pa](std::size_t i) { pa[i] = static_cast<int>(i); }).name("kept");
        world.region(1).add(N, Reads{}, Writes(b),
                            [pb](std::size_t i) { pb[i] = static_cast<int>(i); }).name("dropped_v1");
        world.run().value();

        world.drop(1);
        world.region(1).add(N, Reads{}, Writes(b),
                            [pb](std::size_t i) { pb[i] = static_cast<int>(i) * 2; }).name("dropped_v2");
        world.run().value();

        const auto& plan = Detail::DynamicGraphNodeEventsTestAccess::plan(world);
        const std::uint32_t idx_kept = registration_index_of(plan, "kept");
        const std::uint32_t idx_v2 = registration_index_of(plan, "dropped_v2");

        EXPECT_EQ(world.region_count(), 2u);
        EXPECT_EQ(idx_kept, 0u);
        EXPECT_EQ(idx_v2, 1u) << "re-adding region 1 must not resume from a stale counter";
    }

    // Adding the first observer recomposes exactly once, not once per run.
    TEST(Medium_DynamicGraphNodeEvents, AddingAnObserverRecomposesOnce)
    {
        constexpr std::size_t N = 8;

        auto rt = Runtime::create().value();
        rt.set_rebalancer_enabled(false);
        auto buf = rt.buffer<int>(N).value();
        int* p = buf.data();

        auto world = rt.dynamic_graph();
        world.region(0).add(N, Reads{}, Writes(buf),
                            [p](std::size_t i) { p[i] += 1; });

        world.run().value();
        const std::size_t compiles_before = world.compose_count();

        Testing::RunRecorder observed(rt);
        world.run().value();
        EXPECT_EQ(world.compose_count(), compiles_before + 1)
            << "the first observer must force exactly one recompose";
        EXPECT_EQ(observed.count(), 1u);

        const std::size_t compiles_after_toggle = world.compose_count();
        world.run().value();
        EXPECT_EQ(world.compose_count(), compiles_after_toggle)
            << "an unmutated run after the observer is added must not recompose again";
    }
} // namespace DynamicGraphNodeEventsTests

/** @brief Tests that four graph shapes give byte-identical output on every path that runs them. */
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
            /** @brief Layer the last blocking run reported. */
            ExecutionLayer layer = ExecutionLayer::Scheduler;
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

        /** @brief Returns a configuration that keeps every plan on the scheduler layer. */
        RuntimeConfig scheduler_only()
        {
            RuntimeConfig config;
            config.force_scheduler = true;
            return config;
        }

        /** @brief Says how a graph is stepped. */
        enum class Drive : std::uint8_t
        {
            Blocking,     /**< One run(steps) call. */
            Asynchronous, /**< One run_async() and wait() per step. */
        };

        /**
         * @brief Steps a graph the given way and records the layer a blocking run reports.
         * @tparam Runnable Graph or DynamicGraph.
         */
        template <class Runnable>
        void drive(Runnable& graph, Drive how, std::size_t steps, Snapshot& shot)
        {
            if (how == Drive::Blocking)
            {
                const RunReport report = graph.run(steps).value();
                EXPECT_TRUE(report.is_successful);
                shot.layer = report.layer;
                return;
            }
            for (std::size_t s = 0; s < steps; ++s)
                graph.run_async().value().wait().value();
        }

        /** @brief Owns the data of the tile workload: three chained buffers and one State. */
        struct Tiles
        {
            /** @brief Allocates the data on @p rt and writes the starting values. */
            explicit Tiles(Runtime& rt)
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
        void run_static_segment(Runtime& rt, Tiles& tiles, const Segment& segment,
                                bool evolve_field, Drive how, Snapshot& shot)
        {
            auto g = rt.graph();
            record_scale(g, tiles);
            if (segment.mix_live)
                record_mix(g, tiles);
            record_fold(g, tiles);
            if (evolve_field)
                record_field(g, tiles);

            for (std::size_t r = 0; r < segment.runs; ++r)
                drive(g, how, K_STEPS, shot);
        }

        /** @brief Runs a script with one static graph per segment and copies the results out. */
        Snapshot run_static_tiles(Runtime& rt, const Script& script, Drive how)
        {
            Tiles tiles(rt);
            Snapshot shot;
            for (const Segment& segment : script.segments)
                run_static_segment(rt, tiles, segment, script.evolve_field, how, shot);
            take_tiles(shot, tiles);
            return shot;
        }

        /**
         * @brief Runs a script on one DynamicGraph, one region per kernel, and copies the results out.
         *
         * The mix region is dropped or recorded again where a segment changes it.
         */
        Snapshot run_dynamic_tiles(Runtime& rt, const Script& script, Drive how)
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
                    drive(world, how, K_STEPS, shot);
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
        Snapshot run_pass_chain(Runtime& rt, PassSite site)
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

            Snapshot shot;
            for (std::size_t r = 0; r < K_RUNS; ++r)
                drive(g, Drive::Blocking, K_STEPS, shot);

            take(shot, "a", Image(pa, pa + K_HOST_N));
            take(shot, "b", Image(pb, pb + K_HOST_N));
            take(shot, "c", Image(pc, pc + K_HOST_N));
            take(shot, "d", Image(pd, pd + K_HOST_N));
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
        Buffer<Word> seeded_grid(Runtime& rt, DeviceIndex device = DeviceIndex{0})
        {
            auto grid = rt.buffer<Word>(K_GRID_N, device).value();
            for (std::size_t k = 0; k < K_GRID_N; ++k)
                grid[k] = seed_word(k);
            return grid;
        }

        /** @brief Allocates the read-only input grid on a device of @p rt. */
        Buffer<Word> source_grid(Runtime& rt, DeviceIndex device = DeviceIndex{0})
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
        Snapshot run_tracked_launches(Runtime& rt)
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

            Snapshot shot;
            drive(g, Drive::Blocking, K_STEPS, shot);

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
        Snapshot run_untracked_launches(Runtime& rt)
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

            Snapshot shot;
            drive(g, Drive::Blocking, K_STEPS, shot);

            take(shot, "untracked Extent", Image(extent_out, extent_out + K_GRID_N));
            take(shot, "untracked NdRange", Image(tiles_out, tiles_out + K_GRID_N));
            return shot;
        }

        /**
         * @brief Runs both untracked launch forms on the runtime's last device, placed with On().
         *
         * Every buffer lives on that device, so a kernel routed anywhere else faults or misses.
         */
        Snapshot run_placed_untracked_launches(Runtime& rt)
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

            Snapshot shot;
            drive(g, Drive::Blocking, K_STEPS, shot);

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

    /** @brief Tests that four regions of a DynamicGraph leave the bytes one static Graph leaves. */
    TEST(Medium_LayerEquivalence, DynamicRegionsMatchOneStaticGraph)
    {
        const Script script{{{true, K_RUNS}}, true};

        auto chained_rt = Runtime::create().value();
        auto scheduled_rt = Runtime::create(scheduler_only()).value();

        const Snapshot static_chained    = run_static_tiles(chained_rt, script, Drive::Blocking);
        const Snapshot static_scheduled  = run_static_tiles(scheduled_rt, script, Drive::Blocking);
        const Snapshot dynamic_default   = run_dynamic_tiles(chained_rt, script, Drive::Blocking);
        const Snapshot dynamic_scheduled = run_dynamic_tiles(scheduled_rt, script, Drive::Blocking);

        EXPECT_EQ(static_chained.layer, ExecutionLayer::Chain)
            << "the static graph of tracked kernels must take the chain";
        EXPECT_EQ(static_scheduled.layer, ExecutionLayer::Scheduler);

        expect_identical(static_chained, static_scheduled, "static chain vs static scheduler");
        expect_identical(static_chained, dynamic_default, "static chain vs dynamic");
        expect_identical(static_chained, dynamic_scheduled,
                         "static chain vs dynamic, forced scheduler");
    }

    /** @brief Tests that a region dropped and recorded again matches a static graph per stretch. */
    TEST(Medium_LayerEquivalence, DroppedAndReaddedRegionMatchesStaticGraphs)
    {
        const Script script{{{true, 1}, {false, 1}, {true, 1}}, false};

        auto chained_rt = Runtime::create().value();
        auto scheduled_rt = Runtime::create(scheduler_only()).value();

        const Snapshot static_chained    = run_static_tiles(chained_rt, script, Drive::Blocking);
        const Snapshot static_scheduled  = run_static_tiles(scheduled_rt, script, Drive::Blocking);
        const Snapshot dynamic_default   = run_dynamic_tiles(chained_rt, script, Drive::Blocking);
        const Snapshot dynamic_scheduled = run_dynamic_tiles(scheduled_rt, script, Drive::Blocking);

        EXPECT_EQ(static_chained.layer, ExecutionLayer::Chain);
        EXPECT_EQ(static_scheduled.layer, ExecutionLayer::Scheduler);

        expect_identical(static_chained, static_scheduled, "static chain vs static scheduler");
        expect_identical(static_chained, dynamic_default, "static chain vs dynamic");
        expect_identical(static_chained, dynamic_scheduled,
                         "static chain vs dynamic, forced scheduler");
    }

    /** @brief Tests that host tasks between kernels give the same bytes on both layers. */
    TEST(Medium_LayerEquivalence, HostTasksAmongKernelsMatchAcrossLayers)
    {
        auto default_rt = Runtime::create().value();
        auto scheduled_rt = Runtime::create(scheduler_only()).value();

        const Snapshot host_default   = run_pass_chain(default_rt, PassSite::Host);
        const Snapshot host_scheduled = run_pass_chain(scheduled_rt, PassSite::Host);
        const Snapshot device_chained = run_pass_chain(default_rt, PassSite::Device);

        EXPECT_EQ(host_scheduled.layer, ExecutionLayer::Scheduler);
        EXPECT_EQ(device_chained.layer, ExecutionLayer::Chain)
            << "the same chain written as kernels only must take the chain";

        expect_identical(host_default, host_scheduled, "host tasks: default vs forced scheduler");
        expect_identical(device_chained, host_default, "kernels on the chain vs host tasks");
    }

    /** @brief Tests that N run_async() steps leave the bytes run(N) leaves, on both layers. */
    TEST(Medium_LayerEquivalence, AsyncStepsMatchBlockingRun)
    {
        const Script script{{{true, K_RUNS}}, true};

        auto chained_rt = Runtime::create().value();
        auto scheduled_rt = Runtime::create(scheduler_only()).value();

        const Snapshot blocking_chained   = run_static_tiles(chained_rt, script, Drive::Blocking);
        const Snapshot blocking_scheduled = run_static_tiles(scheduled_rt, script, Drive::Blocking);
        const Snapshot async_default      = run_static_tiles(chained_rt, script, Drive::Asynchronous);
        const Snapshot async_scheduled    = run_static_tiles(scheduled_rt, script, Drive::Asynchronous);

        EXPECT_EQ(blocking_chained.layer, ExecutionLayer::Chain);
        EXPECT_EQ(blocking_scheduled.layer, ExecutionLayer::Scheduler);

        expect_identical(blocking_chained, async_default, "run(N) on the chain vs run_async");
        expect_identical(blocking_scheduled, async_scheduled, "run(N) vs run_async, forced scheduler");
        expect_identical(blocking_chained, blocking_scheduled, "run(N): chain vs forced scheduler");
    }

    /** @brief Tests that a DynamicGraph stepped through run_async() matches its blocking run. */
    TEST(Medium_LayerEquivalence, AsyncStepsOfADynamicGraphMatchBlockingRun)
    {
        const Script script{{{true, 1}, {false, 1}, {true, 1}}, true};

        auto rt = Runtime::create().value();

        const Snapshot blocking = run_dynamic_tiles(rt, script, Drive::Blocking);
        const Snapshot stepped  = run_dynamic_tiles(rt, script, Drive::Asynchronous);

        expect_identical(blocking, stepped, "dynamic run(N) vs dynamic run_async");
    }

    /** @brief Tests that every tracked form of an offset 2-D launch matches the host reference. */
    TEST(Medium_LayerEquivalence, OffsetLaunchFormsMatchAcrossLayers)
    {
        auto chained_rt = Runtime::create().value();
        auto scheduled_rt = Runtime::create(scheduler_only()).value();

        const Snapshot reference = tracked_launch_reference();
        const Snapshot chained   = run_tracked_launches(chained_rt);
        const Snapshot scheduled = run_tracked_launches(scheduled_rt);

        EXPECT_EQ(chained.layer, ExecutionLayer::Chain)
            << "tracked Extent and NdRange kernels on one device must take the chain";
        EXPECT_EQ(scheduled.layer, ExecutionLayer::Scheduler);

        expect_identical(reference, chained, "host reference vs chain");
        expect_identical(reference, scheduled, "host reference vs forced scheduler");
        expect_identical(chained, scheduled, "chain vs forced scheduler");
    }

    /** @brief Tests that the untracked Extent and NdRange launches match the host reference. */
    TEST(Medium_LayerEquivalence, UntrackedOffsetLaunchesMatchAcrossLayers)
    {
        auto default_rt = Runtime::create().value();
        auto scheduled_rt = Runtime::create(scheduler_only()).value();

        const Snapshot reference       = untracked_launch_reference();
        const Snapshot default_layer   = run_untracked_launches(default_rt);
        const Snapshot scheduler_layer = run_untracked_launches(scheduled_rt);

        EXPECT_EQ(scheduler_layer.layer, ExecutionLayer::Scheduler);

        expect_identical(reference, default_layer, "host reference vs default layer");
        expect_identical(reference, scheduler_layer, "host reference vs forced scheduler");
        expect_identical(default_layer, scheduler_layer, "default layer vs forced scheduler");
    }

    /** @brief Tests that an untracked kernel placed with On() runs on the device it names. */
    TEST(Medium_LayerEquivalence, PlacedUntrackedLaunchesMatchAcrossLayers)
    {
        auto default_rt = Runtime::create().value();
        auto scheduled_rt = Runtime::create(scheduler_only()).value();

        const Snapshot reference       = untracked_launch_reference();
        const Snapshot default_layer   = run_placed_untracked_launches(default_rt);
        const Snapshot scheduler_layer = run_placed_untracked_launches(scheduled_rt);

        EXPECT_EQ(scheduler_layer.layer, ExecutionLayer::Scheduler);

        expect_identical(reference, default_layer, "host reference vs default layer");
        expect_identical(reference, scheduler_layer, "host reference vs forced scheduler");
        expect_identical(default_layer, scheduler_layer, "default layer vs forced scheduler");
    }
} // namespace LayerEquivalenceTests

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
        void record_ruled_task(Runtime& rt, DynamicGraph& world,
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

    /** @brief Tests that a rule change between two runs re-places a region's node unrecorded. */
    TEST(Medium_DynamicGraphPlacement, Regression_ARuleChangeReplacesARegionsNodeWithoutARecord)
    {
        auto rt = Runtime::create().value();
        const std::size_t absent = rt.advanced().device_count();
        std::optional<sycl::device> noted;
        auto world = rt.dynamic_graph();
        record_ruled_task(rt, world, noted);

        ASSERT_TRUE(rt.set_placement_rules(rule_to_device(absent, AbsentAction::Fail)));
        const auto refused = world.run();
        ASSERT_FALSE(refused) << "the region's node is placed by the Fail rule";
        EXPECT_EQ(refused.error().code, ErrorCode::InvalidArgument);
        EXPECT_NE(refused.error().message.find("\"ruled\""), std::string::npos)
            << refused.error().message;

        ASSERT_TRUE(rt.set_placement_rules(rule_to_device(absent, AbsentAction::Local)));
        ASSERT_TRUE(world.run()) << "the new set reaches the region without a re-record";
        ASSERT_TRUE(noted.has_value());
        EXPECT_EQ(*noted, Internal::ContextAccess::context(rt).devices().queue(0).get_device());
        const std::size_t composed = world.compose_count();

        ASSERT_TRUE(world.run());
        EXPECT_EQ(world.compose_count(), composed) << "an unchanged set composes nothing";

        if (absent < 2)
            return;
        ASSERT_TRUE(rt.set_placement_rules(rule_to_device(1, AbsentAction::Fail)));
        ASSERT_TRUE(world.run());
        EXPECT_EQ(world.compose_count(), composed + 1);
        EXPECT_EQ(*noted, Internal::ContextAccess::context(rt).devices().queue(1).get_device());
    }
} // namespace DynamicGraphPlacementTests
