/**************************************************************************/
/* test_region_composer.cpp                                               */
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
 * @file test_region_composer.cpp
 * @brief Tests the boundary edges the region composer keeps and what a throwing commit leaves.
 * @see tests/surface/api/graph/plumbing/README.md
 * @author Mustafa Garip
 */

#include <cstddef>
#include <memory>
#include <stdexcept>
#include <vector>

#include <gtest/gtest.h>

#include <SushiRuntime/sushiruntime.hpp>
#include <SushiRuntime/graph/task_graph.hpp>
#include <SushiRuntime/task/node.hpp>
#include <SushiRuntime/graph/compiled_graph.hpp>

#include "dynamic_graph_plan_access.hpp"
#include "refusing_fill.hpp"

using namespace SushiRuntime;

/** @brief Tests the entry and exit pins of a region and the cross-region edges made from them. */
namespace BoundaryAccessTests
{
    namespace
    {
        /** @brief Number of elements of the buffer the edge tests slice. */
        constexpr std::size_t K_ELEMENTS = 20;

        /** @brief Returns the plan of a graph that requested profiling. */
        const Internal::Graph::CompiledGraph& plan_of(const DynamicGraph& world)
        {
            return Detail::DynamicGraphNodeEventsTestAccess::plan(world);
        }
    } // namespace


    /** @brief Tests that an earlier region's writer orders a reader its predecessor half covers. */
    TEST(Integration_BoundaryAccess, AnEarlierWriterOrdersAReaderItsPredecessorHalfCovers)
    {
        auto rt = Runtime::create().value();
        rt.set_rebalancer_enabled(false);

        auto shared = rt.buffer<int>(K_ELEMENTS).value();
        auto total = rt.buffer<int>(1).value();
        for (std::size_t i = 0; i < K_ELEMENTS; ++i)
            shared[i] = 0;
        total[0] = 0;
        int* cells = shared.data();
        int* sum = total.data();

        auto world = rt.dynamic_graph();
        world.set_profiling(true);
        world.region(1).add_host(Reads{}, Writes(shared.region({15, 5})),
                                 [cells]() { for (std::size_t i = 15; i < 20; ++i) cells[i] = 7; })
                       .name("w");
        world.region(2).add_host(Reads{}, Writes(shared.region({0, 10})),
                                 [cells]() { for (std::size_t i = 0; i < 10; ++i) cells[i] = 1; })
                       .name("p");
        world.region(2).add_host(Reads(shared.region({5, 15})), Writes(total),
                                 [cells, sum]()
                                 {
                                     int read = 0;
                                     for (std::size_t i = 5; i < 20; ++i) read += cells[i];
                                     sum[0] = read;
                                 })
                       .name("q");
        world.run().value();

        const Internal::Graph::Node* w = Testing::find_plan_node(plan_of(world), "w");
        const Internal::Graph::Node* node = Testing::find_plan_node(plan_of(world), "q");
        ASSERT_NE(w, nullptr);
        ASSERT_NE(node, nullptr);
        EXPECT_TRUE(Testing::has_boundary_edge(*w, *node)) << "q reads [15,20) unordered against w";
        EXPECT_EQ(Testing::boundary_in_degree(*node), 1);
        EXPECT_EQ(total[0], 5 * 1 + 5 * 0 + 5 * 7);
    }

