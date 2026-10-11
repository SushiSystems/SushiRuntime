/**************************************************************************/
/* test_boundary_access.cpp                                               */
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
 * @file test_boundary_access.cpp
 * @brief Tests the entry and exit pins of a region and the cross-region edges made from them.
 * @see modules/surface/api/tests/README.md
 * @author Mustafa Garip
 */

#include <cstddef>
#include <vector>

#include <gtest/gtest.h>

#include <SushiRuntime/SushiRuntime.h>
#include <SushiRuntime/api/graph/boundary_access.hpp>

#include "dynamic_graph_plan_access.hpp"

using namespace SushiRuntime;

namespace
{
    using Interval = Internal::Memory::ResourceRegion;
    using Intervals = std::vector<Internal::Memory::ResourceRegion>;

    /** @brief Number of bytes of the allocation the pin tests slice. */
    constexpr std::size_t K_BYTES = 20;

    /** @brief Number of elements of the buffer the edge tests slice. */
    constexpr std::size_t K_ELEMENTS = 20;

    /** @brief Returns the pin of a node in a pin list, or nullptr when the node has none. */
    const detail::Pin* pin_of(const std::vector<detail::Pin>& pins,
                                   const Internal::Graph::Node& node)
    {
        for (const auto& pin : pins)
            if (pin.node == &node)
                return &pin;
        return nullptr;
    }

    /** @brief Returns whether one interval of a list fully contains wanted. */
    bool holds(const Intervals& intervals, const Interval& wanted)
    {
        return detail::covered_by_any(wanted, intervals);
    }

    /** @brief Returns the plan of a graph that requested profiling. */
    const Internal::Graph::CompiledGraph& plan_of(const DynamicGraph& world)
    {
        return detail::DynamicGraphNodeEventsTestAccess::plan(world);
    }
} // namespace

/** @brief Tests that a read reaching past its predecessor's write keeps an entry pin. */
TEST(Unit_BoundaryAccess, AReadPastItsPredecessorsWriteKeepsAnEntryPin)
{
    unsigned char bytes[K_BYTES] = {};
    void* base = bytes;

    Internal::Graph::TaskGraph graph;
    Internal::Graph::Node& p = graph.add_native({}, {Interval(base, 0, 10)}, [] {});
    Internal::Graph::Node& q = graph.add_native({Interval(base, 5, 15)}, {}, [] {});
    const auto nodes = graph.take_nodes();

    const auto access = detail::compute_boundary_access(nodes);
    ASSERT_EQ(access.count(base), 1u);
    const auto& entry = access.at(base).entry;

    ASSERT_NE(pin_of(entry, p), nullptr);
    const detail::Pin* q_pin = pin_of(entry, q);
    ASSERT_NE(q_pin, nullptr) << "bytes [15,20) of q are ordered behind no earlier region";
    EXPECT_TRUE(holds(q_pin->reads, Interval(base, 15, 5)));
    EXPECT_TRUE(q_pin->writes.empty());
}

/** @brief Tests that a read inside its predecessor's write has no entry pin. */
TEST(Unit_BoundaryAccess, AReadInsideItsPredecessorsWriteHasNoEntryPin)
{
    unsigned char bytes[K_BYTES] = {};
    void* base = bytes;

    Internal::Graph::TaskGraph graph;
    Internal::Graph::Node& p = graph.add_native({}, {Interval(base, 0, 20)}, [] {});
    Internal::Graph::Node& q = graph.add_native({Interval(base, 5, 15)}, {}, [] {});
    const auto nodes = graph.take_nodes();

    const auto access = detail::compute_boundary_access(nodes);
    const auto& entry = access.at(base).entry;

    EXPECT_NE(pin_of(entry, p), nullptr);
    EXPECT_EQ(pin_of(entry, q), nullptr) << "p carries every byte q reads";
    EXPECT_EQ(entry.size(), 1u);
}

/** @brief Tests that a write behind a predecessor that only reads keeps its write pin. */
TEST(Unit_BoundaryAccess, AWriteBehindAReadingPredecessorKeepsItsWritePin)
{
    unsigned char bytes[K_BYTES] = {};
    void* base = bytes;

    Internal::Graph::TaskGraph graph;
    Internal::Graph::Node& p = graph.add_native({Interval(base, 0, 10)}, {}, [] {});
    Internal::Graph::Node& q = graph.add_native({}, {Interval(base, 0, 10)}, [] {});
    const auto nodes = graph.take_nodes();

    const auto access = detail::compute_boundary_access(nodes);
    const auto& entry = access.at(base).entry;

    ASSERT_NE(pin_of(entry, p), nullptr);
    const detail::Pin* q_pin = pin_of(entry, q);
    ASSERT_NE(q_pin, nullptr) << "a reader does not follow the readers of earlier regions";
    EXPECT_TRUE(holds(q_pin->writes, Interval(base, 0, 10)));
}