    /** @brief Tests that a reader its predecessor fully covers gains no edge of its own. */
    TEST(Integration_BoundaryAccess, AReaderItsPredecessorCoversGainsNoEdge)
    {
        auto rt = Runtime::create().value();
        rt.set_rebalancer_enabled(false);

        auto shared = rt.buffer<int>(K_ELEMENTS).value();
        auto total = rt.buffer<int>(1).value();
        for (std::size_t i = 0; i < K_ELEMENTS; ++i)
            shared[i] = 0;
        total[0] = 0;
        int* cells = shared.data();
        int* sum = total.data();

        auto world = rt.dynamic_graph();
        world.set_profiling(true);
        world.region(1).add_host(Reads{}, Writes(shared.region({15, 5})),
                                 [cells]() { for (std::size_t i = 15; i < 20; ++i) cells[i] = 7; })
                       .name("w");
        world.region(2).add_host(Reads{}, Writes(shared),
                                 [cells]() { for (std::size_t i = 0; i < 20; ++i) cells[i] += 1; })
                       .name("p");
        world.region(2).add_host(Reads(shared.region({5, 15})), Writes(total),
                                 [cells, sum]()
                                 {
                                     int read = 0;
                                     for (std::size_t i = 5; i < 20; ++i) read += cells[i];
                                     sum[0] = read;
                                 })
                       .name("q");
        world.run().value();

        const Internal::Graph::Node* w = Testing::find_plan_node(plan_of(world), "w");
        const Internal::Graph::Node* p = Testing::find_plan_node(plan_of(world), "p");
        const Internal::Graph::Node* node = Testing::find_plan_node(plan_of(world), "q");
        ASSERT_NE(w, nullptr);
        ASSERT_NE(p, nullptr);
        ASSERT_NE(node, nullptr);
        EXPECT_TRUE(Testing::has_boundary_edge(*w, *p));
        EXPECT_FALSE(Testing::has_boundary_edge(*w, *node)) << "p already orders q behind w";
        EXPECT_EQ(Testing::boundary_in_degree(*node), 0);
        EXPECT_EQ(total[0], 10 * 1 + 5 * 8);
    }

    /** @brief Tests that an earlier region's reader orders a writer whose predecessor only reads. */
    TEST(Integration_BoundaryAccess, AnEarlierReaderOrdersAWriterBehindAReadingPredecessor)
    {
        auto rt = Runtime::create().value();
        rt.set_rebalancer_enabled(false);

        auto shared = rt.buffer<int>(K_ELEMENTS).value();
        auto early = rt.buffer<int>(1).value();
        auto late = rt.buffer<int>(1).value();
        for (std::size_t i = 0; i < K_ELEMENTS; ++i)
            shared[i] = 3;
        early[0] = 0;
        late[0] = 0;
        int* cells = shared.data();
        int* first = early.data();
        int* second = late.data();

        auto world = rt.dynamic_graph();
        world.set_profiling(true);
        world.region(1).add_host(Reads(shared.region({0, 10})), Writes(early),
                                 [cells, first]() { first[0] = cells[0]; })
                       .name("r");
        world.region(2).add_host(Reads(shared.region({0, 10})), Writes(late),
                                 [cells, second]() { second[0] = cells[0]; })
                       .name("p");
        world.region(2).add_host(Reads{}, Writes(shared.region({0, 10})),
                                 [cells]() { for (std::size_t i = 0; i < 10; ++i) cells[i] = 9; })
                       .name("q");
        world.run().value();

        const Internal::Graph::Node* r = Testing::find_plan_node(plan_of(world), "r");
        const Internal::Graph::Node* node = Testing::find_plan_node(plan_of(world), "q");
        ASSERT_NE(r, nullptr);
        ASSERT_NE(node, nullptr);
        EXPECT_TRUE(Testing::has_boundary_edge(*r, *node)) << "q overwrites what r still reads";
        EXPECT_EQ(Testing::boundary_in_degree(*node), 1);
        EXPECT_EQ(early[0], 3);
        EXPECT_EQ(late[0], 3);
    }
} // namespace BoundaryAccessTests

/** @brief Tests what a DynamicGraph keeps when the commit of its regions throws. */
namespace RegionCommitTests
{
    namespace
    {
        /** @brief Number of elements of every buffer in this file. */
        constexpr std::size_t K_ELEMENTS = 8;

        /** @brief Message of the exception the refused copy of a task throws. */
        constexpr const char* K_BUILD_FAILURE = "the task refused to build";

        /** @brief Returns the plan of a graph that requested profiling. */
        const Internal::Graph::CompiledGraph& plan_of(const DynamicGraph& world)
        {
            return Detail::DynamicGraphNodeEventsTestAccess::plan(world);
        }

        /** @brief Copies K_ELEMENTS elements from one buffer to another. */
        void copy_elements(const int* from, int* to)
        {
            for (std::size_t i = 0; i < K_ELEMENTS; ++i)
                to[i] = from[i];
        }

        /** @brief Expects the cross-region edge from the node named "a" to the node named "c". */
        void expect_producer_orders_consumer(const DynamicGraph& world)
        {
            const Internal::Graph::Node* a = Testing::find_plan_node(plan_of(world), "a");
            const Internal::Graph::Node* c = Testing::find_plan_node(plan_of(world), "c");
            ASSERT_NE(a, nullptr);
            ASSERT_NE(c, nullptr);
            EXPECT_TRUE(Testing::has_boundary_edge(*a, *c));
            EXPECT_EQ(Testing::boundary_in_degree(*c), 1);
        }
    } // namespace

    /** @brief Tests that a commit whose region build throws keeps the edges of the other regions. */
    TEST(Integration_RegionCommit, AThrowingRegionBuildKeepsTheEdgesOfTheOtherRegions)
    {
        auto rt = Runtime::create().value();
        rt.set_rebalancer_enabled(false);

        auto shared = rt.buffer<int>(K_ELEMENTS).value();
        auto out_b = rt.buffer<int>(K_ELEMENTS).value();
        auto out_c = rt.buffer<int>(K_ELEMENTS).value();
        auto own_d = rt.buffer<int>(K_ELEMENTS).value();
        for (std::size_t i = 0; i < K_ELEMENTS; ++i)
        {
            shared[i] = 0;
            out_b[i] = 0;
            out_c[i] = 0;
            own_d[i] = 0;
        }
        int* ps = shared.data();
        int* pb = out_b.data();
        int* pc = out_c.data();
        int* pd = own_d.data();

        auto world = rt.dynamic_graph();
        world.set_profiling(true);
        world.region(0).add_host(Reads{}, Writes(shared),
                                 [ps]() { for (std::size_t i = 0; i < K_ELEMENTS; ++i) ps[i] += 1; })
                       .name("a");
        world.region(1).add_host(Reads(shared), Writes(out_b),
                                 [ps, pb]() { copy_elements(ps, pb); })
                       .name("b");
        world.region(2).add_host(Reads(shared), Writes(out_c),
                                 [ps, pc]() { copy_elements(ps, pc); })
                       .name("c");
        world.run().value();
        expect_producer_orders_consumer(world);
        const std::size_t composed = world.compose_count();

        const auto refuses = std::make_shared<bool>(true);
        world.drop(1);
        world.region(3).add_host(Reads{}, Writes(own_d),
                                 Testing::RefusingFill(refuses, K_BUILD_FAILURE, pd, K_ELEMENTS, 5));

        const auto failed = world.run();
        ASSERT_FALSE(failed.has_value());
        EXPECT_EQ(failed.error().code, ErrorCode::CompileFailed);
        EXPECT_EQ(world.compose_count(), composed) << "a commit that threw composed nothing";
        expect_producer_orders_consumer(world);

        *refuses = false;
        const auto retried = world.run();
        ASSERT_TRUE(retried.has_value());
        EXPECT_EQ(world.compose_count(), composed + 1);
        expect_producer_orders_consumer(world);

        for (std::size_t i = 0; i < K_ELEMENTS; ++i)
        {
            ASSERT_EQ(shared[i], 2) << "at i=" << i;
            ASSERT_EQ(out_c[i], 2) << "at i=" << i;
            ASSERT_EQ(out_b[i], 1) << "the dropped region ran once, at i=" << i;
            ASSERT_EQ(own_d[i], 5) << "at i=" << i;
        }
    }
} // namespace RegionCommitTests