/** @brief Tests that only a successor's write that contains an access shadows it on exit. */
TEST(Unit_BoundaryAccess, OnlyAContainingSuccessorWriteShadowsAnExit)
{
    unsigned char bytes[K_BYTES] = {};
    void* base = bytes;

    Internal::Graph::TaskGraph contained;
    Internal::Graph::Node& inner = contained.add_native({}, {Interval(base, 5, 5)}, [] {});
    Internal::Graph::Node& outer = contained.add_native({}, {Interval(base, 0, 20)}, [] {});
    const auto contained_nodes = contained.take_nodes();
    const auto shadowed = detail::compute_boundary_access(contained_nodes);
    EXPECT_EQ(pin_of(shadowed.at(base).exit, inner), nullptr);
    EXPECT_NE(pin_of(shadowed.at(base).exit, outer), nullptr);

    Internal::Graph::TaskGraph partial;
    Internal::Graph::Node& wide = partial.add_native({}, {Interval(base, 0, 20)}, [] {});
    Internal::Graph::Node& narrow = partial.add_native({}, {Interval(base, 5, 5)}, [] {});
    const auto partial_nodes = partial.take_nodes();
    const auto kept = detail::compute_boundary_access(partial_nodes);
    EXPECT_NE(pin_of(kept.at(base).exit, wide), nullptr) << "bytes outside [5,10) stay live";
    EXPECT_NE(pin_of(kept.at(base).exit, narrow), nullptr);
}

/** @brief Tests that an earlier region's writer orders a reader its predecessor half covers. */
TEST(Integration_BoundaryAccess, AnEarlierWriterOrdersAReaderItsPredecessorHalfCovers)
{
    auto rt = Runtime::create();
    rt.set_rebalancer_enabled(false);

    auto shared = rt.buffer<int>(K_ELEMENTS);
    auto total = rt.buffer<int>(1);
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
    world.run();

    const Internal::Graph::Node* w = Testing::find_plan_node(plan_of(world), "w");
    const Internal::Graph::Node* q = Testing::find_plan_node(plan_of(world), "q");
    ASSERT_NE(w, nullptr);
    ASSERT_NE(q, nullptr);
    EXPECT_TRUE(Testing::has_boundary_edge(*w, *q)) << "q reads [15,20) unordered against w";
    EXPECT_EQ(Testing::boundary_in_degree(*q), 1);
    EXPECT_EQ(total[0], 5 * 1 + 5 * 0 + 5 * 7);
}

/** @brief Tests that a reader its predecessor fully covers gains no edge of its own. */
TEST(Integration_BoundaryAccess, AReaderItsPredecessorCoversGainsNoEdge)
{
    auto rt = Runtime::create();
    rt.set_rebalancer_enabled(false);

    auto shared = rt.buffer<int>(K_ELEMENTS);
    auto total = rt.buffer<int>(1);
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
    world.run();

    const Internal::Graph::Node* w = Testing::find_plan_node(plan_of(world), "w");
    const Internal::Graph::Node* p = Testing::find_plan_node(plan_of(world), "p");
    const Internal::Graph::Node* q = Testing::find_plan_node(plan_of(world), "q");
    ASSERT_NE(w, nullptr);
    ASSERT_NE(p, nullptr);
    ASSERT_NE(q, nullptr);
    EXPECT_TRUE(Testing::has_boundary_edge(*w, *p));
    EXPECT_FALSE(Testing::has_boundary_edge(*w, *q)) << "p already orders q behind w";
    EXPECT_EQ(Testing::boundary_in_degree(*q), 0);
    EXPECT_EQ(total[0], 10 * 1 + 5 * 8);
}

/** @brief Tests that an earlier region's reader orders a writer whose predecessor only reads. */
TEST(Integration_BoundaryAccess, AnEarlierReaderOrdersAWriterBehindAReadingPredecessor)
{
    auto rt = Runtime::create();
    rt.set_rebalancer_enabled(false);

    auto shared = rt.buffer<int>(K_ELEMENTS);
    auto early = rt.buffer<int>(1);
    auto late = rt.buffer<int>(1);
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
    world.run();

    const Internal::Graph::Node* r = Testing::find_plan_node(plan_of(world), "r");
    const Internal::Graph::Node* q = Testing::find_plan_node(plan_of(world), "q");
    ASSERT_NE(r, nullptr);
    ASSERT_NE(q, nullptr);
    EXPECT_TRUE(Testing::has_boundary_edge(*r, *q)) << "q overwrites what r still reads";
    EXPECT_EQ(Testing::boundary_in_degree(*q), 1);
    EXPECT_EQ(early[0], 3);
    EXPECT_EQ(late[0], 3);
}
